#pragma once
#include <atlas/core.hpp>
#include <array>

namespace atlas {
struct AudioSettings {
    std::uint32_t sampleRate{48000};
    unsigned rootMidi{60};
    double attackSeconds{0.008}, decaySeconds{0.650}, masterDb{};
};
enum class Admission { accepted, density };
struct AudioDecision { std::size_t eventIndex{}; Admission reason{}; };
struct MusicalAction {
    std::uint64_t frame{};
    double phaseStep{};
    std::size_t eventIndex{};
    unsigned midiNote{};
};

// Worker-owned immutable numeric plan. Do not move/destroy while a renderer borrows it.
class AudioPlan {
public:
    explicit AudioPlan(const Timeline&, AudioSettings = {});
    const AudioSettings& settings() const noexcept { return settings_; }
    std::span<const MusicalAction> actions() const noexcept { return actions_; }
    std::span<const AudioDecision> decisions() const noexcept { return decisions_; }
    std::uint64_t totalFrames() const noexcept { return totalFrames_; }
    std::size_t missingSpecies() const noexcept { return missingSpecies_; }
    std::uint32_t voiceFrames() const noexcept { return attackFrames_ + decayFrames_ + releaseFrames_; }
private:
    friend class AudioRenderer;
    AudioSettings settings_;
    std::vector<MusicalAction> actions_;
    std::vector<AudioDecision> decisions_;
    std::uint64_t totalFrames_{};
    std::size_t missingSpecies_{};
    std::uint32_t attackFrames_{}, decayFrames_{}, releaseFrames_{}, fadeFrames_{};
    double decayFactor_{}, masterGain_{};
};

enum class AudioState { ready, playing, pausing, paused, draining, finished };
struct AudioStatistics {
    std::uint64_t triggered{}, stolen{}, faults{};
    double peak{};
};

// One owner, controls between process calls. No owning plan reference on the audio path.
class AudioRenderer {
public:
    static constexpr std::size_t voiceCapacity = 8, maxBlockFrames = 4096;
    explicit AudioRenderer(const AudioPlan&) noexcept;
    AudioRenderer(AudioPlan&&) = delete;
    AudioRenderer(const AudioRenderer&) = delete;
    AudioRenderer& operator=(const AudioRenderer&) = delete;
    bool play(std::uint64_t generation) noexcept;
    bool pause(std::uint64_t generation) noexcept;
    bool stop(std::uint64_t generation) noexcept;
    bool seek(std::uint64_t frame, std::uint64_t generation) noexcept;
    bool panic(std::uint64_t generation) noexcept;
    bool process(std::span<float> interleavedStereo) noexcept;
    AudioState state() const noexcept { return state_; }
    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t position() const noexcept { return frame_; }
    const AudioStatistics& statistics() const noexcept { return statistics_; }
private:
    struct Voice {
        bool active{};
        std::uint64_t started{};
        std::uint32_t age{}, fadeAge{};
        double phase{}, phaseStep{}, decay{1};
    };
    const AudioPlan& plan_;
    std::array<Voice, voiceCapacity> voices_{}, retiring_{};
    std::size_t nextAction_{};
    std::uint64_t frame_{}, generation_{1};
    AudioState state_{AudioState::ready};
    AudioStatistics statistics_;
    double gain_{1}, rampFrom_{1}, rampTo_{1};
    std::uint32_t rampRemaining_{};
    void clearVoices() noexcept;
    void clearToStart() noexcept;
    void beginRamp(double target) noexcept;
    void trigger(const MusicalAction&) noexcept;
    double tick(Voice&, bool retiring) noexcept;
};
} // namespace atlas
