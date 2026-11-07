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
    baselineCursor_ = cursor_;
    timingGroups_.clear();
    return reset;
}
LiveBatch LiveAdmission::poll(std::size_t limit) {
    return pollImpl(limit, false);
}
LiveBatch LiveAdmission::pollFifo(std::size_t limit, std::int64_t intervalUs) {
    return pollImpl(limit, true, intervalUs);
}
LiveSmoothTiming LiveAdmission::timing(const std::string &source,
                                      const std::string &ingested,
                                      const std::string &observed,
                                      std::int64_t seq, std::int64_t high,
                                      std::int64_t intervalUs) {
    auto group = std::find_if(timingGroups_.begin(), timingGroups_.end(),
                             [&](const TimingGroup &g) {
                                 return g.source == source &&
                                        g.ingested == ingested && seq <= g.high;
                             });
    if (group == timingGroups_.end()) {
        // Source/ingestion index keeps this proportional to the new collection
        // batch, rather than the historical library. Cache ownership is bounded.
        Statement bounds(
            db_, "WITH batch AS (SELECT seq,observed_at,"
                 "julianday(observed_at) AS observed_day,"
                 "LAG(julianday(observed_at)) OVER (ORDER BY seq) AS prior_day "
                 "FROM revisions WHERE source_id=?1 AND ingested_at=?2 "
                 "AND seq>?3 AND seq<=?4 AND change_kind='new' "
                 "AND live_eligible=1 AND julianday(observed_at) IS NOT NULL) "
                 "SELECT COUNT(*),MIN(observed_at),MAX(observed_at),"
                 "SUM(CASE WHEN seq<?5 THEN 1 ELSE 0 END),"
                 "COUNT(DISTINCT observed_at),"
                 "SUM(CASE WHEN observed_day>prior_day THEN 1 ELSE 0 END),"
                 "SUM(CASE WHEN observed_day<prior_day THEN 1 ELSE 0 END) "
                 "FROM batch");
        bounds.bind(1, source);
        bounds.bind(2, ingested);
        bounds.bind(3, baselineCursor_);
        bounds.bind(4, high);
        bounds.bind(5, seq);
        if (!bounds.next() || bounds.integer(0) < 1)
            throw std::runtime_error("Live smoothing batch unavailable");
        TimingGroup next;
        next.source = source;
        next.ingested = ingested;
        next.high = high;
        next.count = bounds.integer(0);
        next.position = bounds.integer(3);
        next.repeatedObservations = bounds.integer(4) < next.count;
        const auto ascendingSteps = bounds.integer(5);
        const auto descendingSteps = bounds.integer(6);
        if (ascendingSteps > 0 && descendingSteps == 0)
            next.observationDirection = 1;
        else if (descendingSteps > 0 && ascendingSteps == 0)
            next.observationDirection = -1;
        // Invalid neighboring rows must not poison a valid event's timing.
        const auto observedUs = parseUtc(observed);
        const auto safeObserved = [observedUs](const std::string &value) {
            try {
                return parseUtc(value);
            } catch (const std::invalid_argument &) {
                return observedUs;
            }
        };
        next.minimumObserved = safeObserved(bounds.text(1));
        next.maximumObserved = safeObserved(bounds.text(2));
        // Empty collections still establish real source cadence. Old libraries
        // without poll history retain the source-specific revision fallback.
        Statement polls(db_, "SELECT 1 FROM sqlite_master WHERE type='table' "
                             "AND name='polls'");
        const bool hasPolls = polls.next();
        std::vector<std::string> pollSources{source};
        if (hasPolls && source == "birdweather") {
            // Regional collectors retain their own poll identities while event
            // provenance correctly remains the shared BirdWeather namespace.
            Statement regional(
                db_, "SELECT DISTINCT source_id FROM polls WHERE completed_at=?1 "
                     "AND (source_id='birdweather' OR source_id GLOB 'birdweather:*') "
                     "ORDER BY source_id LIMIT 16");
            regional.bind(1, ingested);
            while (regional.next()) {
                const auto pollSource = regional.text(0);
                if (pollSource != source)
                    pollSources.push_back(pollSource);
            }
        }
        std::int64_t latestPrior{};
        for (const auto &pollSource : pollSources) {
            Statement prior(
                db_, hasPolls
                         ? "SELECT completed_at FROM polls WHERE source_id=?1 "
                           "AND completed_at<?2 ORDER BY completed_at DESC LIMIT 1"
                         : "SELECT ingested_at FROM revisions WHERE source_id=?1 "
                           "AND ingested_at<?2 ORDER BY ingested_at DESC LIMIT 1");
            prior.bind(1, pollSource);
            prior.bind(2, ingested);
            if (prior.next()) {
                try {
                    latestPrior = std::max(latestPrior, parseUtc(prior.text(0)));
                } catch (const std::invalid_argument &) {
                    // Retain caller interval or use genuine observation span.
                  }
            }
        }
        if (latestPrior > 0)
            intervalUs = parseUtc(ingested) - latestPrior;
        // Bounds protect against outage/history catch-up, rather than imposing
        // a musical phrase duration. Managed sources retain their real cadence.
        std::int64_t configuredCadenceUs =
            source == "birdweather" || source == "birdweather_world" ||
                    source.starts_with("birdweather:")
                ? 60000000
                : 900000000;
        if (hasPolls) {
            bool hasDetails{};
            Statement columns(db_, "PRAGMA table_info(polls)");
            while (columns.next())
                hasDetails = hasDetails || columns.text(1) == "details";
            if (hasDetails) {
                for (const auto &pollSource : pollSources) {
                    Statement configured(
                        db_, "SELECT CAST(json_extract(CASE WHEN json_valid(details) "
                             "THEN details ELSE '{}' END,'$.configured_poll_seconds') "
                             "AS INTEGER) FROM polls WHERE source_id=?1 AND "
                             "completed_at<=?2 ORDER BY completed_at DESC LIMIT 1");
                    configured.bind(1, pollSource);
                    configured.bind(2, ingested);
                    if (configured.next() && !configured.null(0)) {
                        const auto seconds = configured.integer(0);
                        if (seconds > 0 && seconds <= 86400)
                            configuredCadenceUs = seconds * 1000000;
                    }
                }
            }
        }
        const auto observationSpan =
            std::max<std::int64_t>(0, next.maximumObserved - next.minimumObserved);
        if (next.observationDirection != 0 && observationSpan > 0) {
            // Reliable observation gaps remain seconds-for-seconds, including
            // newest-first order, unless stale history exceeds source cadence.
            next.horizonUs = std::min(observationSpan, configuredCadenceUs);
        } else {
            // Unresolved timestamps use actual source collection spacing. An
            // outage cannot create a silent backlog longer than normal cadence.
            if (intervalUs <= 0)
                intervalUs = configuredCadenceUs;
            next.horizonUs = std::min(intervalUs, configuredCadenceUs);
        }
        if (timingGroups_.size() == 128)
            timingGroups_.pop_front();
        timingGroups_.push_back(std::move(next));
        group = std::prev(timingGroups_.end());
    }
    double phase = group->count <= 1
                       ? 0.0
                       : static_cast<double>(group->position) /
                             static_cast<double>(group->count - 1);
    // Preserve genuine observed spacing when ingestion order supports it.
    // Shared/coarse or unordered timestamps use neutral FIFO rank in that same
    // collection interval, with no added pulse, density rule or animal events.
    if (group->observationDirection != 0 &&
        group->maximumObserved > group->minimumObserved) {
        const auto observedUs = parseUtc(observed);
        const auto observedPhase =
            static_cast<double>(group->observationDirection > 0
                                    ? observedUs - group->minimumObserved
                                    : group->maximumObserved - observedUs) /
                static_cast<double>(group->maximumObserved -
                                    group->minimumObserved);
        // Coarse timestamps do not imply simultaneous biological detections.
        // Interpolate their unknown intra-timestamp order using FIFO position,
        // while retaining the dominant irregular observed-time spacing.
        phase = group->repeatedObservations
                    ? 0.8 * observedPhase + 0.2 * phase
                    : observedPhase;
    }
    auto offset = static_cast<std::int64_t>(std::clamp(phase, 0.0, 1.0) *
                                            static_cast<double>(group->horizonUs));
    offset = std::max(offset, group->previousOffset);
    group->previousOffset = offset;
    ++group->position;
    return {source, parseUtc(ingested), offset};
}
LiveBatch LiveAdmission::pollImpl(std::size_t limit, bool fifo,
                                  std::int64_t intervalUs) {
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
        baselineCursor_ = cursor_;
        timingGroups_.clear();
        LiveBatch reset;
        reset.storeReset = true;
        return reset;
    }
    // Raw mode reads its newest window; FIFO mode retains its committed cursor.
    const auto floor = fifo ? cursor_ : std::max(cursor_, high - 512);
    if (fifo && limit == 0) {
        LiveBatch blocked;
        blocked.hasMore = high > cursor_;
        return blocked;
    }
    Statement rows(
        db_, "SELECT r.seq,r.event_json,r.change_kind,r.live_eligible,"
             "e.revision_seq,r.event_id,r.source_id,r.observed_at,"
             "r.first_seen_at,r.ingested_at FROM revisions r LEFT JOIN events "
             "e ON e.id=r.event_id WHERE r.seq>?1 AND r.seq<=?2 "
             "ORDER BY r.seq LIMIT 512");
    rows.bind(1, floor);
    rows.bind(2, high);
    LiveBatch result;
    auto examinedCursor = cursor_;
    std::size_t examined{};
    while (rows.next()) {
        ++examined;
        if (fifo)
            examinedCursor = rows.integer(0);
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
        if (fifo)
            result.smoothTiming.push_back(timing(rows.text(6), rows.text(9),
                                                 rows.text(7), rows.integer(0),
                                                 high, intervalUs));
        result.payloads.push_back(payload);
        if (fifo && result.payloads.size() == limit)
            break;
    }
    if (!fifo) {
        result.skippedRevisions = static_cast<std::uint64_t>(floor - cursor_);
        cursor_ = high;
    } else {
        // Empty/deleted sequence gaps are consumed without buffering records.
        cursor_ = result.payloads.size() < limit && examined < 512
                      ? high
                      : examinedCursor;
        result.hasMore = cursor_ < high;
    }
    return result;
}
} // namespace atlas
