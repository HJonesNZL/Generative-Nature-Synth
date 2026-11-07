#include <algorithm>
#include <array>
#include <atlas/playback.hpp>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace atlas {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
using Clock = std::chrono::steady_clock;
bool validConfiguration(const AudioDeviceConfiguration &configuration) {
    if (configuration.bufferFrames < 0 ||
        configuration.bufferFrames >
            static_cast<int>(AudioRenderer::maxBlockFrames))
        return false;
    if (configuration.mode == AudioDeviceMode::systemDefault)
        return configuration.deviceId.empty();
    return configuration.mode == AudioDeviceMode::explicitDevice &&
           !configuration.deviceId.empty() &&
           configuration.deviceId.size() <= 64 &&
           std::all_of(configuration.deviceId.begin(),
                       configuration.deviceId.end(), [](unsigned char c) {
                           return (c >= 'a' && c <= 'z') ||
                                  (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '-';
                       });
}
PlaybackError validateDevice(const AudioDeviceCatalog &catalog,
                             const AudioDeviceConfiguration &configuration,
                             std::uint32_t rate) {
    if (!validConfiguration(configuration))
        return PlaybackError::invalidDeviceConfiguration;
    if (!catalog.available)
        return PlaybackError::deviceUnavailable;
    const auto selected = std::find_if(
        catalog.devices.begin(), catalog.devices.end(),
        [&](const AudioDeviceInfo &device) {
            return configuration.mode == AudioDeviceMode::systemDefault
                       ? device.isDefault
                       : device.id == configuration.deviceId;
        });
    if (selected == catalog.devices.end())
        return PlaybackError::deviceUnavailable;
    const auto block = configuration.bufferFrames
                           ? configuration.bufferFrames
                           : selected->defaultBufferFrames;
    if (selected->outputChannels < 2 || block <= 0 ||
        block > static_cast<int>(AudioRenderer::maxBlockFrames) ||
        (configuration.bufferFrames &&
         std::find(selected->bufferFrames.begin(), selected->bufferFrames.end(),
                   block) == selected->bufferFrames.end()) ||
        (rate &&
         std::find(selected->sampleRates.begin(), selected->sampleRates.end(),
                   rate) == selected->sampleRates.end()))
        return PlaybackError::unsupportedDevice;
    return PlaybackError::none;
}
constexpr auto leaseDuration = std::chrono::seconds(5);
} // namespace
const char *playbackErrorText(PlaybackError error) noexcept {
    switch (error) {
    case PlaybackError::liveNoteRejected:
        return "live_note_rejected";
    case PlaybackError::none:
        return "none";
    case PlaybackError::staleControlRevision:
        return "stale_control_revision";
    case PlaybackError::invalidDeviceConfiguration:
        return "invalid_device_configuration";
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
    case PlaybackError::invalidVoiceSettings:
        return "invalid_voice_settings";
    }
    return "unknown";
}
struct PlaybackRuntime::Impl {
    explicit Impl(std::unique_ptr<OutputDriver> output,
                  AudioDeviceConfiguration initial)
        : driver(std::move(output)), configuration(std::move(initial)) {
        if (!validConfiguration(configuration))
            throw std::invalid_argument(
                "invalid initial audio device configuration");
        if (!driver)
            throw std::invalid_argument("playback requires an output driver");
    }
    std::unique_ptr<OutputDriver> driver;
    AudioDeviceConfiguration configuration;
    AudioDeviceCatalog catalog;
    std::optional<ActiveAudioDevice> activeDevice;
    bool catalogInitialized{};
    std::uint64_t controlRevision{1};
    std::atomic<std::uint64_t> callbackCount{}, callbackFrames{}, faultCount{};
    std::atomic<unsigned> deviceRate{}, deviceBlock{}, deviceChannels{};
    std::atomic<float> outputPeakLeft{}, outputPeakRight{};
    std::uint64_t peakHoldLeft{}, peakHoldRight{};
    std::unique_ptr<AudioPlan> plan;
    std::unique_ptr<AudioRenderer> renderer;
    std::uint64_t preparationId{};
    bool live{};
    std::size_t suppressedSourceEvents{}, routedNotes{};
    std::atomic<bool> enabled{}, started{};
    std::atomic<unsigned> failure{static_cast<unsigned>(PlaybackError::none)};
    std::atomic<std::uint64_t> snapshotSerial{}, position{}, generation{};
    std::atomic<unsigned> state{static_cast<unsigned>(AudioState::ready)};
    // One-slot SPSC mailbox: producer owns command until release request;
    // consumer owns it until release ack.
    static constexpr std::uint64_t liveCapacity = 64;
    struct QueuedLiveNote {
        PlaybackCommand command;
        std::uint64_t id{};
    };
    std::array<QueuedLiveNote, liveCapacity> liveQueue{};
    std::atomic<std::uint64_t> liveWrite{}, liveRead{}, liveAcknowledged{},
        liveTriggered{};
    std::uint64_t lastSubmittedId{}, nextLiveFrame{};
    void cancelLive() noexcept {
        liveRead.store(liveWrite.load(std::memory_order_acquire),
                       std::memory_order_release);
        nextLiveFrame = 0;
    }
    // Callback writes an entire block before publishing it to the host worker.
    std::array<LiveVoiceStart, 4096> voiceStarts{};
    std::atomic<std::uint64_t> startWrite{}, startRead{}, lostStarts{};
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
    bool apply(const PlaybackCommand &command,
               LiveVoiceFrames *frames = nullptr) noexcept {
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
        case PlaybackCommandKind::liveNote:
            if (command.liveVoiceUpdate &&
                !renderer->updateVoice(command.voiceSlot,
                                       command.voiceCoefficients))
                return false;
            return live &&
                   renderer->triggerLive(command.voiceSlot, command.midiNote,
                                         command.phaseStep, command.velocity,
                                         command.gateFrames, command.route,
                                         command.liveToken, frames);
        case PlaybackCommandKind::play:
            return renderer->play(current);
        case PlaybackCommandKind::pause:
            return renderer->pause(current);
        case PlaybackCommandKind::stop:
            cancelLive();
            return renderer->stop(current);
        case PlaybackCommandKind::seek:
            cancelLive();
            return renderer->seek(command.frame, current);
        case PlaybackCommandKind::panic:
            cancelLive();
            return renderer->panic(current);
        case PlaybackCommandKind::updateVoice:
            return renderer->updateVoice(command.voiceSlot,
                                         command.voiceCoefficients);
        }
        return false;
    }
    void close(PlaybackError error) noexcept {
        enabled.store(false, std::memory_order_release);
        driver->stop(); // Callback quiescence before touching renderer,
                        // mailbox, scratch or plan ownership.
        if (renderer)
            renderer->panic(renderer->generation());
        cancelLive();
        started.store(false, std::memory_order_release);
        activeDevice.reset();
        deviceRate.store(0, std::memory_order_release);
        deviceBlock.store(0, std::memory_order_release);
        deviceChannels.store(0, std::memory_order_release);
        outputPeakLeft.store(0, std::memory_order_relaxed);
        outputPeakRight.store(0, std::memory_order_relaxed);
        peakHoldLeft = peakHoldRight = 0;
        acknowledged.store(request.load(std::memory_order_relaxed),
                           std::memory_order_release);
        const auto previousError = failure.exchange(
            static_cast<unsigned>(error), std::memory_order_acq_rel);
        if (error != PlaybackError::none &&
            previousError == static_cast<unsigned>(PlaybackError::none))
            faultCount.fetch_add(1, std::memory_order_relaxed);
        publish();
    }
};
PlaybackRuntime::PlaybackRuntime(std::unique_ptr<OutputDriver> driver,
                                 AudioDeviceConfiguration initial)
    : impl_(std::make_unique<Impl>(std::move(driver), std::move(initial))) {}
PlaybackRuntime::~PlaybackRuntime() { impl_->close(PlaybackError::none); }
PlaybackSnapshot PlaybackRuntime::snapshot() const noexcept {
    const auto &p = *impl_;
    PlaybackSnapshot value;
    value.prepared = p.plan != nullptr;
    value.live = p.live;
    value.lostLiveStarts = p.lostStarts.load(std::memory_order_relaxed);
    value.liveAcknowledgedId = p.liveAcknowledged.load(std::memory_order_acquire);
    value.liveTriggeredNotes = p.liveTriggered.load(std::memory_order_acquire);
    value.preparationId = p.preparationId;
    if (p.plan) {
        value.sampleRate = p.plan->settings().sampleRate;
        value.totalFrames = p.plan->totalFrames();
        value.generatedNotes = p.plan->actions().size();
        value.suppressedSourceEvents = p.suppressedSourceEvents;
        value.omittedCompositionNotes = p.plan->omittedCompositionNotes();
        value.routedNotes = p.routedNotes;
        value.unassignedNotes = value.generatedNotes - p.routedNotes;
        value.wetActive = p.plan->spatialPlan().wetActive;
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
    value.callbackCount = p.callbackCount.load(std::memory_order_relaxed);
    value.callbackFrames = p.callbackFrames.load(std::memory_order_relaxed);
    value.faultCount = p.faultCount.load(std::memory_order_relaxed);
    value.outputPeakLeft = p.outputPeakLeft.load(std::memory_order_relaxed);
    value.outputPeakRight = p.outputPeakRight.load(std::memory_order_relaxed);
    if (value.enabled) {
        value.deviceSampleRate = p.deviceRate.load(std::memory_order_acquire);
        value.deviceBufferFrames =
            static_cast<int>(p.deviceBlock.load(std::memory_order_acquire));
        value.deviceOutputChannels =
            static_cast<int>(p.deviceChannels.load(std::memory_order_acquire));
    }
    return value;
}
const AudioDeviceConfiguration &
PlaybackRuntime::outputConfiguration() const noexcept {
    return impl_->configuration;
}
AudioDeviceCatalog PlaybackRuntime::audioDevices(bool refresh) {
    auto &p = *impl_;
    if (refresh) {
        service();
        AudioDeviceCatalog next;
        try {
            next = p.driver->audioDevices();
            if (next.devices.size() > 128)
                throw std::runtime_error(
                    "audio device catalog exceeds capacity");
            for (const auto &device : next.devices)
                if (device.id.empty() || device.id.size() > 64 ||
                    device.name.size() > 256 || device.type.size() > 256 ||
                    device.sampleRates.size() > 64 ||
                    device.bufferFrames.size() > 64)
                    throw std::runtime_error(
                        "audio device metadata exceeds capacity");
        } catch (...) {
            next = {};
            next.available = true;
            next.error =
                "Audio device discovery failed or exceeded supported capacity.";
        }
        if (p.catalogInitialized && (p.catalog.devices != next.devices ||
                                     p.catalog.available != next.available ||
                                     p.catalog.error != next.error))
            ++p.controlRevision;
        p.catalog = std::move(next);
        p.catalogInitialized = true;
    }
    auto result = p.catalog;
    result.configuration = p.configuration;
    result.controlRevision = p.controlRevision;
    const auto state = snapshot();
    result.active = state.enabled ? p.activeDevice : std::nullopt;
    if (result.error.empty() && state.error != PlaybackError::none)
        result.error = playbackErrorText(state.error);
    return result;
}
PlaybackResult PlaybackRuntime::setAudioDevice(
    AudioDeviceConfiguration configuration, std::uint64_t revision,
    std::uint64_t preparation, std::uint64_t generation) {
    auto &p = *impl_;
    const auto catalog = audioDevices();
    const auto current = snapshot();
    if (revision != p.controlRevision)
        return {false, PlaybackError::staleControlRevision, current};
    if (preparation != current.preparationId)
        return {false, PlaybackError::stalePreparation, current};
    if (generation != current.generation)
        return {false, PlaybackError::staleGeneration, current};
    const auto error =
        validateDevice(catalog, configuration, current.sampleRate);
    if (error != PlaybackError::none)
        return {false, error, current};
    p.close(PlaybackError::none);
    try {
        p.driver->configureOutput(configuration);
    } catch (...) {
        p.close(PlaybackError::deviceUnavailable);
        return {false, PlaybackError::deviceUnavailable, snapshot()};
    }
    p.configuration = std::move(configuration);
    ++p.controlRevision;
    return {true, PlaybackError::none, snapshot()};
}
PlaybackResult PlaybackRuntime::prepare(const Timeline &timeline,
                                        AudioSettings settings,
                                        std::span<const VoiceProfile> profiles,
                                        bool editableVoices) {
    impl_->close(PlaybackError::none);
    auto nextPlan = std::make_unique<AudioPlan>(timeline, settings, profiles,
                                                editableVoices);
    auto nextRenderer = std::make_unique<AudioRenderer>(*nextPlan);
    impl_->renderer = std::move(
        nextRenderer); // Destroy borrower before retiring the old plan.
    impl_->plan = std::move(nextPlan);
    impl_->suppressedSourceEvents = static_cast<std::size_t>(std::count_if(
        impl_->plan->decisions().begin(), impl_->plan->decisions().end(),
        [](const AudioDecision &decision) {
            return decision.reason == Admission::density;
        }));
    impl_->routedNotes = static_cast<std::size_t>(std::count_if(
        impl_->plan->actions().begin(), impl_->plan->actions().end(),
        [](const MusicalAction &action) {
            return action.route.regionSlot != 0;
        }));
    impl_->lastSubmittedId = 0;
    impl_->startRead.store(impl_->startWrite.load(std::memory_order_acquire),
                           std::memory_order_release);
    impl_->lostStarts.store(0, std::memory_order_relaxed);
    impl_->liveAcknowledged.store(0, std::memory_order_release);
    impl_->liveTriggered.store(0, std::memory_order_release);
    impl_->live = false;
    ++impl_->preparationId;
    impl_->publish();
    return {true, PlaybackError::none, snapshot()};
}
PlaybackResult PlaybackRuntime::prepareLive(AudioSettings settings) {
    VoiceSettings fallbackSettings;
    fallbackSettings.attackSeconds = settings.attackSeconds;
    fallbackSettings.decaySeconds = settings.decaySeconds;
    const std::array<VoiceProfile, 1> fallback{{{"", fallbackSettings}}};
    prepare(Timeline({}, TimeBasis::arrival), settings, fallback, true);
    impl_->live = true;
    impl_->renderer->setLive(true);
    return {true, PlaybackError::none, snapshot()};
}
PlaybackResult PlaybackRuntime::submitLive(const VoiceSettings &settings,
                                           unsigned midiNote, double velocity,
                                           std::uint32_t gateFrames,
                                           std::optional<Position> position,
                                           std::uint64_t token) {
    const auto current = snapshot();
    if (!current.live || !current.enabled || current.state != AudioState::playing)
        return {false, PlaybackError::liveNoteRejected, current};
    PlaybackCommand note;
    note.kind = PlaybackCommandKind::liveNote;
    note.preparationId = current.preparationId;
    note.generation = current.generation;
    note.midiNote = midiNote;
    note.liveToken = token;
    note.phaseStep = 2 * std::numbers::pi * 440 *
                     std::exp2((static_cast<double>(midiNote) - 69) / 12) /
                     current.sampleRate;
    note.velocity = velocity;
    note.gateFrames = gateFrames;
    note.route = prepareSpatialRoute(impl_->plan->settings().spatial, position);
    note.liveVoiceUpdate = true;
    if (!impl_->plan->prepareVoiceUpdate(0, settings, note.voiceCoefficients))
        return {false, PlaybackError::invalidVoiceSettings, current};
    auto result = command(note);
    if (!result.ok && result.error == PlaybackError::invalidTransition)
        result.error = PlaybackError::liveNoteRejected;
    return result;
}
std::size_t PlaybackRuntime::drainLiveStarts(
    std::span<LiveVoiceStart> destination) noexcept {
    auto &p = *impl_;
    const auto read = p.startRead.load(std::memory_order_relaxed);
    const auto available = p.startWrite.load(std::memory_order_acquire) - read;
    const auto size =
        std::min(destination.size(), static_cast<std::size_t>(available));
    for (std::size_t i = 0; i < size; ++i)
        destination[i] = p.voiceStarts[(read + i) % p.voiceStarts.size()];
    p.startRead.store(read + size, std::memory_order_release);
    return size;
}
LiveBatchResult
PlaybackRuntime::submitLiveBatch(std::span<const LiveNoteInput> notes) {
    auto &p = *impl_;
    const auto current = snapshot();
    if (!current.live || !current.enabled ||
        current.state != AudioState::playing)
        return {0, PlaybackError::liveNoteRejected};
    std::size_t accepted{};
    for (const auto &input : notes) {
        const auto write = p.liveWrite.load(std::memory_order_relaxed);
        if (write - p.liveRead.load(std::memory_order_acquire) >=
            Impl::liveCapacity)
            break;
        if (input.id <= p.lastSubmittedId ||
            input.midiNote < p.plan->settings().rootMidi ||
            input.midiNote > p.plan->settings().rootMidi + 16 ||
            !std::isfinite(input.velocity) || input.velocity <= 0 ||
            input.velocity > 1 || input.gateFrames > current.sampleRate * 4u)
            return {accepted, PlaybackError::liveNoteRejected};
        PlaybackCommand note;
        note.kind = PlaybackCommandKind::liveNote;
        note.frame = input.notBeforeFrame;
        note.preparationId = current.preparationId;
        note.generation = current.generation;
        note.midiNote = input.midiNote;
        note.liveToken = input.id;
        note.phaseStep =
            2 * std::numbers::pi * 440 *
            std::exp2((static_cast<double>(input.midiNote) - 69) / 12) /
            current.sampleRate;
        note.velocity = input.velocity;
        note.gateFrames = input.gateFrames;
        note.route =
            prepareSpatialRoute(p.plan->settings().spatial, input.position);
        note.liveVoiceUpdate = true;
        if (!p.plan->prepareVoiceUpdate(0, input.voice, note.voiceCoefficients))
            return {accepted, PlaybackError::invalidVoiceSettings};
        p.liveQueue[write % Impl::liveCapacity] = {note, input.id};
        p.lastSubmittedId = input.id;
        p.liveWrite.store(write + 1, std::memory_order_release);
        ++accepted;
    }
    return {accepted, PlaybackError::none};
}
PlaybackResult PlaybackRuntime::updateVoice(VoiceSlot slot,
                                            const VoiceSettings &settings) {
    auto &p = *impl_;
    service();
    const auto current = snapshot();
    if (!p.plan)
        return {false, PlaybackError::notPrepared, current};
    PlaybackCommand update;
    update.kind = PlaybackCommandKind::updateVoice;
    update.preparationId = current.preparationId;
    update.generation = current.generation;
    update.voiceSlot = slot;
    if (!p.plan->prepareVoiceUpdate(slot, settings, update.voiceCoefficients))
        return {false, PlaybackError::invalidVoiceSettings, current};
    return command(update);
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
    const auto catalog = audioDevices();
    if (catalog.available ||
        p.configuration.mode != AudioDeviceMode::systemDefault ||
        p.configuration.bufferFrames) {
        const auto error = validateDevice(catalog, p.configuration,
                                          p.plan->settings().sampleRate);
        if (error != PlaybackError::none) {
            p.close(error);
            return {false, error, snapshot()};
        }
    }
    p.close(PlaybackError::none);
    heartbeat();
    p.callbackHeartbeat = p.heartbeatSerial.load(std::memory_order_acquire);
    p.remainingLeaseFrames =
        static_cast<std::uint64_t>(p.plan->settings().sampleRate) * 5;
    p.enabled.store(true, std::memory_order_release);
    std::string deviceError;
    try {
        p.driver->configureOutput(p.configuration);
        deviceError = p.driver->start(*this, p.plan->settings().sampleRate);
        p.activeDevice = p.driver->activeDevice();
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
        channels < 2 || channels > OutputDriver::maxOutputChannels) {
        deviceFailed(PlaybackError::unsupportedDevice);
        return;
    }
    p.deviceRate.store(static_cast<unsigned>(rate), std::memory_order_release);
    p.deviceBlock.store(static_cast<unsigned>(frames),
                        std::memory_order_release);
    p.deviceChannels.store(static_cast<unsigned>(channels),
                           std::memory_order_release);
}
void PlaybackRuntime::deviceFailed(PlaybackError error) noexcept {
    unsigned expected = static_cast<unsigned>(PlaybackError::none);
    if (error != PlaybackError::none &&
        impl_->failure.compare_exchange_strong(
            expected, static_cast<unsigned>(error), std::memory_order_acq_rel))
        impl_->faultCount.fetch_add(1, std::memory_order_relaxed);
}
void PlaybackRuntime::process(float *const *outputs, int channels,
                              int frames) noexcept {
    auto &p = *impl_;
    if (!p.enabled.load(std::memory_order_acquire) ||
        p.error() != PlaybackError::none) {
        p.outputPeakLeft.store(0, std::memory_order_relaxed);
        p.outputPeakRight.store(0, std::memory_order_relaxed);
        p.peakHoldLeft = p.peakHoldRight = 0;
    }
    p.callbackCount.fetch_add(1, std::memory_order_relaxed);
    if (frames > 0)
        p.callbackFrames.fetch_add(static_cast<std::uint64_t>(frames),
                                   std::memory_order_relaxed);
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
    const auto blockStart = p.live ? Clock::now() : Clock::time_point{};
    const auto blockUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             blockStart.time_since_epoch())
                             .count();
    const auto firstWrite = p.startWrite.load(std::memory_order_relaxed);
    auto write = firstWrite;
    std::uint64_t lost = 0;
    const auto apply = [&](const PlaybackCommand &command, std::size_t offset) {
        LiveVoiceFrames frames;
        if (!p.apply(command, command.liveToken ? &frames : nullptr))
            return false;
        if (command.kind == PlaybackCommandKind::liveNote &&
            command.liveToken) {
            if (write - p.startRead.load(std::memory_order_acquire) <
                p.voiceStarts.size()) {
                const auto at = [&](std::size_t frame) {
                    return blockUs + static_cast<std::int64_t>(
                                         frame * std::uint64_t{1000000} /
                                         p.plan->settings().sampleRate);
                };
                p.voiceStarts[write++ % p.voiceStarts.size()] = {
                    command.liveToken, frames.endedToken, at(offset),
                    at(offset + frames.duration),
                    frames.endedToken ? at(offset + frames.shortenedDuration)
                                      : 0};
            } else {
                ++lost;
            }
        }
        return true;
    };
    if (hasCommand)
        p.accepted.store(apply(p.pending, 0), std::memory_order_relaxed);
    const auto count = static_cast<std::size_t>(frames);
    bool rendered = true;
    std::uint64_t lastRenderedOnset{}, renderedOnsets{};
    std::size_t offset{};
    while (offset < count) {
        std::uint64_t onsetId{};
        const auto read = p.liveRead.load(std::memory_order_relaxed);
        const bool queued =
            read != p.liveWrite.load(std::memory_order_acquire) &&
            p.renderer->state() == AudioState::playing;
        if (queued)
            p.nextLiveFrame =
                std::max(p.nextLiveFrame,
                         p.liveQueue[read % Impl::liveCapacity].command.frame);
        if (queued && p.renderer->position() >= p.nextLiveFrame) {
            const auto &note = p.liveQueue[read % Impl::liveCapacity];
            if (apply(note.command, offset)) {
                onsetId = note.id;
                p.liveRead.store(read + 1, std::memory_order_release);
            }
            // Minimum 10 ms between starts prevents same-sample stealing and
            // gives every admitted voice audible frames before it can be
            // stolen.
            p.nextLiveFrame = p.renderer->position() +
                              std::max(1u, p.plan->settings().sampleRate / 100);
        }
        auto chunk = count - offset;
        if (queued && p.nextLiveFrame > p.renderer->position())
            chunk =
                std::min(chunk, static_cast<std::size_t>(
                                    p.nextLiveFrame - p.renderer->position()));
        if (!p.renderer->process(
                std::span<float>(p.scratch.data() + offset * 2, chunk * 2))) {
            rendered = false;
            break;
        }
        if (onsetId) {
            ++renderedOnsets;
            lastRenderedOnset = onsetId;
        }
        offset += chunk;
    }
    if (!rendered) {
        deviceFailed(PlaybackError::callbackFailure);
    } else if (p.error() == PlaybackError::none) {
        float peakLeft{}, peakRight{};
        for (std::size_t frame = 0; frame < count; ++frame) {
            outputs[0][frame] = p.scratch[frame * 2];
            outputs[1][frame] = p.scratch[frame * 2 + 1];
            peakLeft = std::max(peakLeft, std::abs(outputs[0][frame]));
            peakRight = std::max(peakRight, std::abs(outputs[1][frame]));
        }
        // Publish onsets only after their complete callback buffer reached
        // output.
        if (write != firstWrite)
            p.startWrite.store(write, std::memory_order_release);
        if (lost)
            p.lostStarts.fetch_add(lost, std::memory_order_relaxed);
        if (renderedOnsets) {
            p.liveTriggered.fetch_add(renderedOnsets,
                                      std::memory_order_relaxed);
            p.liveAcknowledged.store(lastRenderedOnset,
                                     std::memory_order_release);
        }
        // Hold transients for slow UI polling, then decay with a one-second
        // time constant. These values measure PCM actually copied to output.
        const auto updatePeak = [&](std::atomic<float> &meter,
                                    std::uint64_t &hold, float peak) {
            const auto previous = meter.load(std::memory_order_relaxed);
            const bool silentTransport = p.renderer->state() == AudioState::ready ||
                p.renderer->state() == AudioState::paused ||
                p.renderer->state() == AudioState::finished;
            if (silentTransport) {
                hold = 0;
                meter.store(peak, std::memory_order_relaxed);
            } else if (peak >= previous) {
                hold = p.plan->settings().sampleRate;
                meter.store(peak, std::memory_order_relaxed);
            } else {
                const auto decayFrames = blockFrames > hold ? blockFrames - hold : 0;
                hold = blockFrames > hold ? 0 : hold - blockFrames;
                const auto decayed = previous * std::exp(-static_cast<double>(decayFrames) /
                                                        p.plan->settings().sampleRate);
                meter.store(static_cast<float>(std::max(static_cast<double>(peak), decayed)),
                            std::memory_order_relaxed);
            }
        };
        updatePeak(p.outputPeakLeft, p.peakHoldLeft, peakLeft);
        updatePeak(p.outputPeakRight, p.peakHoldRight, peakRight);
    }
    p.publish();
    if (hasCommand)
        p.acknowledged.store(request, std::memory_order_release);
}
} // namespace atlas
