#pragma once
#include <atlas/audio.hpp>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <stop_token>

namespace atlas {
class RenderCancelled : public std::runtime_error {
  public:
    RenderCancelled() : std::runtime_error("Offline render cancelled") {}
};
struct RenderControl {
    std::stop_token stop;
    std::atomic<std::uint64_t> *framesWritten{}, *totalFrames{};
};
// Worker-only offline adapter. Publishes only complete files; never overwrites
// an existing path.
AudioStatistics renderWave(const AudioPlan &,
                           const std::filesystem::path &output,
                           std::string_view recipeJson,
                           std::uint32_t blockFrames = 512, RenderControl = {});
} // namespace atlas
