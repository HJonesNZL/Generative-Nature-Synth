#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace atlas {
struct CollectionRequest {
    std::string url, body;
};
class CollectionFetchError : public std::runtime_error {
  public:
    explicit CollectionFetchError(std::string safeCode,
                                  int retryAfterSeconds = 0);
    int retryAfterSeconds() const noexcept;

  private:
    int retryAfterSeconds_{};
};
struct CollectionResult {
    std::size_t inserted{}, corrected{}, duplicates{}, invalidRecords{},
        deferredFuture{};
    bool complete{};
};
struct CollectionSourceState {
    std::string id, lastAttemptUtc, lastSuccessUtc, lastObservationUtc, error;
    std::uint64_t records{};
    unsigned retryDelaySeconds{}, consecutiveErrors{};
    bool complete{}, gap{};
};
// Synchronous worker-only owner. Its path must be a NEW managed library or one
// previously created by this class; existing unmarked wildlife libraries are
// rejected before a writable connection opens. Confine each instance to one
// worker and let the app own its interprocess/scheduler lifetime.
class Collector {
  public:
    using Fetch = std::function<std::string(const CollectionRequest &)>;
    explicit Collector(const std::filesystem::path &databasePath);
    ~Collector();
    Collector(const Collector &) = delete;
    Collector &operator=(const Collector &) = delete;
    // Source IDs: birdweather (NZ), birdweather_world (four regional
    // requests per minute), nzsharks, and inaturalist. World event provenance remains
    // birdweather; per-region checkpoints are persisted independently. Fetch must honor cancellation,
    // finite timeouts, redirect rejection and the 4 MiB response limit.
    // Fetch/schema failures persist redacted source state then throw.
    CollectionResult collect(std::string_view sourceId, std::string_view nowUtc,
                             const Fetch &);
    std::vector<CollectionSourceState> sourceStates() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
