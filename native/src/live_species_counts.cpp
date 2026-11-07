#include "live_species_counts.hpp"
#include "event_validation.hpp"
#include "json_text.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <atlas/core.hpp>
#include <map>
#include <stdexcept>
namespace atlas {
std::string LiveSpeciesCounts::page(
    sqlite3 *db, const std::optional<LiveSessionBoundary> &session,
    std::string_view sessionId, std::size_t offset, std::size_t limit) {
    lastExamined_ = 0;
    if (sessionId.empty() || sessionId.size() > 512 || limit < 1 || limit > 250)
        throw std::invalid_argument("Invalid live species page");
    if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Live species snapshot unavailable");
    struct Snapshot {
        sqlite3 *db;
        ~Snapshot() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    } snapshot{db};
    detail::Statement head(db, "SELECT value,(SELECT COALESCE(MAX(seq),0) FROM "
                               "revisions) FROM metadata WHERE key='store_id'");
    if (!head.next())
        throw std::runtime_error("Live species frontier unavailable");
    const auto revision = head.integer(1);
    const auto prefix =
        "{\"sessionId\":" + detail::quoteJson(sessionId) +
        ",\"revision\":" + detail::quoteJson(std::to_string(revision));
    if (!session || session->id != sessionId ||
        session->storeId != head.text(0) ||
        revision < session->baselineRevision)
        return prefix +
               ",\"sessionActive\":false,\"species\":[],\"nextCursor\":null}";
    if (cachedSession_ != session->id || cachedRevision_ != revision) {
        const bool rebuild =
            cachedSession_ != session->id || revision < cachedRevision_;
        const auto floor =
            rebuild ? session->baselineRevision : cachedRevision_;
        // A failed query must force a complete rebuild on the next request.
        cachedSession_.clear();
        if (rebuild) {
            counts_.clear();
            eventSpecies_.clear();
        }
        const auto cutoff = parseUtc(session->observedFromUtc);
        detail::Statement rows(
            db, "SELECT "
                "r.event_json,r.event_id,r.source_id,r.observed_at,r.first_"
                "seen_at,r.seq,e.revision_seq "
                "FROM revisions r LEFT JOIN events e ON e.id=r.event_id "
                "WHERE r.seq>?1 AND r.seq<=?2 ORDER BY r.seq");
        rows.bind(1, floor);
        rows.bind(2, revision);
        while (rows.next()) {
            ++lastExamined_;
            const auto eventId = rows.text(1);
            if (const auto old = eventSpecies_.find(eventId);
                old != eventSpecies_.end()) {
                const auto count = counts_.find(old->second);
                if (--count->second.count == 0)
                    counts_.erase(count);
                eventSpecies_.erase(old);
            }
            if (rows.null(6) || rows.integer(5) != rows.integer(6))
                continue;
            const auto payload = rows.text(0);
            try {
                detail::validateStoredEvent(db, payload);
                detail::Statement identity(
                    db, "SELECT "
                        "json_extract(?1,'$.provenance_mode'),json_extract(?1,'"
                        "$.event_id'),"
                        "json_extract(?1,'$.source_id'),json_extract(?1,'$."
                        "observed_at_utc'),"
                        "json_extract(?1,'$.first_seen_at_utc'),json_extract(?"
                        "1,'$.species_id'),json_extract(?1,'$.common_name')");
                identity.bindJson(payload);
                if (!identity.next() || identity.text(0) != "live_source" ||
                    identity.text(1) != eventId ||
                    identity.text(2) != rows.text(2) ||
                    identity.text(3) != rows.text(3) ||
                    identity.text(4) != rows.text(4) ||
                    parseUtc(identity.text(3)) < cutoff || identity.null(5))
                    continue;
                const auto id = identity.text(5);
                if (id.empty())
                    continue;
                const auto name = identity.null(6) ? id : identity.text(6);
                auto [entry, inserted] =
                    counts_.try_emplace(id, Row{id, name, 0});
                (void)inserted;
                entry->second.name = name;
                ++entry->second.count;
                eventSpecies_.emplace(eventId, id);
            } catch (const std::invalid_argument &) {
                // Invalid current corrections remove a previously counted
                // record.
            }
        }
        ranked_.clear();
        ranked_.reserve(counts_.size());
        for (const auto &[id, row] : counts_) {
            (void)id;
            ranked_.push_back(row);
        }
        std::sort(
            ranked_.begin(), ranked_.end(), [](const Row &a, const Row &b) {
                return a.count != b.count ? a.count > b.count : a.id < b.id;
            });
        cachedSession_ = session->id;
        cachedRevision_ = revision;
    }
    std::string result = prefix + ",\"sessionActive\":true,\"species\":[";
    const auto start = std::min(offset, ranked_.size());
    const auto end = start + std::min(limit, ranked_.size() - start);
    for (auto i = start; i < end; ++i) {
        if (i != start)
            result += ",";
        const auto &row = ranked_[i];
        result += "{\"speciesId\":" + detail::quoteJson(row.id) +
                  ",\"commonName\":" + detail::quoteJson(row.name) +
                  ",\"count\":" + std::to_string(row.count) + "}";
    }
    return result + "],\"nextCursor\":" +
           (end < ranked_.size() ? detail::quoteJson(std::to_string(end))
                                 : "null") +
           "}";
}
} // namespace atlas
