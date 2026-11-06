#include <atlas/storage.hpp>
#include <sqlite3.h>
#include "sha256.hpp"
#include "sqlite_statement.hpp"
#include "event_validation.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas {
namespace {
class ReadSnapshot {
public:
    explicit ReadSnapshot(sqlite3* db) : db_(db) {
        if (sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error("unable to begin read snapshot");
    }
    ~ReadSnapshot() { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    ReadSnapshot(const ReadSnapshot&) = delete;
    ReadSnapshot& operator=(const ReadSnapshot&) = delete;
private:
    sqlite3* db_;
};

using detail::Statement;

constexpr auto sessionColumns = R"SQL(
 SELECT id,name,state,origin,event_count,
 json_extract(options_json,'$.timeline_bounds.observed[0]'),
 json_extract(options_json,'$.timeline_bounds.observed[1]'),
 json_extract(options_json,'$.timeline_bounds.arrival[0]'),
 json_extract(options_json,'$.timeline_bounds.arrival[1]'),
 COALESCE(json_extract(options_json,'$.capture_truncated'),0),content_hash
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
    session.contentHash = query.optionalText(10).value_or("");
    return session;
}
std::string verifyMetadata(sqlite3* db, std::string_view id, int schema) {
    if (schema == 2) return "legacy-metadata-unverified";
    Statement seal(db, "SELECT envelope_version,envelope_json,envelope_hash FROM sessions WHERE id=?");
    seal.bind(id);
    if (!seal.next()) throw std::runtime_error("session disappeared");
    const auto version = seal.integer(0);
    if (version == 0) {
        if (!seal.null(1) || !seal.null(2)) throw std::runtime_error("partial legacy metadata envelope");
        return "legacy-metadata-unverified";
    }
    if (version != 1) throw std::runtime_error("unsupported metadata envelope version");
    detail::Sha256 hash;
    hash.update(seal.text(1));
    if (hash.hexDigest() != seal.text(2)) throw std::runtime_error("metadata integrity checksum mismatch");
    // Bind every sealed field to the actual columns, without reserialising JSON numbers/Unicode.
    Statement binding(db, R"SQL(
        SELECT json_type(envelope_json)='object'
          AND (SELECT count(*) FROM json_each(envelope_json))=2
          AND json_type(envelope_json,'$.version')='integer'
          AND json_extract(envelope_json,'$.version')=1
          AND json_type(envelope_json,'$.session')='object'
          AND (SELECT count(*) FROM json_each(envelope_json,'$.session'))=12
          AND NOT EXISTS (
            SELECT 1 FROM json_each(envelope_json,'$.session') field
            WHERE CASE field.key
              WHEN 'id' THEN field.type='text' AND field.value IS sessions.id
              WHEN 'name' THEN field.type='text' AND field.value IS name
              WHEN 'state' THEN field.type='text' AND field.value IS state
              WHEN 'created_at' THEN field.type='text' AND field.value IS created_at
              WHEN 'ended_at' THEN field.type IN ('text','null') AND field.value IS ended_at
              WHEN 'start_seq' THEN field.type='integer' AND field.value IS start_seq
              WHEN 'end_seq' THEN field.type='integer' AND field.value IS end_seq
              WHEN 'intended_end_at' THEN field.type IN ('text','null') AND field.value IS intended_end_at
              WHEN 'origin' THEN field.type='text' AND field.value IS origin
              WHEN 'options' THEN field.type='object' AND field.value IS options_json
              WHEN 'event_count' THEN field.type='integer' AND field.value IS event_count
              WHEN 'content_hash' THEN field.type='text' AND field.value IS content_hash
              ELSE 0 END IS NOT 1)
          AND (SELECT count(DISTINCT key) FROM json_each(envelope_json,'$.session'))=12
          AND (SELECT count(DISTINCT key) FROM json_each(envelope_json))=2
        FROM sessions WHERE id=?
    )SQL");
    binding.bind(id);
    if (!binding.next() || binding.null(0) || binding.integer(0) != 1)
        throw std::runtime_error("metadata integrity failure: envelope does not match session");
    return "sha256-envelope-v1-verified";
}
} // namespace

struct ReadOnlyLibrary::Impl {
    sqlite3* db{};
    int schema{};
    ~Impl() { if (db) sqlite3_close(db); }
};

ReadOnlyLibrary::ReadOnlyLibrary(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    const auto utf8 = path.u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    if (sqlite3_open_v2(filename.c_str(), &impl_->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        throw std::runtime_error("unable to open existing library read-only");
    sqlite3_busy_timeout(impl_->db, 5000);
    Statement version(impl_->db, "PRAGMA user_version");
    if (!version.next() || (version.integer(0) != 2 && version.integer(0) != 3))
        throw std::runtime_error("native reader requires database schema v2 or v3");
    impl_->schema = static_cast<int>(version.integer(0));
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
    while (query.next()) {
        auto saved = readSession(query);
        if (saved.state == "frozen" || saved.state == "interrupted")
            saved.metadataIntegrity = verifyMetadata(impl_->db, saved.id, impl_->schema);
        result.push_back(std::move(saved));
    }
    return result;
}

SavedSession ReadOnlyLibrary::session(std::string_view id) const {
    const auto sql = std::string(sessionColumns) + (id == "latest" ?
        " WHERE state IN ('frozen','interrupted') ORDER BY created_at DESC,id LIMIT 1" : " WHERE id=?");
    Statement query(impl_->db, sql.c_str());
    if (id != "latest") query.bind(id);
    if (!query.next()) throw std::invalid_argument("saved session not found");
    auto saved = readSession(query);
    if (saved.state == "frozen" || saved.state == "interrupted")
        saved.metadataIntegrity = verifyMetadata(impl_->db, saved.id, impl_->schema);
    return saved;
}

Timeline ReadOnlyLibrary::load(std::string_view id, TimeBasis basis, Speed speed, SavedSession* verifiedSession) const {
    // One consistent SQLite read transaction for metadata and frozen payloads.
    ReadSnapshot snapshot(impl_->db);
    const auto saved = session(id);
    if (saved.state != "frozen" && saved.state != "interrupted") throw std::invalid_argument("stop recording before native replay");
    if (saved.contentHash.size() != 64 || !std::all_of(saved.contentHash.begin(), saved.contentHash.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw std::runtime_error("session integrity checksum missing or invalid");
    detail::Sha256 checksum;
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
        if (event.normalizedJson.size() > 16384) throw std::runtime_error("event exceeds native capacity");
        detail::validateStoredEvent(impl_->db, event.normalizedJson);
        bytes += event.normalizedJson.size();
        if (bytes > 64 * 1024 * 1024) throw std::runtime_error("native session memory capacity exceeded");
        // Schema-v2 writers persist Python canonical event JSON verbatim.
        checksum.update("{\"event\":");
        checksum.update(event.normalizedJson);
        checksum.update(",\"original_seq\":");
        checksum.update(std::to_string(event.revisionSequence));
        checksum.update("}\n");
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
    if (checksum.hexDigest() != saved.contentHash)
        throw std::runtime_error("session integrity failure: frozen event content changed");
    Timeline timeline(std::move(events), basis, speed,
                      basis == TimeBasis::observed ? saved.observedBounds : saved.arrivalBounds);
    if (verifiedSession) *verifiedSession = saved;
    return timeline;
}
} // namespace atlas
