#include <algorithm>
#include <array>
#include <atlas/playback.hpp>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace atlas {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
using Clock = std::chrono::steady_clock;
constexpr auto leaseDuration = std::chrono::seconds(5);
} // namespace
const char *playbackErrorText(PlaybackError error) noexcept {
    switch (error) {
    case PlaybackError::none:
        return "none";
    case PlaybackError::notPrepared:
        return "not_prepared";
    case PlaybackError::disabled:
        return "audio_disabled";
    case PlaybackError::stalePreparation:
        return "stale_preparation";
    case PlaybackError::staleGeneration:
        return "stale_generation";
    case PlaybackError::invalidTransition:
        return "invalid_transition";
    case PlaybackError::deviceUnavailable:
        return "device_unavailable";
    case PlaybackError::unsupportedDevice:
        return "unsupported_device";
    case PlaybackError::deviceChanged:
        return "device_changed";
    case PlaybackError::callbackFailure:
        return "callback_failure";
    case PlaybackError::commandTimeout:
        return "command_timeout";
    case PlaybackError::leaseExpired:
        return "lease_expired";
    }
    return "unknown";
}
struct PlaybackRuntime::Impl {
    explicit Impl(std::unique_ptr<OutputDriver> output)
        : driver(std::move(output)) {
        if (!driver)
            throw std::invalid_argument("playback requires an output driver");
    }
    std::unique_ptr<OutputDriver> driver;
    std::unique_ptr<AudioPlan> plan;
    std::unique_ptr<AudioRenderer> renderer;
    std::uint64_t preparationId{};
    std::atomic<bool> enabled{}, started{};
    std::atomic<unsigned> failure{static_cast<unsigned>(PlaybackError::none)};
    std::atomic<std::uint64_t> snapshotSerial{}, position{}, generation{};
    std::atomic<unsigned> state{static_cast<unsigned>(AudioState::ready)};
    // One-slot SPSC mailbox: producer owns command until release request;
    // consumer owns it until release ack.
    PlaybackCommand pending;
    std::atomic<std::uint64_t> request{}, acknowledged{};
    std::atomic<bool> accepted{};
    std::array<float, AudioRenderer::maxBlockFrames * 2> scratch{};
    Clock::time_point lastHeartbeat{Clock::now()};
    std::atomic<std::uint64_t> heartbeatSerial{};
    std::uint64_t callbackHeartbeat{}, remainingLeaseFrames{};
    void publish() noexcept {
        snapshotSerial.fetch_add(1, std::memory_order_seq_cst);
        position.store(renderer ? renderer->position() : 0,
                       std::memory_order_seq_cst);
        generation.store(renderer ? renderer->generation() : 0,
                         std::memory_order_seq_cst);
        state.store(static_cast<unsigned>(renderer ? renderer->state()
                                                   : AudioState::ready),
                    std::memory_order_seq_cst);
        snapshotSerial.fetch_add(1, std::memory_order_seq_cst);
    }
    PlaybackError error() const noexcept {
        return static_cast<PlaybackError>(
            failure.load(std::memory_order_acquire));
    }
    bool apply(const PlaybackCommand &command) noexcept {
        if (!renderer)
            return false;
        const bool safety = (command.kind == PlaybackCommandKind::stop ||
                             command.kind == PlaybackCommandKind::panic) &&
                            command.preparationId == 0 &&
                            command.generation == 0;
        if (!safety && (command.preparationId != preparationId ||
                        command.generation != renderer->generation()))
            return false;
        const auto current = renderer->generation();
        switch (command.kind) {
        case PlaybackCommandKind::play:
            return renderer->play(current);
        case PlaybackCommandKind::pause:
            return renderer->pause(current);
        case PlaybackCommandKind::stop:
            return renderer->stop(current);
        case PlaybackCommandKind::seek:
            return renderer->seek(command.frame, current);
        case PlaybackCommandKind::panic:
            return renderer->panic(current);
        }
        return false;
    }
    void close(PlaybackError error) noexcept {
        enabled.store(false, std::memory_order_release);
        driver->stop(); // Callback quiescence before touching renderer,
                        // mailbox, scratch or plan ownership.
        if (renderer)
            renderer->panic(renderer->generation());
        started.store(false, std::memory_order_release);
        acknowledged.store(request.load(std::memory_order_relaxed),
                           std::memory_order_release);
        failure.store(static_cast<unsigned>(error), std::memory_order_release);
        publish();
    }
};
PlaybackRuntime::PlaybackRuntime(std::unique_ptr<OutputDriver> driver)
    : impl_(std::make_unique<Impl>(std::move(driver))) {}
PlaybackRuntime::~PlaybackRuntime() { impl_->close(PlaybackError::none); }
PlaybackSnapshot PlaybackRuntime::snapshot() const noexcept {
    const auto &p = *impl_;
    PlaybackSnapshot value;
    value.prepared = p.plan != nullptr;
    value.preparationId = p.preparationId;
    if (p.plan) {
        value.sampleRate = p.plan->settings().sampleRate;
        value.totalFrames = p.plan->totalFrames();
        value.gainDb = p.plan->settings().masterDb;
    }
    // Only the worker retries. All payload fields are atomic; the callback
    // never waits for a reader.
    for (;;) {
        const auto before = p.snapshotSerial.load(std::memory_order_seq_cst);
        if (before & 1u)
            continue;
        value.generation = p.generation.load(std::memory_order_seq_cst);
        value.positionFrames = p.position.load(std::memory_order_seq_cst);
        value.state =
            static_cast<AudioState>(p.state.load(std::memory_order_seq_cst));
        if (before == p.snapshotSerial.load(std::memory_order_seq_cst))
            break;
    }
    value.error = p.error();
    value.enabled = p.enabled.load(std::memory_order_acquire) &&
                    value.error == PlaybackError::none;
    return value;
}
PlaybackResult PlaybackRuntime::prepare(const Timeline &timeline,
                                        AudioSettings settings) {
    impl_->close(PlaybackError::none);
    auto nextPlan = std::make_unique<AudioPlan>(timeline, settings);
    auto nextRenderer = std::make_unique<AudioRenderer>(*nextPlan);
    impl_->renderer = std::move(
        nextRenderer); // Destroy borrower before retiring the old plan.
    impl_->plan = std::move(nextPlan);
    ++impl_->preparationId;
    impl_->publish();
    return {true, PlaybackError::none, snapshot()};
}
PlaybackResult PlaybackRuntime::enable(bool enabled) {
    auto &p = *impl_;
    if (!enabled) {
        p.close(PlaybackError::none);
        return {true, PlaybackError::none, snapshot()};
    }
    if (!p.plan)
        return {false, PlaybackError::notPrepared, snapshot()};
    if (p.enabled.load(std::memory_order_acquire) &&
        p.error() == PlaybackError::none)
        return {true, PlaybackError::none, snapshot()};
    p.close(PlaybackError::none);
    heartbeat();
    p.callbackHeartbeat = p.heartbeatSerial.load(std::memory_order_acquire);
    p.remainingLeaseFrames =
        static_cast<std::uint64_t>(p.plan->settings().sampleRate) * 5;
    p.enabled.store(true, std::memory_order_release);
    std::string deviceError;
    try {
        deviceError = p.driver->start(*this, p.plan->settings().sampleRate);
    } catch (...) {
        p.close(PlaybackError::deviceUnavailable);
        return {false, PlaybackError::deviceUnavailable, snapshot()};
    }
    if (!deviceError.empty() || !p.started.load(std::memory_order_acquire) ||
        p.error() != PlaybackError::none) {
        const auto error = p.error() == PlaybackError::none
                               ? PlaybackError::deviceUnavailable
                               : p.error();
        p.close(error);
        return {false, error, snapshot()};
    }
    return {true, PlaybackError::none, snapshot()};
}
PlaybackResult PlaybackRuntime::command(PlaybackCommand command) {
    auto &p = *impl_;
    service();
    const auto current = snapshot();
    const bool safety = (command.kind == PlaybackCommandKind::stop ||
                         command.kind == PlaybackCommandKind::panic) &&
                        command.preparationId == 0 && command.generation == 0;
    if (!p.renderer)
        return {safety,
                safety ? PlaybackError::none : PlaybackError::notPrepared,
                current};
    if (!safety && command.preparationId != current.preparationId)
        return {false, PlaybackError::stalePreparation, current};
    if (!safety && command.generation != current.generation)
        return {false, PlaybackError::staleGeneration, current};
    if (!current.enabled) {
        if (command.kind == PlaybackCommandKind::play ||
            command.kind == PlaybackCommandKind::pause)
            return {false,
                    current.error == PlaybackError::none
                        ? PlaybackError::disabled
                        : current.error,
                    current};
        const auto ok = p.apply(command);
        p.publish();
        return {ok, ok ? PlaybackError::none : PlaybackError::invalidTransition,
                snapshot()};
    }
    p.pending = command;
    const auto sequence = p.request.load(std::memory_order_relaxed) + 1;
    p.request.store(sequence, std::memory_order_release);
    const auto deadline = Clock::now() + std::chrono::milliseconds(250);
    while (p.acknowledged.load(std::memory_order_acquire) != sequence) {
        if (p.error() != PlaybackError::none || Clock::now() >= deadline) {
            const auto error = p.error() == PlaybackError::none
                                   ? PlaybackError::commandTimeout
                                   : p.error();
            p.close(error);
            return {false, error, snapshot()};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto ok = p.accepted.load(std::memory_order_acquire);
    return {ok, ok ? PlaybackError::none : PlaybackError::invalidTransition,
            snapshot()};
}
void PlaybackRuntime::heartbeat() noexcept {
    impl_->lastHeartbeat = Clock::now();
    impl_->heartbeatSerial.fetch_add(1, std::memory_order_release);
}
void PlaybackRuntime::service() {
    auto &p = *impl_;
    if (!p.enabled.load(std::memory_order_acquire))
        return;
    if (p.error() != PlaybackError::none)
        p.close(p.error());
    else if (Clock::now() - p.lastHeartbeat >= leaseDuration)
        p.close(PlaybackError::leaseExpired);
}
void PlaybackRuntime::deviceStarted(double rate, int frames,
                                    int channels) noexcept {
    auto &p = *impl_;
    if (p.started.exchange(true, std::memory_order_acq_rel)) {
        deviceFailed(PlaybackError::deviceChanged);
        return;
    }
    if (!p.plan || rate != static_cast<double>(p.plan->settings().sampleRate) ||
        frames <= 0 ||
        frames > static_cast<int>(AudioRenderer::maxBlockFrames) ||
        channels < 2 || channels > OutputDriver::maxOutputChannels)
        deviceFailed(PlaybackError::unsupportedDevice);
}
void PlaybackRuntime::deviceFailed(PlaybackError error) noexcept {
    unsigned expected = static_cast<unsigned>(PlaybackError::none);
    impl_->failure.compare_exchange_strong(
        expected, static_cast<unsigned>(error), std::memory_order_acq_rel);
}
void PlaybackRuntime::process(float *const *outputs, int channels,
                              int frames) noexcept {
    auto &p = *impl_;
    if (frames < 0 || channels < 0) {
        deviceFailed(PlaybackError::callbackFailure);
        return;
    }
    // Hard driver limits bound even malformed counts. Valid driver extents are
    // fully cleared.
    const auto clearChannels =
        std::min(channels, OutputDriver::maxOutputChannels);
    const auto clearFrames = std::min(frames, OutputDriver::maxCallbackFrames);
    if (outputs)
        for (int channel = 0; channel < clearChannels; ++channel)
            if (outputs[channel])
                std::fill_n(outputs[channel], clearFrames, 0.0f);
    if (!frames)
        return;
    if (!outputs || channels < 2 ||
        channels > OutputDriver::maxOutputChannels || !outputs[0] ||
        !outputs[1] ||
        frames > static_cast<int>(AudioRenderer::maxBlockFrames)) {
        deviceFailed(PlaybackError::callbackFailure);
        return;
    }
    if (!p.enabled.load(std::memory_order_acquire) ||
        p.error() != PlaybackError::none || !p.renderer)
        return;
    const auto beat = p.heartbeatSerial.load(std::memory_order_acquire);
    if (beat != p.callbackHeartbeat) {
        p.callbackHeartbeat = beat;
        p.remainingLeaseFrames =
            static_cast<std::uint64_t>(p.plan->settings().sampleRate) * 5;
    }
    const auto blockFrames = static_cast<std::uint64_t>(frames);
    if (blockFrames >= p.remainingLeaseFrames) {
        p.renderer->panic(p.renderer->generation());
        deviceFailed(PlaybackError::leaseExpired);
        p.publish();
        return;
    }
    p.remainingLeaseFrames -= blockFrames;
    const auto request = p.request.load(std::memory_order_acquire);
    const bool hasCommand =
        request != p.acknowledged.load(std::memory_order_relaxed);
    if (hasCommand)
        p.accepted.store(p.apply(p.pending), std::memory_order_relaxed);
    const auto count = static_cast<std::size_t>(frames);
    if (!p.renderer->process(std::span<float>(p.scratch.data(), count * 2))) {
        deviceFailed(PlaybackError::callbackFailure);
    } else if (p.error() == PlaybackError::none) {
        for (std::size_t frame = 0; frame < count; ++frame) {
            outputs[0][frame] = p.scratch[frame * 2];
            outputs[1][frame] = p.scratch[frame * 2 + 1];
        }
    }
    p.publish();
    if (hasCommand)
        p.acknowledged.store(request, std::memory_order_release);
}
} // namespace atlas
