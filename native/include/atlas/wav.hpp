#pragma once
#include <atlas/audio.hpp>
#include <filesystem>

namespace atlas {
// Worker-only offline adapter. Publishes only complete files; never overwrites an existing path.
AudioStatistics renderWave(const AudioPlan&, const std::filesystem::path& output,
                           std::string_view recipeJson, std::uint32_t blockFrames = 512);
} // namespace atlas
