#pragma once
#include <atlas/core.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace atlas {
struct SavedSession {
    std::string id, name, state, origin, contentHash, metadataIntegrity, observedWindowPolicy;
    std::size_t eventCount{};
    bool truncated{};
    std::optional<std::pair<Microseconds, Microseconds>> observedBounds, arrivalBounds;
};

struct LibraryRevision {
    std::string sourceId, revision;
    bool operator==(const LibraryRevision&) const = default;
};
struct SessionPage {
    std::vector<SavedSession> sessions;
    LibraryRevision library;
    std::optional<std::string> nextCursor;
};
class StaleLibraryCursor : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class LibraryOpenFailure { missing, unreadable, incompatible };
class LibraryOpenError : public std::runtime_error {
public:
    LibraryOpenError(LibraryOpenFailure failure, const std::string& message)
        : std::runtime_error(message), failure_(failure) {}
    LibraryOpenFailure failure() const noexcept { return failure_; }
private:
    LibraryOpenFailure failure_;
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
    // Revisions and opaque cursors belong to this connection. A commit by another
    // connection invalidates continuation; reopening/switching invalidates all cursors.
    LibraryRevision revision() const;
    // 1..100 records ordered by created_at DESC, id ASC. No total count scan.
    // Bounded cursors (2048 bytes) fail observably when malformed or stale.
    SessionPage sessionPage(std::size_t limit = 100, std::string_view cursor = {}) const;
    SavedSession session(std::string_view id) const;
    Timeline load(std::string_view id, TimeBasis basis, Speed speed = {}, SavedSession* verifiedSession = nullptr) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
