#pragma once
#include <atlas/collection.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace atlas::standalone {
// Created on the app worker; transfers the managed Collector to one dedicated
// thread. Starts disabled. configure/stateJson never access its database.
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
    const std::filesystem::path &databasePath() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas::standalone
