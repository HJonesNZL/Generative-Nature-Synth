#include <array>
#include <atlas/render_jobs.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/voices.hpp>
#include <atlas/wav.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace atlas;
using namespace std::chrono_literals;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void error(RenderJobErrorCode code, F action) {
    try {
        action();
    } catch (const RenderJobError &failure) {
        check(failure.code() == code, "Wrong render job error code");
        return;
    }
    throw std::runtime_error("Expected render job failure");
}
struct Temporary {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("atlas-render-jobs-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directory(directory); }
    ~Temporary() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    std::filesystem::path file(std::string_view name) const {
        return directory / name;
    }
};
Soundscape scene(Microseconds duration = 200000,
                 std::uint32_t sampleRate = 44100) {
    const auto start = parseUtc("2026-11-07T00:00:00Z");
    std::vector<Event> events;
    SoundscapeCapture capture;
    capture.name = "Disposable export fixture";
    capture.sourceLibraryLabel = "Source already absent";
    capture.session.id = "disposable";
    capture.session.name = "Synthetic copied material";
    capture.session.state = "frozen";
    capture.session.origin = "demo";
    capture.session.contentHash = std::string(64, '0');
    capture.session.metadataIntegrity = "legacy-metadata-unverified";
    capture.session.observedWindowPolicy = "preserve_requested";
    capture.session.eventCount = 6;
    capture.session.observedBounds = std::pair{start, start + duration};
    capture.session.arrivalBounds = capture.session.observedBounds;
    capture.bounds = capture.session.observedBounds;
    capture.audio = {sampleRate, 60, .009, .71, -17};
    capture.audio.composition = {
        true, CompositionScale::minorPentatonic, 7, 113,
        CompositionDensity::active, .61999999999999988, 812345, true};
    capture.profiles.push_back({"", {}});
    for (int i = 0; i < 6; ++i) {
        const auto suffix = std::to_string(i), fraction = "0" + suffix + "0000";
        const auto stamp = "2026-11-07T00:00:00." + fraction + "Z";
        Event event;
        event.id = "fixture:" + suffix;
        event.sourceId = "fixture";
        event.kind = "gps_fix";
        event.locationKind = "individual_fix";
        event.speciesId = "fixture:species-" + suffix;
        event.individualId = "fixture:individual-" + suffix;
        event.observedAt = start + i * 10000;
        event.firstSeenAt = event.observedAt;
        event.ingestedAt = event.observedAt;
        event.revisionSequence = i + 1;
        event.normalizedJson =
            "{\"schema_version\":1,\"event_id\":\"" + event.id +
            "\",\"source_id\":\"fixture\",\"provider_event_id\":\"" + suffix +
            "\",\"observed_at_utc\":\"" + stamp +
            "\",\"event_kind\":\"gps_fix\",\"location_kind\":\"individual_"
            "fix\",\"species_id\":\"" +
            *event.speciesId + "\",\"individual_id\":\"" + *event.individualId +
            "\",\"latitude\":null,\"longitude\":null,\"coordinate_order\":"
            "\"latitude,longitude\",\"provenance_mode\":\"demo\",\"first_seen_"
            "at_utc\":\"" +
            stamp + "\",\"ingested_at_utc\":\"" + stamp + "\"}";
        VoiceSettings settings{i - 3, .006 + i * .002, .4 + i * .1, -2.0 * i};
        settings.character = static_cast<VoiceCharacter>(i);
        settings.brightness = .1 + i * .15;
        settings.roughness = i * .12;
        settings.pulseHz = i + 1;
        settings.sweepSemitones = i - 2;
        capture.profiles.push_back({*event.speciesId, settings});
        events.push_back(std::move(event));
    }
    Timeline timeline(std::move(events), capture.basis, capture.speed,
                      capture.bounds);
    return captureSoundscape(timeline, std::move(capture));
}
RenderJobSnapshot terminal(RenderJobs &jobs, std::string_view id) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        auto snapshot = jobs.get(id);
        check(snapshot.framesWritten <= snapshot.totalFrames,
              "Render progress exceeded total");
        if (snapshot.state != RenderJobState::running)
            return snapshot;
        check(std::chrono::steady_clock::now() < deadline,
              "Render job did not terminate");
        std::this_thread::sleep_for(1ms);
    }
}
void partial(RenderJobs &jobs, std::string_view id) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        const auto snapshot = jobs.get(id);
        check(snapshot.state == RenderJobState::running,
              "Long render unexpectedly finished before cancel");
        if (snapshot.framesWritten > 0)
            return;
        check(std::chrono::steady_clock::now() < deadline,
              "Long render produced no progress");
        std::this_thread::sleep_for(1ms);
    }
}
std::vector<unsigned char> read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    check(file.good(), "Expected output absent");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
std::uint32_t u32(const std::vector<unsigned char> &bytes, std::size_t offset) {
    check(offset + 4 <= bytes.size(), "Truncated WAV field");
    return bytes[offset] |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
struct Wave {
    std::vector<unsigned char> data;
    std::string bytes;
};
Wave wave(const std::filesystem::path &path, std::uint32_t rate) {
    const auto bytes = read(path);
    Wave result;
    check(bytes.size() >= 80 && u32(bytes, 4) + 8 == bytes.size(),
          "Invalid RIFF size");
    result.bytes.assign(bytes.begin(), bytes.end());
    check(result.bytes.substr(0, 4) == "RIFF" &&
              result.bytes.substr(8, 4) == "WAVE",
          "Invalid WAV signature");
    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const auto size = u32(bytes, offset + 4);
        check(offset + 8 + size <= bytes.size(), "Invalid WAV chunk size");
        const auto name = result.bytes.substr(offset, 4);
        if (name == "fmt ") {
            check(u32(bytes, offset + 12) == rate && bytes[offset + 10] == 2 &&
                      bytes[offset + 22] == 24,
                  "Scene rate or PCM format changed");
        }
        if (name == "data")
            result.data.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
        offset += 8 + size + size % 2;
    }
    check(!result.data.empty(), "WAV PCM absent");
    return result;
}
std::vector<unsigned char> reference(const Soundscape &source,
                                     unsigned block = 79) {
    AudioPlan plan(source.timeline(), source.capture().audio,
                   source.capture().profiles, true);
    AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()),
          "Reference render did not start");
    std::array<float, 8192> samples{};
    std::vector<unsigned char> result;
    result.reserve(static_cast<std::size_t>(plan.totalFrames()) * 6);
    for (std::uint64_t frame = 0; frame < plan.totalFrames();) {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(block, plan.totalFrames() - frame));
        check(renderer.process(std::span(samples).first(count * 2)),
              "Reference render failed");
        for (std::size_t i = 0; i < count * 2; ++i) {
            const auto value =
                static_cast<std::uint32_t>(static_cast<std::int32_t>(
                    std::lround(static_cast<double>(samples[i]) * 8388607)));
            for (unsigned shift = 0; shift < 3; ++shift)
                result.push_back(
                    static_cast<unsigned char>((value >> (shift * 8)) & 255));
        }
        frame += count;
    }
    check(renderer.statistics().faults == 0, "Reference DSP fault");
    return result;
}
} // namespace
int main() {
    try {
        Temporary temp;
        const auto output = temp.file("renders");
        std::filesystem::create_directory(output);
        const auto shortScene = scene(), longScene = scene(1800000000, 48000);
        const auto shortPath = temp.file("short.atlas-scene"),
                   longPath = temp.file("long.atlas-scene");
        saveSoundscape(shortPath, shortScene);
        saveSoundscape(longPath, longScene);
        const std::string sourceHash(64, 'a');
        RenderJobs jobs(output, sourceHash);
        error(RenderJobErrorCode::invalidArgument, [&] {
            jobs.start(shortPath, shortScene.contentHash(), "../escape");
        });
        error(RenderJobErrorCode::invalidArgument,
              [&] { jobs.start(shortPath, "invalid", "bad-hash"); });
        error(RenderJobErrorCode::invalidArgument, [&] { jobs.get(""); });
        error(RenderJobErrorCode::notFound, [&] { jobs.get("unknown"); });
        error(RenderJobErrorCode::notFound, [&] { jobs.cancel("unknown"); });
        const auto began = std::chrono::steady_clock::now();
        check(jobs.start(longPath, longScene.contentHash(), "cancel-active")
                      .state == RenderJobState::running,
              "Start did not return active state");
        check(std::chrono::steady_clock::now() - began < 1s,
              "Start synchronously loaded/rendered the scene");
        error(RenderJobErrorCode::busy, [&] {
            jobs.start(shortPath, shortScene.contentHash(), "competing");
        });
        partial(jobs, "cancel-active");
        jobs.cancel("cancel-active");
        const auto cancelled = terminal(jobs, "cancel-active");
        check(cancelled.state == RenderJobState::cancelled &&
                  cancelled.cancelRequested && cancelled.framesWritten > 0 &&
                  cancelled.outputFilename.empty(),
              "Cancellation status wrong");
        check(!std::filesystem::exists(output / "cancel-active.wav") &&
                  !std::filesystem::exists(output /
                                           "cancel-active.wav.rendering"),
              "Cancelled partial artifact/staging leaked");
        error(RenderJobErrorCode::invalidArgument, [&] {
            jobs.start(shortPath, shortScene.contentHash(), "cancel-active");
        });
        jobs.start(shortPath, shortScene.contentHash(), "complete");
        // An unrelated mutable preset cannot change the frozen offline recipe.
        {
            VoiceStore presets(temp.file("mutable-voices.sqlite3"));
            presets.setParameter("default", "gain", 0, VoiceGesture::begin);
            presets.setParameter("default", "gain", 0, VoiceGesture::end);
            presets.save("default", 0);
        }
        const auto done = terminal(jobs, "complete");
        check(done.state == RenderJobState::completed &&
                  done.framesWritten == done.totalFrames &&
                  done.sampleRate == 44100 &&
                  done.outputFilename == "complete.wav" && done.error.empty(),
              "Completed render status wrong");
        const auto actual = wave(output / done.outputFilename, 44100);
        check(actual.data == reference(shortScene),
              "Offline PCM differs from live AudioRenderer profile/tail/block "
              "semantics");
        check(actual.bytes.find(shortScene.contentHash()) !=
                      std::string::npos &&
                  actual.bytes.find(sourceHash) != std::string::npos &&
                  actual.bytes.find("\"scene_schema_revision\":2") !=
                      std::string::npos &&
                  actual.bytes.find("\"synthesis_revision\":3") !=
                      std::string::npos &&
                  actual.bytes.find("\"mapping_revision\":2") !=
                      std::string::npos &&
                  actual.bytes.find("editable_voice_maximum") !=
                      std::string::npos,
              "WAV recipe lost scene/source/tail traceability");
        check(jobs.cancel("complete").state == RenderJobState::completed &&
                  read(output / done.outputFilename).size() ==
                      actual.bytes.size(),
              "Late cancellation invalidated published artifact");
        auto legacyCapture = shortScene.capture();
        legacyCapture.schemaRevision = 1;
        legacyCapture.synthesisRevision = 2;
        legacyCapture.mappingRevision = 1;
        legacyCapture.audio.composition = {};
        const auto legacy =
            captureSoundscape(shortScene.timeline(), legacyCapture);
        const auto legacyPath = temp.file("legacy.atlas-scene");
        saveSoundscape(legacyPath, legacy);
        jobs.start(legacyPath, legacy.contentHash(), "legacy");
        const auto legacyDone = terminal(jobs, "legacy");
        check(legacyDone.state == RenderJobState::completed,
              "Legacy export failed");
        const auto legacyWave = wave(output / legacyDone.outputFilename, 44100);
        check(legacyWave.data == reference(legacy) &&
                  legacyWave.bytes.find("\"scene_schema_revision\":1") !=
                      std::string::npos &&
                  legacyWave.bytes.find("\"synthesis_revision\":2") !=
                      std::string::npos &&
                  legacyWave.bytes.find("\"mapping_revision\":1") !=
                      std::string::npos,
              "Legacy export changed PCM or substituted current revisions");
        AudioPlan plan(shortScene.timeline(), shortScene.capture().audio,
                       shortScene.capture().profiles, true);
        renderWave(plan, output / "partition.wav", "{}", 17);
        check(wave(output / "partition.wav", 44100).data == actual.data,
              "Render block partition changed PCM");
        std::stop_source stopped;
        stopped.request_stop();
        try {
            renderWave(plan, output / "pre-cancel.wav", "{}", 512,
                       {stopped.get_token()});
            throw std::runtime_error("Precancel ignored");
        } catch (const RenderCancelled &) {
        }
        check(!std::filesystem::exists(output / "pre-cancel.wav.rendering"),
              "Precancel created staging");
        jobs.start(shortPath, std::string(64, 'b'), "wrong-scene-hash");
        check(terminal(jobs, "wrong-scene-hash").state ==
                  RenderJobState::failed,
              "Changed scene hash rendered");
        const auto corruptPath = temp.file("corrupt.atlas-scene");
        {
            std::ofstream file(corruptPath);
            file << "not SQLite";
        }
        jobs.start(corruptPath, shortScene.contentHash(), "corrupt");
        const auto failed = terminal(jobs, "corrupt");
        check(failed.state == RenderJobState::failed && !failed.error.empty() &&
                  failed.outputFilename.empty(),
              "Corrupt scene failure hidden");
        {
            std::ofstream existing(output / "occupied.wav");
            existing << "owner content";
        }
        jobs.start(shortPath, shortScene.contentHash(), "occupied");
        check(terminal(jobs, "occupied").state == RenderJobState::failed,
              "Existing output overwritten");
        const auto occupied = read(output / "occupied.wav");
        check(std::string(occupied.begin(), occupied.end()) == "owner content",
              "Existing output damaged");
        std::filesystem::create_directory(output /
                                          "occupied-stage.wav.rendering");
        {
            std::ofstream marker(output / "occupied-stage.wav.rendering" /
                                 "keep");
            marker << "owned elsewhere";
        }
        jobs.start(shortPath, shortScene.contentHash(), "occupied-stage");
        check(terminal(jobs, "occupied-stage").state ==
                      RenderJobState::failed &&
                  std::filesystem::exists(
                      output / "occupied-stage.wav.rendering" / "keep"),
              "Foreign staging removed or adopted");
        for (int i = 0; i < 17; ++i) {
            const auto id = "retained-" + std::to_string(i);
            jobs.start(shortPath, shortScene.contentHash(), id);
            check(terminal(jobs, id).state == RenderJobState::completed,
                  "Repeated render failed");
        }
        error(RenderJobErrorCode::notFound, [&] { jobs.get("retained-0"); });
        check(jobs.get("retained-1").state == RenderJobState::completed &&
                  jobs.get("retained-16").state == RenderJobState::completed,
              "Bounded terminal history wrong");
        for (int i = 0; i < 6; ++i) {
            const auto id = "race-" + std::to_string(i);
            jobs.start(shortPath, shortScene.contentHash(), id);
            jobs.cancel(id);
            const auto result = terminal(jobs, id);
            const bool published =
                std::filesystem::exists(output / (id + ".wav"));
            check((result.state == RenderJobState::completed && published) ||
                      (result.state == RenderJobState::cancelled && !published),
                  "Cancel/publish race reported inconsistent final state");
        }
        {
            RenderJobs shuttingDown(output, sourceHash);
            shuttingDown.start(longPath, longScene.contentHash(), "shutdown");
            partial(shuttingDown, "shutdown");
        }
        check(!std::filesystem::exists(output / "shutdown.wav") &&
                  !std::filesystem::exists(output / "shutdown.wav.rendering"),
              "Shutdown did not cancel/join/clean up");
        const auto link = temp.file("output-link");
        std::filesystem::create_directory_symlink(output, link);
        error(RenderJobErrorCode::invalidArgument,
              [&] { RenderJobs rejected(link, sourceHash); });
        std::cout << "Offline jobs: exact frozen scene PCM/rate/tails, bounded "
                     "async control/history, cancellation/shutdown, no "
                     "overwrite, failures and recipe traceability passed\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
