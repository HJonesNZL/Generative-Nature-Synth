#include <atlas/storage.hpp>
#include <sqlite3.h>
#include <limits>
#include <stdexcept>

namespace atlas {
namespace {
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK)
            throw std::runtime_error("database query failed; schema or JSON support unavailable");
    }
    ~Statement() { sqlite3_finalize(value_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(std::string_view value) {
        if (value.size() > 128 || sqlite3_bind_text(value_, 1, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::invalid_argument("invalid session ID");
    }
    void bindLimit(std::size_t value) {
        if (sqlite3_bind_int(value_, 1, static_cast<int>(value)) != SQLITE_OK) throw std::runtime_error("query bind failed");
    }
    bool next() {
        const int result = sqlite3_step(value_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        throw std::runtime_error("database read failed");
    }
    bool null(int column) const { return sqlite3_column_type(value_, column) == SQLITE_NULL; }
    std::string text(int column) const {
        const auto* value = sqlite3_column_text(value_, column);
        if (!value) throw std::runtime_error("required text field missing");
        const auto size = sqlite3_column_bytes(value_, column);
        if (size > 16384) throw std::runtime_error("text field exceeds native capacity");
        return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
    }
    std::optional<std::string> optionalText(int column) const {
        return null(column) ? std::nullopt : std::optional<std::string>{text(column)};
    }
    std::int64_t integer(int column) const {
        if (sqlite3_column_type(value_, column) != SQLITE_INTEGER) throw std::runtime_error("integer field missing or invalid");
        return sqlite3_column_int64(value_, column);
    }
    double number(int column) const { return sqlite3_column_double(value_, column); }
private:
    sqlite3_stmt* value_{};
};

constexpr auto sessionColumns = R"SQL(
 SELECT id,name,state,origin,event_count,
 json_extract(options_json,'$.timeline_bounds.observed[0]'),
 json_extract(options_json,'$.timeline_bounds.observed[1]'),
 json_extract(options_json,'$.timeline_bounds.arrival[0]'),
 json_extract(options_json,'$.timeline_bounds.arrival[1]'),
 COALESCE(json_extract(options_json,'$.capture_truncated'),0)
 FROM sessions
)SQL";

SavedSession readSession(const Statement& query) {
    SavedSession session;
    session.id = query.text(0); session.name = query.text(1);
    session.state = query.text(2); session.origin = query.text(3);
    const auto count = query.integer(4);
    if (count < 0 || count > 50000) throw std::runtime_error("saved session exceeds native capacity");
    session.eventCount = static_cast<std::size_t>(count);
    if (!query.null(5) && !query.null(6)) session.observedBounds = {parseUtc(query.text(5)), parseUtc(query.text(6))};
    if (!query.null(7) && !query.null(8)) session.arrivalBounds = {parseUtc(query.text(7)), parseUtc(query.text(8))};
    session.truncated = query.integer(9) != 0;
    return session;
}
} // namespace

struct ReadOnlyLibrary::Impl {
    sqlite3* db{};
    ~Impl() { if (db) sqlite3_close(db); }
};

ReadOnlyLibrary::ReadOnlyLibrary(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    const auto utf8 = path.u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    if (sqlite3_open_v2(filename.c_str(), &impl_->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        throw std::runtime_error("unable to open existing library read-only");
    sqlite3_busy_timeout(impl_->db, 5000);
    Statement version(impl_->db, "PRAGMA user_version");
    if (!version.next() || version.integer(0) != 2) throw std::runtime_error("native reader requires database schema v2");
    Statement jsonSupport(impl_->db, "SELECT json_valid('{}')");
    if (!jsonSupport.next() || jsonSupport.integer(0) != 1) throw std::runtime_error("SQLite JSON functions required");
    if (sqlite3_db_readonly(impl_->db, "main") != 1) throw std::runtime_error("library must be read-only");
}
ReadOnlyLibrary::~ReadOnlyLibrary() = default;

std::vector<SavedSession> ReadOnlyLibrary::sessions(std::size_t limit) const {
    if (limit < 1 || limit > 100) throw std::invalid_argument("session list limit must be 1..100");
    const std::string sql = std::string(sessionColumns) + " ORDER BY created_at DESC,id LIMIT ?";
    Statement query(impl_->db, sql.c_str());
    query.bindLimit(limit);
    std::vector<SavedSession> result;
    while (query.next()) result.push_back(readSession(query));
    return result;
}

SavedSession ReadOnlyLibrary::session(std::string_view id) const {
    const auto sql = std::string(sessionColumns) + (id == "latest" ?
        " WHERE state IN ('frozen','interrupted') ORDER BY created_at DESC,id LIMIT 1" : " WHERE id=?");
    Statement query(impl_->db, sql.c_str());
    if (id != "latest") query.bind(id);
    if (!query.next()) throw std::invalid_argument("saved session not found");
    return readSession(query);
}

Timeline ReadOnlyLibrary::load(std::string_view id, TimeBasis basis, Speed speed) const {
    const auto saved = session(id);
    if (saved.state != "frozen" && saved.state != "interrupted") throw std::invalid_argument("stop recording before native replay");
    Statement query(impl_->db, R"SQL(
        SELECT original_seq,event_json,
        json_extract(event_json,'$.event_id'),json_extract(event_json,'$.source_id'),
        json_extract(event_json,'$.event_kind'),json_extract(event_json,'$.location_kind'),
        json_extract(event_json,'$.species_id'),json_extract(event_json,'$.individual_id'),
        json_extract(event_json,'$.deployment_id'),json_extract(event_json,'$.station_id'),
        json_extract(event_json,'$.latitude'),json_extract(event_json,'$.longitude'),
        json_extract(event_json,'$.observed_at_utc'),json_extract(event_json,'$.first_seen_at_utc'),
        json_extract(event_json,'$.ingested_at_utc'),json_extract(event_json,'$.schema_version'),
        json_type(event_json,'$.latitude'),json_type(event_json,'$.longitude'),ordinal
        FROM session_events WHERE session_id=? ORDER BY ordinal LIMIT 50001
    )SQL");
    query.bind(saved.id);
    std::vector<Event> events;
    events.reserve(saved.eventCount);
    std::size_t bytes{};
    while (query.next()) {
        if (events.size() >= 50000 || query.integer(18) != static_cast<std::int64_t>(events.size()))
            throw std::runtime_error("session ordinal/count invalid");
        Event event;
        event.revisionSequence = query.integer(0); event.normalizedJson = query.text(1);
        bytes += event.normalizedJson.size();
        if (bytes > 64 * 1024 * 1024) throw std::runtime_error("native session memory capacity exceeded");
        event.id = query.text(2); event.sourceId = query.text(3);
        event.kind = query.text(4); event.locationKind = query.text(5);
        event.speciesId = query.optionalText(6); event.individualId = query.optionalText(7);
        event.deploymentId = query.optionalText(8); event.stationId = query.optionalText(9);
        if (query.null(10) != query.null(11)) throw std::runtime_error("incomplete coordinate pair");
        if (!query.null(10)) {
            for (const int column : {16, 17}) {
                const auto type = query.text(column);
                if (type != "real" && type != "integer") throw std::runtime_error("nonnumeric coordinate");
            }
            event.position = {query.number(10), query.number(11)};
        }
        event.observedAt = parseUtc(query.text(12)); event.firstSeenAt = parseUtc(query.text(13));
        event.ingestedAt = parseUtc(query.text(14));
        if (query.integer(15) != 1) throw std::runtime_error("unsupported event schema");
        events.push_back(std::move(event));
    }
    if (events.size() != saved.eventCount) throw std::runtime_error("saved session count mismatch");
    return Timeline(std::move(events), basis, speed,
                    basis == TimeBasis::observed ? saved.observedBounds : saved.arrivalBounds);
}
} // namespace atlas
