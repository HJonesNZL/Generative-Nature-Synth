#pragma once
#include <atlas/core.hpp>
#include <filesystem>
#include <memory>

namespace atlas {
struct SavedSession {
    std::string id, name, state, origin, contentHash, metadataIntegrity;
    std::size_t eventCount{};
    bool truncated{};
    std::optional<std::pair<Microseconds, Microseconds>> observedBounds, arrivalBounds;
};

// Read-only compatibility adapter for the existing schema-v2/v3 database.
// Collector/session writes are not migrated yet. No call belongs on an audio thread.
// Confine each instance to one worker; use separate connections for concurrent loads.
class ReadOnlyLibrary {
public:
    explicit ReadOnlyLibrary(const std::filesystem::path& path);
    ~ReadOnlyLibrary();
    ReadOnlyLibrary(const ReadOnlyLibrary&) = delete;
    ReadOnlyLibrary& operator=(const ReadOnlyLibrary&) = delete;
    std::vector<SavedSession> sessions(std::size_t limit = 100) const;
    SavedSession session(std::string_view id) const;
    Timeline load(std::string_view id, TimeBasis basis, Speed speed = {}, SavedSession* verifiedSession = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
