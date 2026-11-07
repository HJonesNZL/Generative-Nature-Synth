#include "event_validation.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <atlas/core.hpp>
#include <atlas/live_admission.hpp>
#include <stdexcept>
namespace atlas {
using detail::Statement;
namespace {
class Snapshot {
  public:
    explicit Snapshot(sqlite3 *db) : db_(db) {
        if (sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error("Live read snapshot unavailable");
    }
    ~Snapshot() { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }

  private:
    sqlite3 *db_;
};
} // namespace

bool LiveAdmission::baseline() {
    const Snapshot snapshot(db_);
    Statement head(db_, "SELECT value,(SELECT COALESCE(MAX(seq),0) FROM "
                        "revisions) FROM metadata WHERE key='store_id'");
    if (!head.next())
        throw std::runtime_error("Live library identity unavailable");
    const bool reset = !store_.empty() &&
                       (store_ != head.text(0) || head.integer(1) < cursor_);
    store_ = head.text(0);
    cursor_ = head.integer(1);
    return reset;
}
LiveBatch LiveAdmission::poll(std::size_t limit) {
    limit = std::min<std::size_t>(limit, 512);
    const Snapshot snapshot(db_);
    Statement head(db_, "SELECT value,(SELECT COALESCE(MAX(seq),0) FROM "
                        "revisions) FROM metadata WHERE key='store_id'");
    if (!head.next())
        throw std::runtime_error("Live library identity unavailable");
    const auto high = head.integer(1);
    if (head.text(0) != store_ || high < cursor_) {
        store_ = head.text(0);
        cursor_ = high;
        LiveBatch reset;
        reset.storeReset = true;
        return reset;
    }
    // Bounded newest window: old bursts are discarded, never played later.
    const auto floor = std::max(cursor_, high - 512);
    Statement rows(
        db_,
        ("SELECT "
         "r.seq,r.event_json,r.change_kind,r.live_eligible,e.revision_seq,r."
         "event_id,r.source_id,r.observed_at,r.first_seen_at,r.ingested_at "
         "FROM revisions r LEFT JOIN events e ON e.id=r.event_id WHERE r.seq>" +
         std::to_string(floor) + " AND r.seq<=" + std::to_string(high) +
         " ORDER BY r.seq")
            .c_str());
    LiveBatch result;
    while (rows.next()) {
        if (rows.text(2) != "new" || rows.integer(3) != 1 || rows.null(4) ||
            rows.integer(4) != rows.integer(0))
            continue;
        ++result.received;
        const auto payload = rows.text(1);
        try {
            detail::validateStoredEvent(db_, payload);
            Statement identity(
                db_, "SELECT "
                     "json_extract(?1,'$.provenance_mode'),json_"
                     "extract(?1,'$.event_id'),json_extract(?1,'$.source_id'),"
                     "json_extract(?1,'$.observed_at_utc'),json_extract(?1,'$."
                     "first_seen_at_utc'),"
                     "json_extract(?1,'$.ingested_at_utc')");
            identity.bindJson(payload);
            if (!identity.next() || identity.text(0) != "live_source" ||
                identity.text(1) != rows.text(5) ||
                identity.text(2) != rows.text(6) ||
                parseUtc(identity.text(3)) != parseUtc(rows.text(7)) ||
                parseUtc(identity.text(4)) != parseUtc(rows.text(8)) ||
                parseUtc(identity.text(5)) != parseUtc(rows.text(9))) {
                ++result.dropped;
                continue;
            }
        } catch (const std::invalid_argument &) {
            ++result.dropped;
            continue;
        }
        if (result.payloads.size() == limit) {
            ++result.dropped;
            continue;
        }
        result.payloads.push_back(payload);
    }
    result.skippedRevisions = static_cast<std::uint64_t>(floor - cursor_);
    cursor_ = high;
    return result;
}
} // namespace atlas
