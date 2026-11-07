#include "sqlite_statement.hpp"
#include <atlas/piece_import.hpp>
#include <atlas/piece_writer.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <sqlite3.h>
namespace atlas {
namespace {
void report(const PieceImportProgress &progress, std::string_view stage,
            std::size_t completed, std::size_t total) {
    if (progress && !progress(stage, completed, total))
        throw PieceCancelled();
}
struct Connection {
    sqlite3 *db{};
    explicit Connection(const std::filesystem::path &path) {
        const auto utf8 = path.u8string();
        const std::string filename(reinterpret_cast<const char *>(utf8.data()),
                                   utf8.size());
        if (sqlite3_open_v2(filename.c_str(), &db,
                            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error("Cannot read attached import library.");
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 65536);
        if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error("Cannot begin import snapshot.");
        }
    }
    ~Connection() {
        if (db)
            sqlite3_close(db);
    }
};
void execute(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Import staging operation failed.");
}
void write(sqlite3 *db, const char *sql,
           const std::vector<std::string> &values) {
    sqlite3_stmt *statement{};
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error("Import staging query failed.");
    struct Finalize {
        sqlite3_stmt *value;
        ~Finalize() { sqlite3_finalize(value); }
    } finalize{statement};
    for (std::size_t i = 0; i < values.size(); ++i)
        if (sqlite3_bind_text(statement, static_cast<int>(i + 1),
                              values[i].data(),
                              static_cast<int>(values[i].size()),
                              SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("Import staging bind failed.");
    if (sqlite3_step(statement) != SQLITE_DONE)
        throw std::runtime_error("Import staging write failed.");
}
constexpr std::uintmax_t libraryBudget = 512ULL * 1024 * 1024;
constexpr std::uintmax_t stagingBudget = 80ULL * 1024 * 1024;
std::uintmax_t sizeIfPresent(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}
void checkBudget(const std::filesystem::path &destination,
                 const std::filesystem::path &staging,
                 std::uintmax_t remainingBytes) {
    const auto stageBytes = sizeIfPresent(staging);
    if (stageBytes > stagingBudget ||
        sizeIfPresent(destination) +
                sizeIfPresent(destination.string() + "-wal") + stageBytes +
                sizeIfPresent(staging.string() + "-journal") + remainingBytes >
            libraryBudget)
        throw std::runtime_error(
            "Managed library import exceeds the 512 MiB storage budget.");
}
void requireManaged(const std::filesystem::path &destination) {
    if (!std::filesystem::is_regular_file(
            std::filesystem::symlink_status(destination)))
        throw std::invalid_argument(
            "Import destination must be an existing managed library.");
    sqlite3 *raw{};
    if (sqlite3_open_v2(destination.string().c_str(), &raw,
                        SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (raw)
            sqlite3_close(raw);
        throw std::runtime_error("Cannot inspect managed import destination.");
    }
    struct Close {
        sqlite3 *db;
        ~Close() { sqlite3_close(db); }
    } close{raw};
    detail::Statement marker(
        raw, "SELECT value FROM metadata WHERE key='native_collection_owner'");
    detail::Statement version(raw, "PRAGMA user_version");
    if (!marker.next() || marker.text(0) != "animal_atlas_native_v1" ||
        !version.next() || version.integer(0) != 3)
        throw std::invalid_argument(
            "Import destination is not a managed library.");
}
std::filesystem::file_status checkedStatus(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && status.type() != std::filesystem::file_type::not_found)
        throw std::runtime_error("Cannot inspect import checkpoint path.");
    if (std::filesystem::exists(status) &&
        !std::filesystem::is_regular_file(status))
        throw std::runtime_error("Import checkpoint and journal must be "
                                 "regular files, without symlinks.");
    return status;
}
bool emptyCheckpoint(sqlite3 *db) {
    detail::Statement schema(db, "SELECT count(*) FROM sqlite_master");
    if (!schema.next())
        throw std::runtime_error("Import checkpoint schema unavailable.");
    if (schema.integer(0) != 0)
        return false;
    detail::Statement version(db, "PRAGMA user_version");
    detail::Statement application(db, "PRAGMA application_id");
    if (!version.next() || !application.next() || version.integer(0) != 0 ||
        application.integer(0) != 0)
        throw std::runtime_error(
            "Refusing an unowned import checkpoint header.");
    return true;
}
void requireCheckpointOwner(sqlite3 *db) {
    detail::Statement marker(db, "SELECT marker FROM import_identity");
    if (!marker.next() || marker.text(0) != "animal_atlas_import_stage_v1" ||
        marker.next())
        throw std::runtime_error("Refusing unrelated import staging file.");
}
// A read-only ownership probe cannot recover a hot rollback journal. Recover a
// bounded disposable copy first; never recover an unrelated original database.
void validateRecoverableCheckpoint(const std::filesystem::path &destination,
                                   const std::filesystem::path &stage,
                                   bool journalExists) {
    const auto journal = std::filesystem::path(stage.string() + "-journal");
    const auto bytes =
        std::filesystem::file_size(stage) +
        (journalExists ? std::filesystem::file_size(journal) : 0);
    if (bytes > stagingBudget)
        throw std::runtime_error("Import recovery scratch exceeds capacity.");
    checkBudget(destination, stage, bytes);
    const auto root =
        std::filesystem::canonical(std::filesystem::temp_directory_path());
    if (std::filesystem::space(root).available < bytes + 64ULL * 1024 * 1024)
        throw std::runtime_error(
            "Import recovery has insufficient temporary storage.");
    static std::atomic<std::uint64_t> sequence{};
    const auto directory =
        root /
        ("atlas-import-recovery-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(++sequence));
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("Cannot allocate import recovery scratch.");
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    const auto copy = directory / "checkpoint.sqlite3";
    std::filesystem::copy_file(stage, copy);
    if (journalExists)
        std::filesystem::copy_file(journal, copy.string() + "-journal");
    sqlite3 *db{};
    if (sqlite3_open_v2(copy.string().c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW,
                        nullptr) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        throw std::runtime_error("Cannot inspect import recovery scratch.");
    }
    struct Close {
        sqlite3 *db;
        ~Close() { sqlite3_close(db); }
    } close{db};
    sqlite3_busy_timeout(db, 0);
    if (!emptyCheckpoint(db))
        requireCheckpointOwner(db);
}
class Stage {
  public:
    sqlite3 *db{};
    std::filesystem::path path;
    std::size_t count{};
    Stage(const std::filesystem::path &destination,
          const std::filesystem::path &source, const VerifiedPiece &piece)
        : path(std::filesystem::weakly_canonical(destination).string() +
               ".atlas-import-stage.sqlite3") {
        const auto status = checkedStatus(path);
        const auto journal = std::filesystem::path(path.string() + "-journal");
        const auto journalStatus = checkedStatus(journal);
        const bool exists = std::filesystem::exists(status);
        const bool journalExists = std::filesystem::exists(journalStatus);
        if (!exists && journalExists)
            throw std::runtime_error(
                "Refusing an orphan import checkpoint journal.");
        if (exists)
            validateRecoverableCheckpoint(destination, path, journalExists);
        checkBudget(destination, path, 0);
        if (sqlite3_open_v2(path.string().c_str(), &db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW |
                                (exists ? 0 : SQLITE_OPEN_CREATE),
                            nullptr) != SQLITE_OK) {
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error("Cannot open import checkpoint.");
        }
        try {
            sqlite3_busy_timeout(db, 5000);
            execute(db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; "
                        "PRAGMA max_page_count=20480; BEGIN IMMEDIATE");
            if (emptyCheckpoint(db))
                execute(
                    db,
                    "CREATE TABLE import_identity(marker TEXT,source TEXT,id "
                    "TEXT,content_hash TEXT,envelope_hash TEXT); CREATE TABLE "
                    "import_rows(ordinal INTEGER PRIMARY KEY,original_seq "
                    "INTEGER NOT NULL,event_json TEXT NOT NULL)");
            else
                requireCheckpointOwner(db);
            const auto canonical =
                std::filesystem::weakly_canonical(source).string();
            // Paths can exceed the replay adapter's ID binding capacity.
            detail::Statement identity(
                db, "SELECT source,id,content_hash,envelope_hash FROM "
                    "import_identity");
            const bool same = identity.next() &&
                              identity.text(0) == canonical &&
                              identity.text(1) == piece.id &&
                              identity.text(2) == piece.contentHash &&
                              identity.text(3) == piece.envelopeHash;
            if (!same) {
                execute(db,
                        "DELETE FROM import_rows; DELETE FROM import_identity");
                write(db,
                      "INSERT INTO import_identity "
                      "VALUES('animal_atlas_import_stage_v1',?1,?2,?3,?4)",
                      {canonical, piece.id, piece.contentHash,
                       piece.envelopeHash});
            }
            detail::Statement rows(
                db,
                "SELECT count(*),coalesce(max(ordinal),-1) FROM import_rows");
            if (!rows.next() || rows.integer(0) < 0 ||
                rows.integer(0) > 50000 ||
                rows.integer(1) != rows.integer(0) - 1)
                throw std::runtime_error("Import checkpoint order is invalid.");
            count = static_cast<std::size_t>(rows.integer(0));
            execute(db, "COMMIT; BEGIN IMMEDIATE");
        } catch (...) {
            sqlite3_close(db);
            db = nullptr;
            throw;
        }
    }
    ~Stage() {
        if (db)
            sqlite3_close(db);
    }
    void append(std::size_t ordinal, const VerifiedPieceEvent &event) {
        if (ordinal < count) {
            detail::Statement row(db, ("SELECT original_seq,event_json FROM "
                                       "import_rows WHERE ordinal=" +
                                       std::to_string(ordinal))
                                          .c_str());
            if (!row.next() || row.integer(0) != event.originalSequence ||
                row.text(1) != event.eventJson)
                throw std::runtime_error(
                    "Import checkpoint differs from verified source.");
            return;
        }
        write(db,
              "INSERT INTO import_rows VALUES(CAST(?1 AS INTEGER),CAST(?2 AS "
              "INTEGER),?3)",
              {std::to_string(ordinal), std::to_string(event.originalSequence),
               event.eventJson});
    }
    void checkpoint(std::size_t completed) {
        execute(db, "COMMIT; BEGIN IMMEDIATE");
        count = completed;
    }
    bool finish() noexcept {
        const bool rolledBack = sqlite3_exec(db, "ROLLBACK", nullptr, nullptr,
                                             nullptr) == SQLITE_OK;
        if (sqlite3_close(db) != SQLITE_OK)
            return false;
        db = nullptr;
        std::error_code error;
        std::filesystem::remove(path, error);
        return rolledBack && !error;
    }
};
} // namespace
SavedSession importSavedPiece(const std::filesystem::path &source,
                              const std::filesystem::path &destination,
                              std::string_view sessionId,
                              const PieceImportProgress &progress) {
    if (sessionId.empty() || sessionId == "latest" || sessionId.size() > 128 ||
        sessionId.find('\0') != std::string_view::npos)
        throw std::invalid_argument(
            "Choose one explicit saved piece to import.");
    std::error_code error;
    if (std::filesystem::equivalent(source, destination, error) ||
        std::filesystem::weakly_canonical(source) ==
            std::filesystem::weakly_canonical(destination))
        throw std::invalid_argument(
            "Import requires a separate attached source library.");
    requireManaged(destination);
    report(progress, "verifying", 0, 1);
    ReadOnlyLibrary reader(source);
    const auto preview = reader.session(sessionId);
    const auto requireOriginalSeal = [](const SavedSession &session) {
        if (session.metadataIntegrity != "sha256-envelope-v1-verified")
            throw std::runtime_error(
                "Import requires verified original metadata (v1 seal). Legacy "
                "pieces remain available for read-only replay.");
    };
    requireOriginalSeal(preview);
    if (preview.eventCount > 50000)
        throw std::runtime_error("Import piece exceeds native event capacity.");
    SavedSession verified;
    reader.load(sessionId, TimeBasis::observed, {}, &verified);
    requireOriginalSeal(verified);
    Connection connection(source);
    auto *db = connection.db;
    detail::Statement version(db, "PRAGMA user_version");
    if (!version.next())
        throw std::runtime_error("Import schema unavailable.");
    if (version.integer(0) != 3)
        throw std::runtime_error(
            "Import requires verified original metadata (v1 seal). Legacy "
            "pieces remain available for read-only replay.");
    const std::string columns =
        "SELECT "
        "id,name,state,created_at,ended_at,start_seq,end_seq,intended_end_at,"
        "origin,options_json,event_count,content_hash";
    detail::Statement metadata(
        db, (columns + ",envelope_version,envelope_json,envelope_hash FROM "
                       "sessions WHERE id=?")
                .c_str());
    metadata.bind(sessionId);
    if (!metadata.next())
        throw std::runtime_error("Import piece no longer exists.");
    VerifiedPiece piece;
    piece.id = metadata.text(0);
    piece.name = metadata.text(1);
    piece.state = metadata.text(2);
    piece.createdAt = metadata.text(3);
    piece.endedAt = metadata.optionalText(4);
    piece.startSequence = metadata.integer(5);
    if (metadata.null(6))
        throw std::runtime_error("Import piece is not finalized.");
    piece.endSequence = metadata.integer(6);
    piece.intendedEndAt = metadata.optionalText(7);
    piece.origin = metadata.text(8);
    piece.optionsJson = metadata.text(9);
    const auto count = metadata.integer(10);
    piece.contentHash = metadata.text(11);
    if (count < 0 || count > 50000 ||
        static_cast<std::size_t>(count) != verified.eventCount ||
        piece.contentHash != verified.contentHash ||
        piece.state != verified.state)
        throw std::runtime_error(
            "Import source changed; choose the piece again.");
    if (metadata.integer(12) != 1 || metadata.null(13) || metadata.null(14))
        throw std::runtime_error(
            "Import requires verified original metadata (v1 seal). Legacy "
            "pieces remain available for read-only replay.");
    piece.envelopeVersion = 1;
    piece.envelopeJson = metadata.text(13);
    piece.envelopeHash = metadata.text(14);
    if (piece.envelopeJson.empty() || piece.envelopeHash.empty())
        throw std::runtime_error(
            "Import requires a complete original metadata seal.");
    Stage stage(destination, source, piece);
    if (stage.count > static_cast<std::size_t>(count))
        throw std::runtime_error("Import checkpoint exceeds piece length.");
    const auto resumed = stage.count;
    report(progress, "reading", resumed, static_cast<std::size_t>(count));
    piece.events.reserve(static_cast<std::size_t>(count));
    detail::Statement rows(
        db, "SELECT ordinal,original_seq,event_json FROM session_events WHERE "
            "session_id=? ORDER BY ordinal LIMIT 50001");
    rows.bind(sessionId);
    std::size_t bytes{};
    while (rows.next()) {
        if (piece.events.size() >= 50000 ||
            rows.integer(0) != static_cast<std::int64_t>(piece.events.size()))
            throw std::runtime_error(
                "Import piece order exceeds capacity or is invalid.");
        auto json = rows.text(2);
        bytes += json.size();
        if (json.size() > 16384 || bytes > 64 * 1024 * 1024)
            throw std::runtime_error("Import piece exceeds native capacity.");
        piece.events.push_back({rows.integer(1), std::move(json)});
        stage.append(piece.events.size() - 1, piece.events.back());
        if (piece.events.size() > resumed && piece.events.size() % 256 == 0) {
            checkBudget(destination, stage.path, bytes);
            stage.checkpoint(piece.events.size());
            report(progress, "reading", piece.events.size(),
                   static_cast<std::size_t>(count));
        }
    }
    if (piece.events.size() != static_cast<std::size_t>(count))
        throw std::runtime_error("Import piece count changed.");
    stage.checkpoint(piece.events.size());
    checkBudget(destination, stage.path,
                bytes + piece.events.size() * 256 + 16 * 1024 * 1024);
    report(progress, "saving", piece.events.size(), piece.events.size());
    // Writer independently verifies this complete snapshot, preserves its seal,
    // validates managed ownership, and commits atomically with exact-ID retry.
    const auto saved = saveVerifiedPiece(destination, piece, progress);
    if (!stage.finish() && progress) {
        // Publication has committed. Cleanup cannot convert a completed save
        // into failure or cancellation; retained staging can be retried safely.
        try {
            (void)progress("saved_checkpoint_retained", piece.events.size(),
                           piece.events.size());
        } catch (
            ...) { /* A notification cannot undo the completed publication. */
        }
    }
    return saved;
}
} // namespace atlas
