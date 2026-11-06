#pragma once
#include <array>
#include <atlas/composition.hpp>
#include <atlas/core.hpp>
#include <atlas/spatial.hpp>

namespace atlas {
using VoiceSlot = std::uint16_t;
inline constexpr std::size_t voiceSlotCapacity = 513;
enum class VoiceCharacter { struck, whistle, reed, bell, air, pulse };
struct VoiceSettings {
    int tuningSemitones{};
    double attackSeconds{.008}, decaySeconds{.650}, gainDb{};
    VoiceCharacter character{VoiceCharacter::struck};
    double brightness{.5}, roughness{}, pulseHz{}, sweepSemitones{};
};
bool validVoiceSettings(const VoiceSettings &) noexcept;
struct VoiceProfile {
    std::string speciesId;
    VoiceSettings settings;
};
inline constexpr std::size_t voicePartialCapacity = 8;
struct VoicePartial {
    double ratio{}, amplitude{}, decayFactor{1};
};
// Numeric, prepared off-thread. Pitch multiplies each action's mapped pitch.
// New settings are captured at onset; no active-voice parameter mutation.
struct VoiceCoefficients {
    std::uint32_t attackFrames{}, decayFrames{}, releaseFrames{};
    double pitchMultiplier{1}, decayFactor{}, gain{1};
    std::array<VoicePartial, voicePartialCapacity> partials{};
    std::uint32_t sweepFrames{};
    double sweepFactor{1}, sweepEnd{1}, pulsePhaseStep{}, roughnessPhaseStep{},
        roughnessDepth{}, phaseSpread{};
    std::uint8_t partialCount{};
    bool legacyStruck{true};
    std::array<double, 12> scalePitchMultipliers{};
    bool scaleQuantized{};
};
struct AudioSettings {
    std::uint32_t sampleRate{48000};
    unsigned rootMidi{60};
    double attackSeconds{0.008}, decaySeconds{0.650}, masterDb{};
    CompositionSettings composition{};
    SpatialSettings spatial{};
};
enum class Admission { accepted, density };
struct AudioDecision {
    std::size_t eventIndex{};
    Admission reason{};
};
struct MusicalAction {
    std::uint64_t frame{};
    double phaseStep{};
    std::size_t eventIndex{};
    unsigned midiNote{};
    VoiceSlot voiceSlot{};
    std::uint32_t phaseSeed{};
    double velocity{1};
    std::uint32_t gateFrames{};
    SpatialRoute route{};
};

// Worker-owned immutable numeric plan. Do not move/destroy while a renderer
// borrows it.
class AudioPlan {
  public:
    explicit AudioPlan(const Timeline &, AudioSettings = {},
                       std::span<const VoiceProfile> = {},
                       bool editableVoices = false);
    const AudioSettings &settings() const noexcept { return settings_; }
    std::span<const MusicalAction> actions() const noexcept { return actions_; }
    std::span<const AudioDecision> decisions() const noexcept {
        return decisions_;
    }
    std::uint64_t totalFrames() const noexcept { return totalFrames_; }
    std::size_t missingSpecies() const noexcept { return missingSpecies_; }
    std::size_t omittedCompositionNotes() const noexcept {
        return omittedCompositionNotes_;
    }
    std::uint32_t voiceFrames() const noexcept {
        return attackFrames_ + decayFrames_ + releaseFrames_;
    }
    std::span<const VoiceProfile> voiceProfiles() const noexcept {
        return voiceProfiles_;
    }
    VoiceSlot voiceSlot(std::string_view speciesId) const noexcept;
    bool editableVoices() const noexcept { return editableVoices_; }
    const SpatialPlan &spatialPlan() const noexcept { return spatialPlan_; }
    // Worker-only validation and coefficient preparation, before publication.
    bool prepareVoiceUpdate(VoiceSlot, const VoiceSettings &,
                            VoiceCoefficients &) const noexcept;

  private:
    friend class AudioRenderer;
    AudioSettings settings_;
    std::vector<MusicalAction> actions_;
    std::vector<AudioDecision> decisions_;
    std::uint64_t totalFrames_{};
    std::size_t missingSpecies_{}, omittedCompositionNotes_{};
    std::uint32_t attackFrames_{}, decayFrames_{}, releaseFrames_{},
        fadeFrames_{};
    double decayFactor_{}, masterGain_{}, maximumPhaseStep_{},
        minimumPitchMultiplier_{}, maximumPitchMultiplier_{};
    std::vector<VoiceProfile> voiceProfiles_;
    std::array<double, 12> maximumScalePitchMultipliers_{};
    std::array<VoiceCoefficients, voiceSlotCapacity> voiceBank_{};
    bool editableVoices_{};
    SpatialPlan spatialPlan_;
    std::uint32_t spatialEndFadeFrames_{};
};

enum class AudioState { ready, playing, pausing, paused, draining, finished };
struct AudioStatistics {
    std::uint64_t triggered{}, stolen{}, faults{};
    double peak{};
};

// One owner, controls between process calls. No owning plan reference on the
// audio path.
class AudioRenderer {
  public:
    static constexpr std::size_t voiceCapacity = 8, maxBlockFrames = 4096;
    explicit AudioRenderer(const AudioPlan &);
    AudioRenderer(AudioPlan &&) = delete;
    AudioRenderer(const AudioRenderer &) = delete;
    AudioRenderer &operator=(const AudioRenderer &) = delete;
    bool play(std::uint64_t generation) noexcept;
    bool pause(std::uint64_t generation) noexcept;
    bool stop(std::uint64_t generation) noexcept;
    bool seek(std::uint64_t frame, std::uint64_t generation) noexcept;
    bool panic(std::uint64_t generation) noexcept;
    bool updateVoice(VoiceSlot, const VoiceCoefficients &) noexcept;
    bool process(std::span<float> interleavedStereo) noexcept;
    AudioState state() const noexcept { return state_; }
    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t position() const noexcept { return frame_; }
    const AudioStatistics &statistics() const noexcept { return statistics_; }

  private:
    struct Voice {
        bool active{};
        std::uint64_t started{};
        std::uint32_t age{}, fadeAge{}, releaseStart{}, endAge{};
        double velocity{1};
        SpatialRoute route{};
        double phase{}, phaseStep{}, decay{1};
        VoiceCoefficients coefficients;
        std::array<double, voicePartialCapacity> partialPhases{},
            partialGains{};
        double sweep{1}, pulsePhase{}, roughnessPhase{};
    };
    const AudioPlan &plan_;
    SpatialProcessor spatial_;
    std::array<Voice, voiceCapacity> voices_{}, retiring_{};
    std::array<VoiceCoefficients, voiceSlotCapacity> voiceBank_{};
    std::size_t nextAction_{};
    std::uint64_t frame_{}, generation_{1};
    AudioState state_{AudioState::ready};
    AudioStatistics statistics_;
    double gain_{1}, rampFrom_{1}, rampTo_{1};
    std::uint32_t rampRemaining_{};
    void clearVoices() noexcept;
    void clearToStart() noexcept;
    void beginRamp(double target) noexcept;
    void trigger(const MusicalAction &) noexcept;
    double tick(Voice &, bool retiring) noexcept;
};
} // namespace atlas
