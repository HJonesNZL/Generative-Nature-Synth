#pragma once
#include <atlas/collection.hpp>
#include <atlas/live_admission.hpp>
#include <optional>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace atlas::standalone {
// Created on the app worker; transfers the managed Collector to one dedicated
// thread. Starts disabled. configure captures the start frontier through a
// separate read-only snapshot; stateJson never accesses SQLite.
class CollectionService {
  public:
    explicit CollectionService(const std::filesystem::path &managedDatabase,
                               Collector::Fetch fetch = {});
    ~CollectionService();
    CollectionService(const CollectionService &) = delete;
    CollectionService &operator=(const CollectionService &) = delete;
    static void validateSelection(const std::vector<std::string>& sourceIds);
    // Enabling respects the last attempt's minimum
    // interval; disabling cancels HTTP and discards a response before
    // ingestion.
    void configure(const std::vector<std::string> &sourceIds);
    // Records are decimal strings; absent timestamps/errors are JSON null.
    std::string stateJson() const;
    std::optional<LiveSessionBoundary> session() const;
    // App worker: read-only identity check; a changed/privacy-reset library
    // visibly stops collection rather than leaving a frozen session active.
    bool verifySession();
    const std::filesystem::path &databasePath() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas::standalone
