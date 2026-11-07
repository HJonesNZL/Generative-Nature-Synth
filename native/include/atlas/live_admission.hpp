#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct sqlite3;
namespace atlas {
struct LiveBatch {
    std::vector<std::string> payloads;
    bool storeReset{};
    std::uint64_t received{}, dropped{}, skippedRevisions{};
};
// Serial UI worker, borrowed read-only connection. Each poll examines at most
// 512 revisions and admits at most eight current, newly live source records.
// Start/reconnect baselines at the committed head; browser cursors never enter.
class LiveAdmission {
  public:
    explicit LiveAdmission(sqlite3 *db) : db_(db) {}
    bool
    baseline(); // True when a previously attached store was replaced/reset.
    LiveBatch poll();

  private:
    sqlite3 *db_{};
    std::string store_;
    std::int64_t cursor_{};
};
} // namespace atlas
