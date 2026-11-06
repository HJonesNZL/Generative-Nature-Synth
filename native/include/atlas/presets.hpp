#pragma once
#include <atlas/audio.hpp>
#include <span>
#include <string_view>

namespace atlas {
// Immutable research candidates for new unsaved species defaults or explicit
// draft application. This catalogue never saves or claims listening approval.
struct SpeciesPreset {
    std::string_view id, speciesId, name, scientificName, description,
        evidenceStatus, researchPath;
    VoiceSettings settings;
};
std::span<const SpeciesPreset> speciesPresets() noexcept;
const SpeciesPreset *speciesPreset(std::string_view id) noexcept;
} // namespace atlas
