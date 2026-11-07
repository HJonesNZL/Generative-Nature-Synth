#include "json_text.hpp"
#include "sha256.hpp"
#include "sqlite_statement.hpp"
#include <atlas/piece_writer.hpp>
#include <atomic>
#include <chrono>
#include <limits>
#include <map>
#include <sqlite3.h>
namespace atlas {
namespace {
using detail::quoteJson;
using detail::Statement;
void progress(const PieceProgress &callback, std::string_view stage,
              std::size_t done = 0, std::size_t total = 0) {
    if (callback && !callback(stage, done, total))
        throw PieceCancelled();
}
void exec(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("piece database operation failed");
}
struct Db {
    sqlite3 *value{};
    explicit Db(const std::filesystem::path &path) {
        const auto s = std::filesystem::symlink_status(path);
        if (!std::filesystem::is_regular_file(s))
            throw std::invalid_argument(
                "piece destination must be an existing regular library");
        if (sqlite3_open_v2(path.string().c_str(), &value,
                            SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            if (value)
                sqlite3_close(value);
            value = nullptr;
            throw std::runtime_error("cannot open piece destination");
        }
        sqlite3_busy_timeout(value, 5000);
    }
    ~Db() {
        if (value)
            sqlite3_close(value);
    }
};
struct Transaction {
    sqlite3 *db;
    explicit Transaction(sqlite3 *p, bool writing = true) : db(p) {
        exec(db, writing ? "BEGIN IMMEDIATE" : "BEGIN");
    }
    ~Transaction() { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db, "COMMIT"); }
};
void bindText(sqlite3_stmt *s, int i, std::string_view v) {
    if (sqlite3_bind_text(s, i, v.data(), static_cast<int>(v.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("piece bind failed");
}
void run(sqlite3 *db, const char *sql, const std::vector<std::string> &args) {
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error("piece prepare failed");
    struct Finalize {
        sqlite3_stmt *s;
        ~Finalize() { sqlite3_finalize(s); }
    } final{raw};
    for (std::size_t i = 0; i < args.size(); ++i)
        bindText(raw, static_cast<int>(i + 1), args[i]);
    if (sqlite3_step(raw) != SQLITE_DONE)
        throw std::runtime_error("piece write failed");
}
std::string hash(std::string_view s) {
    detail::Sha256 h;
    h.update(s);
    return h.hexDigest();
}
void managed(sqlite3 *db) {
    Statement q(
        db, "SELECT value FROM metadata WHERE key='native_collection_owner'");
    if (!q.next() || q.text(0) != "animal_atlas_native_v1")
        throw std::invalid_argument("refusing unmanaged piece destination");
    Statement v(db, "PRAGMA user_version");
    if (!v.next() || v.integer(0) != 3)
        throw std::invalid_argument("piece destination schema unsupported");
}
std::string nullable(const std::optional<std::string> &s) {
    return s ? quoteJson(*s) : "null";
}
VerifiedPiece sealed(VerifiedPiece p) {
    if (p.id.empty() || p.id.size() > 128 || p.name.empty() ||
        p.name.size() > 128 || p.origin.empty() || p.origin.size() > 128 ||
        p.optionsJson.size() > 16384 || p.startSequence < 0 ||
        p.endSequence < 0 ||
        (p.state != "frozen" && p.state != "interrupted") ||
        p.events.size() > 50000)
        throw std::invalid_argument("invalid piece metadata");
    (void)parseUtc(p.createdAt);
    if (p.endedAt)
        (void)parseUtc(*p.endedAt);
    if (p.intendedEndAt)
        (void)parseUtc(*p.intendedEndAt);
    detail::Sha256 h;
    std::size_t bytes{};
    for (const auto &e : p.events) {
        bytes += e.eventJson.size();
        if (e.originalSequence < 0 || e.eventJson.size() > 16384 ||
            bytes > 64 * 1024 * 1024)
            throw std::invalid_argument("piece exceeds capacity");
        h.update("{\"event\":");
        h.update(e.eventJson);
        h.update(",\"original_seq\":");
        h.update(std::to_string(e.originalSequence));
        h.update("}\n");
    }
    const auto checksum = h.hexDigest();
    if (!p.contentHash.empty() && p.contentHash != checksum)
        throw std::invalid_argument("piece content checksum mismatch");
    p.contentHash = checksum;
    if (p.envelopeVersion == 0) {
        p.envelopeVersion = 1;
        p.envelopeJson =
            "{\"version\":1,\"session\":{\"id\":" + quoteJson(p.id) +
            ",\"name\":" + quoteJson(p.name) +
            ",\"state\":" + quoteJson(p.state) +
            ",\"created_at\":" + quoteJson(p.createdAt) +
            ",\"ended_at\":" + nullable(p.endedAt) +
            ",\"start_seq\":" + std::to_string(p.startSequence) +
            ",\"end_seq\":" + std::to_string(p.endSequence) +
            ",\"intended_end_at\":" + nullable(p.intendedEndAt) +
            ",\"origin\":" + quoteJson(p.origin) +
            ",\"options\":" + p.optionsJson +
            ",\"event_count\":" + std::to_string(p.events.size()) +
            ",\"content_hash\":" + quoteJson(p.contentHash) + "}}";
        p.envelopeHash = hash(p.envelopeJson);
    }
    if (p.envelopeVersion != 1 || p.envelopeJson.size() > 32768 ||
        hash(p.envelopeJson) != p.envelopeHash)
        throw std::invalid_argument("invalid piece seal");
    return p;
}
void insert(sqlite3 *db, const VerifiedPiece &p,
            const PieceProgress &callback = {},
            std::string_view stage = "writing") {
    run(db,
        "INSERT INTO "
        "sessions(id,name,state,created_at,ended_at,start_seq,end_seq,intended_"
        "end_at,origin,options_json,event_count,content_hash,envelope_version,"
        "envelope_json,envelope_hash) "
        "VALUES(?1,?2,?3,?4,json_extract(?5,'$'),CAST(?6 AS INTEGER),CAST(?7 "
        "AS INTEGER),json_extract(?8,'$'),?9,?10,CAST(?11 AS "
        "INTEGER),?12,1,?13,?14)",
        {p.id, p.name, p.state, p.createdAt, nullable(p.endedAt),
         std::to_string(p.startSequence), std::to_string(p.endSequence),
         nullable(p.intendedEndAt), p.origin, p.optionsJson,
         std::to_string(p.events.size()), p.contentHash, p.envelopeJson,
         p.envelopeHash});
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(db, "INSERT INTO session_events VALUES(?1,?2,?3,?4)",
                           -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error("piece event prepare failed");
    struct Finalize {
        sqlite3_stmt *value;
        ~Finalize() { sqlite3_finalize(value); }
    } finalize{raw};
    for (std::size_t i = 0; i < p.events.size(); ++i) {
        if (i % 256 == 0)
            progress(callback, stage, i, p.events.size());
        bindText(raw, 1, p.id);
        if (sqlite3_bind_int64(raw, 2, static_cast<sqlite3_int64>(i)) !=
                SQLITE_OK ||
            sqlite3_bind_int64(raw, 3, p.events[i].originalSequence) !=
                SQLITE_OK)
            throw std::runtime_error("piece sequence bind failed");
        bindText(raw, 4, p.events[i].eventJson);
        if (sqlite3_step(raw) != SQLITE_DONE ||
            sqlite3_reset(raw) != SQLITE_OK ||
            sqlite3_clear_bindings(raw) != SQLITE_OK)
            throw std::runtime_error("piece event write failed");
    }
    progress(callback, stage, p.events.size(), p.events.size());
}
// Validate the complete prospective artifact using the production replay
// reader.
void validate(const VerifiedPiece &p, const PieceProgress &callback) {
    static std::atomic<std::uint64_t> serial{};
    const auto temp =
        std::filesystem::temp_directory_path() /
        ("atlas-piece-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(++serial) + ".sqlite");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove(p, e);
        }
    } cleanup{temp};
    sqlite3 *raw{};
    if (sqlite3_open_v2(temp.string().c_str(), &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_EXCLUSIVE,
                        nullptr) != SQLITE_OK) {
        if (raw)
            sqlite3_close(raw);
        throw std::runtime_error("piece validation unavailable");
    }
    struct Close {
        sqlite3 *p;
        ~Close() { sqlite3_close(p); }
    } close{raw};
    exec(raw, "CREATE TABLE sessions(id TEXT PRIMARY KEY,name TEXT,state "
              "TEXT,created_at TEXT,ended_at TEXT,start_seq INTEGER,end_seq "
              "INTEGER,intended_end_at TEXT,origin TEXT,options_json "
              "TEXT,event_count INTEGER,content_hash TEXT,envelope_version "
              "INTEGER,envelope_json TEXT,envelope_hash TEXT);CREATE TABLE "
              "session_events(session_id TEXT,ordinal INTEGER,original_seq "
              "INTEGER,event_json TEXT);PRAGMA user_version=3;");
    {
        Transaction transaction(raw);
        insert(raw, p, callback, "validating");
        transaction.commit();
    }
    progress(callback, "verifying", 0, p.events.size());
    ReadOnlyLibrary reader(temp);
    (void)reader.load(p.id, TimeBasis::observed);
    progress(callback, "verifying", p.events.size(), p.events.size());
}
void capacity(sqlite3 *db, const std::filesystem::path &path,
              const VerifiedPiece &piece) {
    constexpr std::uintmax_t limit = 512ULL * 1024 * 1024;
    std::uintmax_t bytes{};
    for (const auto *suffix :
         {"", "-wal", "-shm", ".atlas-import-stage.sqlite3",
          ".atlas-import-stage.sqlite3-journal"}) {
        const std::filesystem::path file(path.string() + suffix);
        if (std::filesystem::is_regular_file(file))
            bytes += std::filesystem::file_size(file);
    }
    // Reserve the new database pages and WAL frames, including B-tree overhead.
    std::uintmax_t growth = 65536 + piece.envelopeJson.size() * 2;
    for (const auto &event : piece.events)
        growth += 2 * (event.eventJson.size() + 512);
    const auto available =
        std::filesystem::space(path.has_parent_path()
                                   ? path.parent_path()
                                   : std::filesystem::path("."))
            .available;
    if (bytes >= limit || growth > limit - bytes ||
        available < growth + 64ULL * 1024 * 1024)
        throw std::runtime_error("local_storage_capacity");
    Statement pageSize(db, "PRAGMA page_size");
    if (!pageSize.next() || pageSize.integer(0) <= 0)
        throw std::runtime_error("piece page size unavailable");
    exec(db, ("PRAGMA max_page_count=" +
              std::to_string(limit /
                             static_cast<std::uintmax_t>(pageSize.integer(0))))
                 .c_str());
}
SavedSession save(sqlite3 *db, const std::filesystem::path &path,
                  const VerifiedPiece &p, const PieceProgress &callback) {
    Statement existing(db, "SELECT envelope_hash FROM sessions WHERE id=?");
    existing.bind(p.id);
    if (existing.next()) {
        if (existing.text(0) != p.envelopeHash)
            throw std::invalid_argument(
                "piece ID already exists with different content");
        ReadOnlyLibrary reader(path);
        return reader.session(p.id);
    }
    capacity(db, path, p);
    insert(db, p, callback);
    SavedSession result;
    result.id = p.id;
    result.name = p.name;
    result.state = p.state;
    result.origin = p.origin;
    result.eventCount = p.events.size();
    result.contentHash = p.contentHash;
    result.metadataIntegrity = "sha256-envelope-v1-verified";
    return result;
}
std::optional<SavedSession> existingFreeze(sqlite3 *db,
                                           const std::filesystem::path &path,
                                           const FreezePieceRequest &r,
                                           Microseconds lower,
                                           Microseconds upper) {
    // A completed ID is immutable: retry returns its previously verified
    // artifact.
    Statement prior(db, "SELECT id FROM sessions WHERE id=?");
    prior.bind(r.id);
    if (prior.next()) {
        Statement criteria(
            db,
            "SELECT "
            "json_extract(options_json,'$.seed'),json_extract(options_json,'$."
            "original_session.freeze_source'),json_extract(options_json,'$."
            "original_session.freeze_species') FROM sessions WHERE id=?");
        criteria.bind(r.id);
        criteria.next();
        if (criteria.integer(0) != r.seed ||
            criteria.optionalText(1) != r.sourceId ||
            criteria.optionalText(2) != r.speciesId)
            throw std::invalid_argument(
                "freeze ID conflicts with saved criteria");
        ReadOnlyLibrary reader(path);
        auto s = reader.session(r.id);
        if (s.name != r.name || s.origin != "archive_snapshot" ||
            s.observedBounds != std::optional(std::pair(lower, upper)))
            throw std::invalid_argument("freeze ID conflicts with saved piece");
        return s;
    }
    return std::nullopt;
}
VerifiedPiece selectObservedPiece(sqlite3 *db, const FreezePieceRequest &r,
                                  Microseconds lower, Microseconds upper,
                                  const PieceProgress &callback) {
    Transaction snapshot(db, false);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    sqlite3_progress_handler(
        db, 1000,
        [](void *ptr) {
            return std::chrono::steady_clock::now() >
                           *static_cast<
                               const std::chrono::steady_clock::time_point *>(
                               ptr)
                       ? 1
                       : 0;
        },
        const_cast<void *>(static_cast<const void *>(&deadline)));
    struct ClearProgress {
        sqlite3 *db;
        ~ClearProgress() { sqlite3_progress_handler(db, 0, nullptr, nullptr); }
    } clearProgress{db};
    VerifiedPiece p;
    p.id = r.id;
    p.name = r.name;
    p.state = "frozen";
    p.createdAt = formatUtc(parseUtc(r.createdAt));
    p.endedAt = p.createdAt;
    p.origin = "archive_snapshot";
    Statement high(db, "SELECT COALESCE(max(seq),0) FROM revisions");
    high.next();
    p.endSequence = high.integer(0);
    std::string sql =
        "SELECT "
        "r.seq,r.event_json,r.observed_at,json_extract(r.event_json,'$.first_"
        "seen_at_utc'),json_extract(r.event_json,'$.observed_at_utc'),r.event_"
        "id,e.id,r.source_id,e.source_id,json_extract(r.event_json,'$.source_"
        "id') FROM events e JOIN revisions r ON r.seq=e.revision_seq WHERE "
        "(?1='' OR r.source_id=?1) AND (?2='' OR "
        "json_extract(r.event_json,'$.species_id')=?2) ORDER BY r.seq";
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error("freeze selection failed");
    struct Close {
        sqlite3_stmt *s;
        ~Close() { sqlite3_finalize(s); }
    } close{raw};
    bindText(raw, 1, r.sourceId.value_or(""));
    bindText(raw, 2, r.speciesId.value_or(""));
    std::optional<Microseconds> arrivalMin, arrivalMax;
    std::map<std::string, std::size_t> sourceCounts;
    if (r.sourceId)
        sourceCounts.emplace(*r.sourceId, 0);
    std::size_t bytes{};
    int rc{};
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
        progress(callback, "selecting", p.events.size(), 0);
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("freeze selection deadline exceeded");
        const auto text = [&](int col) {
            const auto *s = sqlite3_column_text(raw, col);
            if (!s)
                throw std::runtime_error("freeze observation time missing");
            return std::string(
                reinterpret_cast<const char *>(s),
                static_cast<std::size_t>(sqlite3_column_bytes(raw, col)));
        };
        const auto observed = parseUtc(text(2));
        if (observed != parseUtc(text(4)) || text(5) != text(6) ||
            text(7) != text(8) || text(7) != text(9))
            throw std::runtime_error(
                "freeze current revision provenance mismatch");
        if (observed < lower || observed >= upper)
            continue;
        if (sqlite3_column_bytes(raw, 1) > 16384)
            throw std::runtime_error("freeze event exceeds capacity");
        auto json = text(1);
        bytes += json.size();
        if (p.events.size() >= 50000 || bytes > 64 * 1024 * 1024)
            throw std::runtime_error("freeze exceeds session capacity");
        const auto arrival = parseUtc(text(3));
        arrivalMin = arrivalMin ? std::min(*arrivalMin, arrival) : arrival;
        arrivalMax = arrivalMax ? std::max(*arrivalMax, arrival) : arrival;
        p.events.push_back({sqlite3_column_int64(raw, 0), std::move(json)});
        ++sourceCounts[text(7)];
        if (sourceCounts.size() > 128)
            throw std::runtime_error("freeze source summary exceeds capacity");
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("freeze selection failed");
    sqlite3_progress_handler(db, 0, nullptr, nullptr);
    const auto from = formatUtc(lower), to = formatUtc(upper);
    const auto arrivalFrom = formatUtc(arrivalMin.value_or(lower)),
               arrivalTo = formatUtc(arrivalMax.value_or(lower));
    p.optionsJson =
        "{\"seed\":" + std::to_string(r.seed) +
        ",\"event_schema_version\":1,\"software_version\":\"atlas-native\","
        "\"selection_policy\":\"latest_event_revisions_at_snapshot; "
        "coverage_unknown; "
        "missing_times_rejected\",\"permissions\":\"preserve_event_provenance; "
        "reuse_not_asserted\",\"sources\":" +
        (r.sourceId ? "[" + quoteJson(*r.sourceId) + "]" : "null") +
        ",\"capture_truncated\":false,\"observed_from\":" + quoteJson(from) +
        ",\"observed_to\":" + quoteJson(to) +
        ",\"window_policy\":\"preserve_requested\",\"window_policy_version\":1,"
        "\"timeline_bounds\":{\"observed\":[" +
        quoteJson(from) + "," + quoteJson(to) + "],\"arrival\":[" +
        quoteJson(arrivalFrom) + "," + quoteJson(arrivalTo) + "]}}";
    std::string sourceCoverage = "[";
    for (const auto &[source, count] : sourceCounts) {
        if (sourceCoverage.size() > 1)
            sourceCoverage += ",";
        sourceCoverage += "{\"source_id\":" + quoteJson(source) +
                          ",\"captured_event_count\":" + std::to_string(count) +
                          ",\"completeness\":\"unknown\"}";
    }
    sourceCoverage += "]";
    p.optionsJson.pop_back();
    p.optionsJson +=
        ",\"original_session\":{\"freeze_source\":" + nullable(r.sourceId) +
        ",\"freeze_species\":" + nullable(r.speciesId) +
        ",\"coverage_scope\":\"saved_snapshot_only\",\"source_coverage\":" +
        sourceCoverage + "}}";
    progress(callback, "selected", p.events.size(), p.events.size());
    return p;
}
} // namespace
SavedSession saveVerifiedPiece(const std::filesystem::path &path,
                               const VerifiedPiece &input,
                               const PieceProgress &callback) {
    progress(callback, "validating");
    const auto p = sealed(input);
    validate(p, callback);
    progress(callback, "validated");
    Db db(path);
    managed(db.value);
    Transaction tx(db.value);
    auto result = save(db.value, path, p, callback);
    progress(callback, "committing", p.events.size(), p.events.size());
    tx.commit();
    ReadOnlyLibrary reader(path);
    SavedSession verified;
    (void)reader.load(result.id, TimeBasis::observed, {}, &verified);
    return verified;
}
SavedSession freezeObservedPiece(const std::filesystem::path &path,
                                 const FreezePieceRequest &r,
                                 const PieceProgress &callback) {
    progress(callback, "selecting");
    const auto lower = parseUtc(r.observedFrom), upper = parseUtc(r.observedTo);
    if (lower >= upper ||
        static_cast<long double>(upper) - static_cast<long double>(lower) >
            366.L * 86400 * 1000000 ||
        r.seed < 0)
        throw std::invalid_argument(
            "freeze requires an increasing window of at most 366 days");
    for (const auto *filter : {&r.sourceId, &r.speciesId})
        if (*filter && ((*filter)->empty() || (*filter)->size() > 128))
            throw std::invalid_argument("invalid freeze filter");
    Db db(path);
    managed(db.value);
    if (const auto existing = existingFreeze(db.value, path, r, lower, upper)) {
        ReadOnlyLibrary reader(path);
        SavedSession verified;
        (void)reader.load(existing->id, TimeBasis::observed, {}, &verified);
        return verified;
    }
    auto p = sealed(selectObservedPiece(db.value, r, lower, upper, callback));
    progress(callback, "validating");
    validate(p, callback);
    progress(callback, "validated");
    // Recheck the immutable ID only after validation, in the short publication
    // transaction. A competing freeze may have completed while selection ran.
    Transaction publication(db.value);
    SavedSession result;
    if (const auto concurrent = existingFreeze(db.value, path, r, lower, upper))
        result = *concurrent;
    else
        result = save(db.value, path, p, callback);
    progress(callback, "committing", result.eventCount, result.eventCount);
    publication.commit();
    ReadOnlyLibrary reader(path);
    SavedSession verified;
    (void)reader.load(result.id, TimeBasis::observed, {}, &verified);
    return verified;
}
} // namespace atlas
