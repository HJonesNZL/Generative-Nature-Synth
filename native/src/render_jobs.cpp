#include "json_text.hpp"
#include <algorithm>
#include <atlas/render_jobs.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/wav.hpp>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace atlas {
RenderJobError::RenderJobError(RenderJobErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
namespace {
using Code = RenderJobErrorCode;
[[noreturn]] void fail(Code code, std::string message) {
    throw RenderJobError(code, std::move(message));
}
bool validId(std::string_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](char value) {
               return (value >= 'a' && value <= 'z') ||
                      (value >= 'A' && value <= 'Z') ||
                      (value >= '0' && value <= '9') || value == '-' ||
                      value == '_';
           });
}
bool validHash(std::string_view hash) {
    return hash.size() == 64 &&
           std::all_of(hash.begin(), hash.end(), [](char value) {
               return (value >= '0' && value <= '9') ||
                      (value >= 'a' && value <= 'f');
           });
}
} // namespace
std::string soundscapeRenderRecipe(const Soundscape &scene,
                                   std::string_view sourceHash) {
    if (!validHash(sourceHash))
        fail(Code::invalidArgument,
             "Render engine source SHA-256 must be lowercase hex");
    const auto &capture = scene.capture();
    const auto regional = capture.schemaRevision >= 3
                              ? ",\"regional_tail_policy\":"
                                "\"active_wet_12s_final_20ms_fade\""
                              : "";
    return "{\"recipe_version\":" +
           std::to_string(capture.schemaRevision >= 3 ? 3 : 2) +
           ",\"engine\":\"atlas-additive\",\"scene_hash\":" +
           detail::quoteJson(scene.contentHash()) +
           ",\"scene_name\":" + detail::quoteJson(capture.name) +
           ",\"scene_schema_revision\":" +
           std::to_string(capture.schemaRevision) + ",\"synthesis_revision\":" +
           std::to_string(capture.synthesisRevision) +
           ",\"mapping_revision\":" + std::to_string(capture.mappingRevision) +
           ",\"time_basis\":" +
           detail::quoteJson(capture.basis == TimeBasis::observed ? "observed"
                                                                  : "arrival") +
           ",\"speed_numerator\":" + std::to_string(capture.speed.numerator) +
           ",\"speed_denominator\":" +
           std::to_string(capture.speed.denominator) +
           ",\"sample_rate\":" + std::to_string(capture.audio.sampleRate) +
           ",\"engine_source_sha256\":" + detail::quoteJson(sourceHash) +
           ",\"channel_count\":2,\"pcm_bits\":24,\"tail_policy\":\"editable_"
           "voice_maximum\"" +
           std::string(regional) +
           ",\"authoritative_recipe\":\"self-contained-atlas-"
           "scene\"}";
}
struct RenderJobs::Impl {
    struct Job {
        RenderJobSnapshot snapshot;
        std::atomic<std::uint64_t> framesWritten{}, totalFrames{};
        std::stop_source stop;
    };
    std::filesystem::path directory;
    std::string engineSourceHash;
    mutable std::mutex mutex;
    std::shared_ptr<Job> active;
    std::vector<std::shared_ptr<Job>> completed;
    std::jthread thread;
    Impl(std::filesystem::path path, std::string sourceHash)
        : directory(std::move(path)), engineSourceHash(std::move(sourceHash)) {
        completed.reserve(16);
        if (!validHash(engineSourceHash))
            fail(Code::invalidArgument,
                 "Render engine source SHA-256 must be lowercase hex");
        std::error_code error;
        const auto status = std::filesystem::symlink_status(directory, error);
        if (error || !std::filesystem::is_directory(status))
            fail(Code::invalidArgument, "Render output must be an existing "
                                        "owned directory, not a symlink");
    }
    ~Impl() {
        thread.request_stop();
        if (thread.joinable())
            thread.join();
    }
    std::shared_ptr<Job> find(std::string_view id) const {
        if (!validId(id))
            fail(Code::invalidArgument, "Invalid render job ID");
        if (active && active->snapshot.id == id)
            return active;
        const auto found = std::find_if(
            completed.begin(), completed.end(),
            [&](const auto &job) { return job->snapshot.id == id; });
        if (found == completed.end())
            fail(Code::notFound,
                 "Render job is absent or its retained status expired");
        return *found;
    }
    RenderJobSnapshot copy(const Job &job) const {
        auto result = job.snapshot;
        result.framesWritten =
            job.framesWritten.load(std::memory_order_acquire);
        result.totalFrames = job.totalFrames.load(std::memory_order_relaxed);
        result.cancelRequested = job.stop.stop_requested();
        return result;
    }
    void finish(const std::shared_ptr<Job> &job, RenderJobState state,
                std::string error = {}) {
        std::lock_guard lock(mutex);
        job->snapshot.state = state;
        job->snapshot.error = std::move(error);
        if (state != RenderJobState::completed)
            job->snapshot.outputFilename.clear();
        if (completed.size() == 16)
            completed.erase(completed.begin());
        completed.push_back(job);
    }
    void perform(std::stop_token stop, const std::shared_ptr<Job> &job,
                 const std::filesystem::path &scenePath,
                 const std::string &expectedHash) {
        if (stop.stop_requested())
            throw RenderCancelled();
        const auto scene = loadSoundscape(scenePath);
        if (scene.contentHash() != expectedHash)
            throw std::runtime_error("Soundscape changed after selection");
        if (stop.stop_requested())
            throw RenderCancelled();
        const auto &capture = scene.capture();
        AudioPlan plan(scene.timeline(), capture.audio, capture.profiles, true);
        if (stop.stop_requested())
            throw RenderCancelled();
        {
            std::lock_guard lock(mutex);
            job->snapshot.sampleRate = capture.audio.sampleRate;
        }
        const auto output = directory / job->snapshot.outputFilename;
        renderWave(plan, output,
                   soundscapeRenderRecipe(scene, engineSourceHash), 512,
                   {stop, &job->framesWritten, &job->totalFrames});
    }
    void render(std::stop_token stop, const std::shared_ptr<Job> &job,
                const std::filesystem::path &scenePath,
                const std::string &expectedHash) {
        RenderJobState state = RenderJobState::completed;
        std::string error;
        try {
            perform(stop, job, scenePath, expectedHash);
        } catch (const RenderCancelled &) {
            state = RenderJobState::cancelled;
        } catch (const SoundscapeError &) {
            state = RenderJobState::failed;
            error = "Soundscape verification or loading failed";
        } catch (const std::exception &) {
            state = RenderJobState::failed;
            error = "Offline WAV render failed; no output was overwritten";
        }
        // Large scene/plan and staging resources have already retired on this
        // worker. Terminal status is its last operation. Publication wins over
        // cancellation arriving after the final prepublication checkpoint.
        finish(job, state, std::move(error));
    }
};
RenderJobs::RenderJobs(std::filesystem::path directory, std::string sourceHash)
    : impl_(std::make_unique<Impl>(std::move(directory),
                                   std::move(sourceHash))) {}
RenderJobs::~RenderJobs() = default;
RenderJobSnapshot RenderJobs::start(const std::filesystem::path &scenePath,
                                    std::string expectedHash, std::string id) {
    if (!validId(id) || !validHash(expectedHash))
        fail(Code::invalidArgument,
             "Invalid render job ID or expected scene hash");
    std::lock_guard lock(impl_->mutex);
    if (impl_->active &&
        impl_->active->snapshot.state == RenderJobState::running)
        fail(Code::busy, "An offline render is already active");
    if (std::any_of(impl_->completed.begin(), impl_->completed.end(),
                    [&](const auto &job) { return job->snapshot.id == id; }))
        fail(Code::invalidArgument, "Render job ID has already been used");
    // The preceding thread has published terminal status as its last operation;
    // joining here cannot wait for file loading/rendering or another lock.
    if (impl_->thread.joinable())
        impl_->thread.join();
    auto job = std::make_shared<Impl::Job>();
    job->snapshot.id = std::move(id);
    job->snapshot.outputFilename = job->snapshot.id + ".wav";
    impl_->active = job;
    try {
        impl_->thread = std::jthread(
            [owner = impl_.get(), job, path = scenePath,
             hash = std::move(expectedHash)](std::stop_token stop) {
                owner->render(stop, job, path, hash);
            });
        job->stop = impl_->thread.get_stop_source();
    } catch (const std::exception &) {
        impl_->active.reset();
        fail(Code::storage, "Unable to start offline render worker");
    }
    return impl_->copy(*job);
}
RenderJobSnapshot RenderJobs::get(std::string_view id) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->copy(*impl_->find(id));
}
RenderJobSnapshot RenderJobs::cancel(std::string_view id) {
    std::lock_guard lock(impl_->mutex);
    const auto job = impl_->find(id);
    if (job->snapshot.state == RenderJobState::running)
        job->stop.request_stop();
    return impl_->copy(*job);
}
} // namespace atlas
