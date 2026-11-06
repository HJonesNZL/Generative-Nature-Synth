#pragma once
#include <atlas/audio.hpp>
#include <memory>
#include <string>

namespace atlas {
class PlaybackRuntime;
// start/stop run on the serial control worker. stop must establish callback
// quiescence.
class OutputDriver {
  public:
    static constexpr int maxCallbackFrames = 65536, maxOutputChannels = 64;
    virtual ~OutputDriver() = default;
    virtual std::string start(PlaybackRuntime &, std::uint32_t sampleRate) = 0;
    virtual void stop() noexcept = 0;
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
    invalidVoiceSettings
};
const char *playbackErrorText(PlaybackError) noexcept;
enum class PlaybackCommandKind : unsigned {
    play,
    pause,
    stop,
    seek,
    panic,
    updateVoice
};
struct PlaybackCommand {
    PlaybackCommandKind kind{};
    std::uint64_t preparationId{}, generation{}, frame{};
    VoiceSlot voiceSlot{};
    VoiceCoefficients voiceCoefficients{};
};
struct PlaybackSnapshot {
    bool prepared{}, enabled{};
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
    explicit PlaybackRuntime(std::unique_ptr<OutputDriver>);
    ~PlaybackRuntime();
    PlaybackRuntime(const PlaybackRuntime &) = delete;
    PlaybackRuntime &operator=(const PlaybackRuntime &) = delete;
    PlaybackResult prepare(const Timeline &,
                           AudioSettings = {48000, 60, .008, .650, -18},
                           std::span<const VoiceProfile> = {},
                           bool editableVoices = false);
    PlaybackResult updateVoice(VoiceSlot, const VoiceSettings &);
    PlaybackResult enable(bool);
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
