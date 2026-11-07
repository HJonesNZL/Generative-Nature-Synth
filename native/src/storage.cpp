#include <atlas/storage.hpp>
#include <sqlite3.h>
#include "sha256.hpp"
#include "sqlite_statement.hpp"
#include "event_validation.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <random>
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
 COALESCE(json_extract(options_json,'$.capture_truncated'),0),content_hash,
 COALESCE(json_extract(options_json,'$.window_policy'),'legacy_saved_bounds'),created_at
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
    session.observedWindowPolicy = query.text(11);
    return session;
}
void validateOptions(sqlite3* db, std::string_view id) {
    Statement query(db, R"SQL(
        SELECT length(CAST(options_json AS BLOB))<=16384
          AND json_type(options_json)='object'
          AND json_type(options_json,'$.seed')='integer'
          AND typeof(json_extract(options_json,'$.seed'))='integer'
          AND json_extract(options_json,'$.seed')>=0
          AND json_type(options_json,'$.event_schema_version')='integer'
          AND json_extract(options_json,'$.event_schema_version')=1
          AND json_type(options_json,'$.software_version')='text'
          AND json_type(options_json,'$.selection_policy')='text'
          AND json_type(options_json,'$.permissions')='text'
          AND json_type(options_json,'$.timeline_bounds')='object'
          AND (SELECT count(*) FROM json_each(options_json,'$.timeline_bounds'))=2
          AND json_type(options_json,'$.timeline_bounds.observed')='array'
          AND json_array_length(options_json,'$.timeline_bounds.observed')=2
          AND json_type(options_json,'$.timeline_bounds.arrival')='array'
          AND json_array_length(options_json,'$.timeline_bounds.arrival')=2
          AND NOT EXISTS (
            SELECT 1 FROM json_each(options_json) field WHERE CASE field.key
              WHEN 'sources' THEN field.type='null' OR (field.type='array' AND json_array_length(field.value)<=30
                AND NOT EXISTS (SELECT 1 FROM json_each(field.value) source
                                WHERE source.type!='text' OR length(source.value) NOT BETWEEN 1 AND 128))
              WHEN 'seed' THEN 1 WHEN 'event_schema_version' THEN 1
              WHEN 'timeline_bounds' THEN 1
              WHEN 'software_version' THEN length(field.value) BETWEEN 1 AND 256
              WHEN 'selection_policy' THEN length(field.value) BETWEEN 1 AND 256
              WHEN 'permissions' THEN length(field.value) BETWEEN 1 AND 256
              WHEN 'capture_truncated' THEN field.type IN ('true','false')
              WHEN 'observed_from' THEN field.type IN ('text','null')
              WHEN 'observed_to' THEN field.type IN ('text','null')
              WHEN 'interruption_detected_at_utc' THEN field.type IN ('text','null')
              WHEN 'end_time_quality' THEN field.type='text' AND length(field.value) BETWEEN 1 AND 256
              WHEN 'original_session' THEN field.type='object'
              WHEN 'window_policy' THEN field.type='text' AND field.value IN ('preserve_requested','trim_to_material')
              WHEN 'window_policy_version' THEN field.type='integer' AND field.value=1
              ELSE 0 END IS NOT 1)
          AND (SELECT count(*)=count(DISTINCT key) FROM json_each(options_json)),
          json_extract(options_json,'$.window_policy'),json_type(options_json,'$.window_policy_version'),
          json_extract(options_json,'$.observed_from'),json_extract(options_json,'$.observed_to'),
          json_extract(options_json,'$.timeline_bounds.observed[0]'),json_extract(options_json,'$.timeline_bounds.observed[1]'),
          json_extract(options_json,'$.timeline_bounds.arrival[0]'),json_extract(options_json,'$.timeline_bounds.arrival[1]'),
          created_at,ended_at,intended_end_at,start_seq,end_seq,
          json_extract(options_json,'$.interruption_detected_at_utc')
        FROM sessions WHERE id=?
    )SQL");
    query.bind(id);
    if (!query.next() || query.null(0) || query.integer(0) != 1) throw std::runtime_error("invalid saved session options");
    if (query.null(1) != query.null(2)) throw std::runtime_error("missing window policy/version");
    for (const int index : {3, 4, 9, 10, 11, 14}) if (!query.null(index)) (void)parseUtc(query.text(index));
    if (query.null(9) || query.integer(12) < 0 || query.integer(13) < 0)
        throw std::runtime_error("invalid session time/sequence");
    for (const int index : {5, 7}) {
        if (parseUtc(query.text(index)) > parseUtc(query.text(index + 1))) throw std::runtime_error("reversed timeline bounds");
    }
    if (!query.null(3) && !query.null(4) && parseUtc(query.text(3)) >= parseUtc(query.text(4)))
        throw std::runtime_error("reversed observation query");
    if (!query.null(1) && query.text(1) == "preserve_requested" &&
        (query.null(3) || query.null(4) || query.text(3) != query.text(5) || query.text(4) != query.text(6)))
        throw std::runtime_error("requested window does not match saved bounds");
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
    validateOptions(db, id);
    return "sha256-envelope-v1-verified";
}
std::string hexText(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result += hex[byte >> 4U];
        result += hex[byte & 15U];
    }
    return result;
}
std::string unhexText(std::string_view value) {
    if (value.empty() || value.size() % 2 != 0 || value.size() > 256)
        throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a') + 10U;
        throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
    };
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 2)
        result += static_cast<char>((digit(value[i]) << 4U) | digit(value[i + 1]));
    if (result.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
    return result;
}
std::string cursorDigest(std::string_view value) {
    detail::Sha256 hash;
    hash.update(value);
    return hash.hexDigest();
}
struct SessionAnchor {
    std::string createdAt, id;
};
SessionAnchor parseCursor(std::string_view cursor, const LibraryRevision& library) {
    if (cursor.size() > 2048)
        throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
    std::array<std::string_view, 6> fields;
    std::size_t start{};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto end = cursor.find('.', start);
        if ((end == std::string_view::npos) != (i == fields.size() - 1))
            throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
        fields[i] = cursor.substr(start, end == std::string_view::npos ? end : end - start);
        if (end != std::string_view::npos) start = end + 1;
    }
    const auto body = cursor.substr(0, cursor.size() - fields.back().size() - 1);
    if (fields[0] != "v1" || fields[1].size() != 32 || fields[2].empty() ||
        fields[2].size() > 20 || fields[5] != cursorDigest(body))
        throw std::invalid_argument("Invalid session cursor. Refresh pieces.");
    SessionAnchor anchor{unhexText(fields[3]), unhexText(fields[4])};
    if (fields[1] != library.sourceId || fields[2] != library.revision)
        throw StaleLibraryCursor("Library changed. Refresh pieces before continuing.");
    return anchor;
}
std::string makeCursor(const LibraryRevision& library, const SessionAnchor& anchor) {
    if (anchor.createdAt.empty() || anchor.createdAt.size() > 128 || anchor.id.empty() || anchor.id.size() > 128 ||
        anchor.createdAt.find('\0') != std::string::npos || anchor.id.find('\0') != std::string::npos)
        throw std::runtime_error("Saved session paging key is invalid.");
    auto value = "v1." + library.sourceId + "." + library.revision + "." + hexText(anchor.createdAt) + "." + hexText(anchor.id);
    return value + "." + cursorDigest(value);
}
} // namespace

struct ReadOnlyLibrary::Impl {
    sqlite3* db{};
    int schema{};
    std::string sourceId;
    ~Impl() { if (db) sqlite3_close(db); }
};

ReadOnlyLibrary::ReadOnlyLibrary(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found)
        throw LibraryOpenError(LibraryOpenFailure::missing, "Library file is missing. Choose an existing library.");
    if (error || !std::filesystem::is_regular_file(status))
        throw LibraryOpenError(LibraryOpenFailure::unreadable, "Library file cannot be read. Choose an accessible library.");
    const auto utf8 = path.u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    if (sqlite3_open_v2(filename.c_str(), &impl_->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        throw LibraryOpenError(LibraryOpenFailure::unreadable, "Library file cannot be opened read-only. Choose an accessible library.");
    sqlite3_busy_timeout(impl_->db, 5000);
    try {
        Statement version(impl_->db, "PRAGMA user_version");
        if (!version.next() || (version.integer(0) != 2 && version.integer(0) != 3))
            throw std::runtime_error("unsupported schema");
        impl_->schema = static_cast<int>(version.integer(0));
        Statement jsonSupport(impl_->db, "SELECT json_valid('{}')");
        if (!jsonSupport.next() || jsonSupport.integer(0) != 1)
            throw std::runtime_error("SQLite JSON functions required");
        const auto probeSql = std::string(sessionColumns) + " LIMIT 0";
        Statement sessionsProbe(impl_->db, probeSql.c_str());
        Statement eventsProbe(impl_->db, "SELECT session_id,ordinal,original_seq,event_json FROM session_events LIMIT 0");
        if (impl_->schema == 3) {
            Statement envelopeProbe(impl_->db, "SELECT envelope_version,envelope_json,envelope_hash FROM sessions LIMIT 0");
        }
    } catch (const std::exception&) {
        const auto code = sqlite3_errcode(impl_->db);
        if (code == SQLITE_CANTOPEN || code == SQLITE_IOERR || code == SQLITE_PERM ||
            code == SQLITE_AUTH || code == SQLITE_BUSY || code == SQLITE_LOCKED)
            throw LibraryOpenError(LibraryOpenFailure::unreadable, "Library file cannot be read. Close conflicting access or choose an accessible library.");
        throw LibraryOpenError(LibraryOpenFailure::incompatible, "Library format is incompatible. Choose a schema v2 or v3 library.");
    }
    if (sqlite3_db_readonly(impl_->db, "main") != 1)
        throw LibraryOpenError(LibraryOpenFailure::unreadable, "Library connection must be read-only.");
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        const auto byte = random() & 255U;
        impl_->sourceId += hex[byte >> 4U];
        impl_->sourceId += hex[byte & 15U];
    }
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

LibraryRevision ReadOnlyLibrary::revision() const {
    Statement query(impl_->db, "PRAGMA data_version");
    if (!query.next()) throw std::runtime_error("Library revision unavailable.");
    return {impl_->sourceId, std::to_string(query.integer(0))};
}

SessionPage ReadOnlyLibrary::sessionPage(std::size_t limit, std::string_view cursor) const {
    if (limit < 1 || limit > 100) throw std::invalid_argument("session list limit must be 1..100");
    SessionPage page;
    page.library = revision();
    const auto anchor = cursor.empty() ? std::optional<SessionAnchor>{} : parseCursor(cursor, page.library);
    {
        ReadSnapshot snapshot(impl_->db);
        // Pin the snapshot before checking its revision; BEGIN alone does not read.
        Statement pin(impl_->db, "SELECT id FROM sessions LIMIT 1");
        (void)pin.next();
        if (revision() != page.library)
            throw StaleLibraryCursor("Library changed. Refresh pieces before continuing.");
        // Hex literals avoid interpolation of source text into SQL and keep the
        // existing statement adapter's single-parameter interface unchanged.
        std::string where;
        if (anchor) {
            const auto created = "CAST(X'" + hexText(anchor->createdAt) + "' AS TEXT)";
            const auto id = "CAST(X'" + hexText(anchor->id) + "' AS TEXT)";
            Statement exists(impl_->db, ("SELECT 1 FROM sessions WHERE created_at=" + created + " AND id=" + id).c_str());
            if (!exists.next()) throw std::invalid_argument("Invalid session cursor anchor. Refresh pieces.");
            where = " WHERE created_at<" + created + " OR (created_at=" + created + " AND id>" + id + ")";
        }
        const auto sql = std::string(sessionColumns) + where + " ORDER BY created_at DESC,id ASC LIMIT " + std::to_string(limit + 1);
        Statement query(impl_->db, sql.c_str());
        SessionAnchor last;
        page.sessions.reserve(limit);
        while (query.next()) {
            if (page.sessions.size() == limit) {
                page.nextCursor = makeCursor(page.library, last);
                break;
            }
            auto saved = readSession(query);
            if (saved.state == "frozen" || saved.state == "interrupted")
                saved.metadataIntegrity = verifyMetadata(impl_->db, saved.id, impl_->schema);
            last = {query.text(12), saved.id};
            page.sessions.push_back(std::move(saved));
        }
    }
    if (revision() != page.library)
        throw StaleLibraryCursor("Library changed. Refresh pieces before continuing.");
    return page;
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
        if (saved.observedWindowPolicy == "preserve_requested" && (!saved.observedBounds ||
            event.observedAt < saved.observedBounds->first || event.observedAt >= saved.observedBounds->second))
            throw std::runtime_error("event outside requested half-open window");
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
