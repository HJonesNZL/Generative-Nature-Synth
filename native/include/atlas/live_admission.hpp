#pragma once
#include <cstddef>
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
// 512 revisions and admits at most the requested limit (default eight).
// Start/reconnect baselines at the committed head; browser cursors never enter.
class LiveAdmission {
  public:
    explicit LiveAdmission(sqlite3 *db) : db_(db) {}
    bool
    baseline(); // True when a previously attached store was replaced/reset.
    const std::string &storeId() const noexcept { return store_; }
    LiveBatch poll(std::size_t limit = 8);

  private:
    sqlite3 *db_{};
    std::string store_;
    std::int64_t cursor_{};
};
} // namespace atlas
