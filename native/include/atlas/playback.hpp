#pragma once
#include <atlas/audio.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace atlas {
class PlaybackRuntime;
enum class AudioDeviceMode { systemDefault, explicitDevice };
struct AudioDeviceConfiguration {
    AudioDeviceMode mode{AudioDeviceMode::systemDefault};
    std::string deviceId;
    int bufferFrames{};
    bool operator==(const AudioDeviceConfiguration &) const = default;
};
struct AudioDeviceInfo {
    std::string id, name, type;
    bool isDefault{};
    int outputChannels{};
    std::vector<std::uint32_t> sampleRates;
    std::vector<int> bufferFrames;
    int defaultBufferFrames{};
    bool operator==(const AudioDeviceInfo &) const = default;
};
struct ActiveAudioDevice {
    std::string deviceId, name, type;
    std::uint32_t sampleRate{};
    int bufferFrames{}, outputChannels{};
};
struct AudioDeviceCatalog {
    bool available{};
    std::vector<AudioDeviceInfo> devices;
    AudioDeviceConfiguration configuration;
    std::optional<ActiveAudioDevice> active;
    std::uint64_t controlRevision{1};
    std::string error;
};
// start/stop run on the serial control worker. stop must establish callback
// quiescence.
class OutputDriver {
  public:
    static constexpr int maxCallbackFrames = 65536, maxOutputChannels = 64;
    virtual ~OutputDriver() = default;
    virtual std::string start(PlaybackRuntime &, std::uint32_t sampleRate) = 0;
    virtual void stop() noexcept = 0;
    virtual AudioDeviceCatalog audioDevices() { return {}; }
    virtual void configureOutput(const AudioDeviceConfiguration &) {}
    virtual std::optional<ActiveAudioDevice> activeDevice() { return {}; }
};
std::unique_ptr<OutputDriver> makeJuceOutputDriver();
enum class PlaybackError : unsigned {
    none,
    notPrepared,
    disabled,
    stalePreparation,
    staleGeneration,
    invalidTransition,
    deviceUnavailable,
    unsupportedDevice,
    deviceChanged,
    callbackFailure,
    commandTimeout,
    leaseExpired,
    invalidVoiceSettings,
    staleControlRevision,
    invalidDeviceConfiguration,
    liveNoteRejected
};
const char *playbackErrorText(PlaybackError) noexcept;
enum class PlaybackCommandKind : unsigned {
    play,
    pause,
    stop,
    seek,
    panic,
    updateVoice,
    liveNote
};
struct PlaybackCommand {
    PlaybackCommandKind kind{};
    std::uint64_t preparationId{}, generation{}, frame{};
    VoiceSlot voiceSlot{};
    VoiceCoefficients voiceCoefficients{};
    unsigned midiNote{};
    double velocity{1}, phaseStep{};
    std::uint32_t gateFrames{};
    bool liveVoiceUpdate{};
};
struct PlaybackSnapshot {
    bool prepared{}, enabled{}, live{};
    std::uint64_t preparationId{}, generation{}, positionFrames{},
        totalFrames{};
    std::uint32_t sampleRate{};
    std::size_t generatedNotes{}, omittedCompositionNotes{},
        suppressedSourceEvents{};
    std::size_t routedNotes{}, unassignedNotes{};
    bool wetActive{};
    AudioState state{AudioState::ready};
    PlaybackError error{PlaybackError::none};
    double gainDb{-18};
    std::uint64_t callbackCount{}, callbackFrames{}, faultCount{};
    std::uint32_t deviceSampleRate{};
    int deviceBufferFrames{}, deviceOutputChannels{};
};
struct PlaybackResult {
    bool ok{};
    PlaybackError error{PlaybackError::none};
    PlaybackSnapshot snapshot;
};
// Exactly one serial control worker; exactly one device callback, never
// concurrently with stop.
class PlaybackRuntime {
  public:
    explicit PlaybackRuntime(
        std::unique_ptr<OutputDriver>,
        AudioDeviceConfiguration initialConfiguration = {});
    ~PlaybackRuntime();
    PlaybackRuntime(const PlaybackRuntime &) = delete;
    PlaybackRuntime &operator=(const PlaybackRuntime &) = delete;
    PlaybackResult prepare(const Timeline &,
                           AudioSettings = {48000, 60, .008, .650, -18},
                           std::span<const VoiceProfile> = {},
                           bool editableVoices = false);
    // Serial worker API. Live mode has no historical transport end; event
    // provenance/deduplication stay with the host. Each onset is acknowledged
    // through the existing bounded mailbox; no queued releases are required.
    PlaybackResult prepareLive(AudioSettings = {48000, 60, .008, .650, -18});
    PlaybackResult submitLive(const VoiceSettings &, unsigned midiNote,
                              double velocity = 1, std::uint32_t gateFrames = 0);
    PlaybackResult updateVoice(VoiceSlot, const VoiceSettings &);
    PlaybackResult enable(bool);
    AudioDeviceCatalog audioDevices(bool refresh = true);
    const AudioDeviceConfiguration &outputConfiguration() const noexcept;
    PlaybackResult setAudioDevice(AudioDeviceConfiguration,
                                  std::uint64_t controlRevision,
                                  std::uint64_t preparationId,
                                  std::uint64_t generation);
    PlaybackResult command(PlaybackCommand);
    PlaybackSnapshot snapshot() const noexcept;
    void heartbeat() noexcept;
    void service(); // Worker calls every 100 ms; output expires after 5 seconds
                    // without heartbeat.
    // Driver must supply valid extents within OutputDriver hard limits;
    // oversized renderer blocks silence.
    void process(float *const *outputs, int channels, int frames) noexcept;
    void deviceStarted(double sampleRate, int blockFrames,
                       int outputChannels) noexcept;
    void deviceFailed(PlaybackError = PlaybackError::deviceChanged) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
