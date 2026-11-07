#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace atlas {
struct ExportArtifact {
    std::string artifactId, sceneName, sceneHash, engineSourceSha256;
    std::uint64_t bytes{}, frames{}, durationUs{};
    std::uint32_t sampleRate{};
};
struct ExportHistory {
    std::vector<ExportArtifact> items;
    bool limited{};
    std::size_t unavailableFiles{}, incompleteRenders{};
};
// Worker-only read-only inventory of managed final WAVs. Verifies the writer's
// structure and recipe, seeking over PCM (no audio checksum claim). At most
// 2048 directory entries are scanned; up to 100 results, newest first. Missing
// directories are empty; invalid arguments and directory I/O failures throw.
// Managed corrupt/symlink files are counted as unavailable, never completed.
ExportHistory listExports(const std::filesystem::path &,
                          std::size_t limit = 100);
} // namespace atlas
