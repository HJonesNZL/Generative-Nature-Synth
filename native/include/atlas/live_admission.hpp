#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
struct sqlite3;
namespace atlas {
struct LiveSmoothTiming {
    std::string sourceId;
    std::int64_t ingestedUs{}, offsetUs{};
};
struct LiveBatch {
    std::vector<std::string> payloads;
    std::vector<LiveSmoothTiming> smoothTiming;
    bool storeReset{};
    bool hasMore{};
    std::uint64_t received{}, dropped{}, skippedRevisions{};
};
// Serial UI worker, borrowed read-only connection. Each poll examines at most
// 512 revisions and admits at most the requested limit (default eight).
// Start/reconnect baselines at the committed head; browser cursors never enter.
class LiveAdmission {
  public:
    explicit LiveAdmission(sqlite3 *db) : db_(db) {}
    bool
    baseline(); // True when a previously attached store was replaced/reset.
    const std::string &storeId() const noexcept { return store_; }
    LiveBatch poll(std::size_t limit = 8);
    // FIFO backpressure: examine at most 512 revisions, retaining unconsumed
    // events in SQLite. A zero capacity leaves the cursor unchanged.
    LiveBatch pollFifo(std::size_t limit = 128,
                       std::int64_t collectionIntervalUs = 0);

  private:
    struct TimingGroup {
        std::string source, ingested;
        bool repeatedObservations{};
        int observationDirection{};
        std::int64_t high{}, count{}, position{}, minimumObserved{}, maximumObserved{}, horizonUs{}, previousOffset{};
    };
    LiveSmoothTiming timing(const std::string &source, const std::string &ingested,
                            const std::string &observed, std::int64_t seq,
                            std::int64_t high, std::int64_t intervalUs);
    LiveBatch pollImpl(std::size_t limit, bool fifo, std::int64_t intervalUs = 0);
    std::deque<TimingGroup> timingGroups_;
    std::int64_t baselineCursor_{};
    sqlite3 *db_{};
    std::string store_;
    std::int64_t cursor_{};
};
} // namespace atlas
