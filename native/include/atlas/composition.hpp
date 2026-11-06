#pragma once
#include <atlas/core.hpp>

namespace atlas {
enum class CompositionScale {
    majorPentatonic,
    minorPentatonic,
    major,
    naturalMinor
};
enum class CompositionDensity { sparse, balanced, active };
struct CompositionSettings {
    bool enabled{};
    CompositionScale scale{CompositionScale::majorPentatonic};
    unsigned tonicPitchClass{};
    unsigned tempoBpm{90};
    CompositionDensity density{CompositionDensity::balanced};
    double variation{.25};
    std::uint32_t seed{1};
    bool quantize{true};
};
bool validCompositionSettings(const CompositionSettings &) noexcept;
bool compositionScaleContains(const CompositionSettings &,
                              int midiNote) noexcept;
inline constexpr std::size_t compositionActionCapacity = 200000;
struct CompositionNote {
    Microseconds dueUs{}, gateUs{};
    std::size_t eventIndex{};
    unsigned midiNote{};
    double velocity{1};
    std::uint32_t phaseSeed{};
};
struct CompositionEventDecision {
    std::size_t eventIndex{};
    bool admitted{};
};
struct CompiledComposition {
    std::vector<CompositionNote> notes;
    std::vector<CompositionEventDecision> decisions;
    std::size_t omittedNotes{};
};
// Worker-only. Authored musical phrases retain source indices; no animal
// observations are generated. At most four notes per source event.
CompiledComposition compileComposition(const Timeline &,
                                       const CompositionSettings &,
                                       unsigned rootMidi);
} // namespace atlas
