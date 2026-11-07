#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace atlas {
struct ProjectSummary {
    std::size_t sceneCount{}, voiceCount{};
    std::uint64_t totalBytes{};
    std::vector<std::string> sceneNames;
};
enum class ProjectErrorCode {
    invalidArgument,
    notFound,
    alreadyExists,
    unsupportedSchema,
    corrupt,
    capacity,
    storage
};
class ProjectError : public std::runtime_error {
  public:
    ProjectError(ProjectErrorCode, std::string);
    ProjectErrorCode code() const noexcept { return code_; }

  private:
    ProjectErrorCode code_;
};
// Worker-only I/O. Includes sealed scene bytes and independently saved voices;
// drafts, settings, source libraries and rendered exports are excluded. An
// empty voicesPath supplies a valid empty saved-voice database; missing paths
// fail.
ProjectSummary
createProjectBackup(const std::filesystem::path &voicesPath,
                    const std::vector<std::filesystem::path> &scenePaths,
                    const std::filesystem::path &outputPath);
// Full byte hashes and every embedded scene/voice database are verified.
ProjectSummary inspectProjectBackup(const std::filesystem::path &backupPath);
// Publishes only an entirely new directory, including empty exports/. Never
// merges with or overwrites a destination, even an empty existing directory.
ProjectSummary
restoreProjectBackup(const std::filesystem::path &backupPath,
                     const std::filesystem::path &newStateDirectory);
// Fully verifies the copied sealed scene, preserving its exact file bytes.
void exportSoundscape(const std::filesystem::path &source,
                      const std::filesystem::path &destination);
} // namespace atlas
