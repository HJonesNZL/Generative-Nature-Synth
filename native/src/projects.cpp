#include "sha256.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <atlas/projects.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/voices.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sqlite3.h>
#include <sstream>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace atlas {
ProjectError::ProjectError(ProjectErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
namespace {
namespace fs = std::filesystem;
using Code = ProjectErrorCode;
constexpr std::uint64_t sceneLimit = 64 * 1024 * 1024;
constexpr std::uint64_t voiceLimit = 16 * 1024 * 1024;
constexpr std::uint64_t totalLimit = 256 * 1024 * 1024;
constexpr std::uint64_t archiveLimit = totalLimit + 4 * 1024 * 1024;
constexpr auto metaSql = "CREATE TABLE project_meta(key TEXT PRIMARY KEY NOT "
                         "NULL,value TEXT NOT NULL) WITHOUT ROWID";
constexpr auto entriesSql =
    "CREATE TABLE project_entries(ordinal INTEGER PRIMARY KEY NOT NULL,path "
    "TEXT NOT NULL,name TEXT NOT NULL,bytes INTEGER NOT NULL,sha256 TEXT NOT "
    "NULL,content BLOB NOT NULL) WITHOUT ROWID";
[[noreturn]] void fail(Code code, const char *message) {
    throw ProjectError(code, message);
}
std::string pathText(const fs::path &path) {
    const auto bytes = path.u8string();
    std::string result(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());
    if (result.find('\0') != std::string::npos)
        fail(Code::invalidArgument, "Invalid project path");
    return result;
}
void plainPath(const fs::path &path) {
    if (path.empty())
        fail(Code::invalidArgument, "Empty project path");
    (void)pathText(path);
    for (const auto &part : path)
        if (part == "..")
            fail(Code::invalidArgument,
                 "Project paths must not traverse parents");
    const auto absolute = fs::absolute(path).lexically_normal();
    fs::path current;
    for (const auto &part : absolute) {
        current /= part;
        if (fs::is_symlink(fs::symlink_status(current)))
            fail(Code::invalidArgument,
                 "Project paths must not contain symbolic links");
    }
}
void absent(const fs::path &path) {
    plainPath(path);
    if (fs::symlink_status(path).type() != fs::file_type::not_found)
        fail(Code::alreadyExists, "Project destination already exists");
    if (!fs::is_directory(path.parent_path().empty() ? fs::path(".")
                                                     : path.parent_path()))
        fail(Code::notFound, "Project destination parent is missing");
}
void regular(const fs::path &path, std::uint64_t limit) {
    plainPath(path);
    const auto status = fs::symlink_status(path);
    if (status.type() == fs::file_type::not_found)
        fail(Code::notFound, "Project source is missing");
    if (!fs::is_regular_file(status))
        fail(Code::invalidArgument, "Project source must be a regular file");
    const auto size = fs::file_size(path);
    if (size == 0)
        fail(Code::corrupt, "Project source is empty");
    if (size > limit)
        fail(Code::capacity, "Project source exceeds byte capacity");
}
struct Temporary {
    fs::path directory;
    explicit Temporary(const fs::path &parent) {
        static std::atomic<unsigned long long> sequence{};
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory =
                parent / (".atlas-project-" +
                          std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()) +
                          "-" + std::to_string(sequence++));
            if (fs::create_directory(directory))
                return;
        }
        fail(Code::storage, "Unable to reserve project staging directory");
    }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
};
class Database {
  public:
    explicit Database(const fs::path &path, bool write = false)
        : writing_(write) {
        const auto flags = (write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                  : SQLITE_OPEN_READONLY) |
                           SQLITE_OPEN_NOMUTEX;
        if (sqlite3_open_v2(pathText(path).c_str(), &db_, flags, nullptr) !=
            SQLITE_OK) {
            if (db_)
                sqlite3_close(db_);
            db_ = nullptr;
            fail(Code::storage, "Cannot open project database");
        }
        sqlite3_limit(db_, SQLITE_LIMIT_LENGTH,
                      static_cast<int>(sceneLimit + 32768));
        sqlite3_limit(db_, SQLITE_LIMIT_SQL_LENGTH, 32768);
        sqlite3_limit(db_, SQLITE_LIMIT_COLUMN, 32);
        sqlite3_busy_timeout(db_, 1000);
        sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    }
    ~Database() {
        if (db_)
            sqlite3_close(db_);
    }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    sqlite3 *get() const { return db_; }
    void run(const char *sql) const {
        if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
            fail(writing_ ? Code::storage : Code::corrupt,
                 "Project database operation failed");
    }

  private:
    sqlite3 *db_{};
    bool writing_{};
};
std::string readBytes(const fs::path &path, std::uint64_t limit) {
    regular(path, limit);
    const auto size = fs::file_size(path);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof())
        fail(Code::storage, "Project source changed or could not be read");
    return bytes;
}
void writeBytes(const fs::path &path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        fail(Code::storage, "Project staging write failed");
    output.close();
    if (!output)
        fail(Code::storage, "Project staging close failed");
}
std::string digest(std::string_view bytes) {
    detail::Sha256 hash;
    hash.update(bytes);
    return hash.hexDigest();
}
void publishFile(const fs::path &source, const fs::path &destination) {
    std::error_code error;
    fs::create_hard_link(source, destination, error);
    if (!error)
        return;
    if (fs::symlink_status(destination).type() != fs::file_type::not_found)
        fail(Code::alreadyExists,
             "Project destination appeared during publication");
    fail(Code::storage,
         "Project publication requires same-filesystem hard-link support");
}
void publishDirectory(const fs::path &source, const fs::path &destination) {
    // Standard filesystem::rename may replace an existing empty directory.
    // Keep the exclusive operation at this small platform boundary.
#if defined(__APPLE__)
    const bool success =
        renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2)
    const bool success = syscall(SYS_renameat2, AT_FDCWD, source.c_str(),
                                 AT_FDCWD, destination.c_str(), 1U) == 0;
#elif defined(_WIN32)
    const bool success =
        MoveFileExW(source.c_str(), destination.c_str(), 0) != 0;
#else
    const bool success = false;
#endif
    if (success)
        return;
    if (fs::symlink_status(destination).type() != fs::file_type::not_found)
        fail(Code::alreadyExists,
             "Project destination appeared during restore");
    fail(Code::storage,
         "Exclusive atomic directory publication is unavailable");
}
std::size_t voiceCount(const fs::path &path) {
    try {
        VoiceStore voices(path, {}, VoiceStoreAccess::readOnly);
        const auto saved = voices.voices();
        return static_cast<std::size_t>(
            std::count_if(saved.begin(), saved.end(), [](const auto &voice) {
                return voice.savedRevision != 0;
            }));
    } catch (const VoiceStoreError &error) {
        if (error.code() == VoiceStoreErrorCode::unsupportedSchema)
            fail(Code::unsupportedSchema, "Unsupported saved-voice database");
        fail(Code::corrupt, "Invalid saved-voice database");
    }
}
void snapshotVoices(const fs::path &source, const fs::path &destination) {
    if (source.empty()) {
        VoiceStore empty(destination);
        return;
    }
    regular(source, voiceLimit);
    Database from(source), to(destination, true);
    to.run("PRAGMA page_size=4096; PRAGMA max_page_count=4096");
    auto *backup = sqlite3_backup_init(to.get(), "main", from.get(), "main");
    if (!backup)
        fail(Code::storage, "Saved-voice snapshot could not start");
    const auto result = sqlite3_backup_step(backup, -1);
    const auto finished = sqlite3_backup_finish(backup);
    if (result != SQLITE_DONE || finished != SQLITE_OK)
        fail(Code::storage, "Saved-voice snapshot failed");
    to.run("PRAGMA journal_mode=DELETE");
}
std::string scenePath(std::size_t index) {
    std::ostringstream name;
    name << "soundscapes/scene-" << std::setfill('0') << std::setw(3) << index
         << ".atlas-scene";
    return name.str();
}
struct Entry {
    std::string path, name, hash;
    std::uint64_t bytes{};
};
void sealField(detail::Sha256 &hash, std::string_view field) {
    hash.update(std::to_string(field.size()));
    hash.update(":");
    hash.update(field);
}
std::string manifest(const std::vector<Entry> &entries) {
    detail::Sha256 hash;
    sealField(hash, "atlas-project-v1");
    for (const auto &entry : entries) {
        sealField(hash, entry.path);
        sealField(hash, entry.name);
        sealField(hash, std::to_string(entry.bytes));
        sealField(hash, entry.hash);
    }
    return hash.hexDigest();
}
void insertEntry(sqlite3 *db, std::size_t ordinal, const Entry &entry,
                 std::string_view bytes) {
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(
            db, "INSERT INTO project_entries VALUES(?1,?2,?3,?4,?5,?6)", -1,
            &raw, nullptr) != SQLITE_OK)
        fail(Code::storage, "Project entry preparation failed");
    struct Finalize {
        sqlite3_stmt *value;
        ~Finalize() { sqlite3_finalize(value); }
    } guard{raw};
    if (sqlite3_bind_int64(raw, 1, static_cast<sqlite3_int64>(ordinal)) !=
            SQLITE_OK ||
        sqlite3_bind_text(raw, 2, entry.path.data(),
                          static_cast<int>(entry.path.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(raw, 3, entry.name.data(),
                          static_cast<int>(entry.name.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(raw, 4, static_cast<sqlite3_int64>(entry.bytes)) !=
            SQLITE_OK ||
        sqlite3_bind_text(raw, 5, entry.hash.data(),
                          static_cast<int>(entry.hash.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(raw, 6, bytes.data(), static_cast<int>(bytes.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(raw) != SQLITE_DONE)
        fail(Code::storage, "Project entry write failed");
}
std::string sceneName(const fs::path &path) {
    try {
        return loadSoundscape(path).capture().name;
    } catch (const SoundscapeError &error) {
        if (error.code() == SoundscapeErrorCode::unsupportedSchema)
            fail(Code::unsupportedSchema, "Unsupported soundscape in project");
        fail(Code::corrupt, "Invalid soundscape in project");
    }
}
ProjectSummary verifyAndExtract(const fs::path &path,
                                const fs::path &destination) {
    regular(path, archiveLimit);
    Temporary archiveCopy(destination);
    const auto isolatedArchive = archiveCopy.directory / "archive.sqlite3";
    writeBytes(isolatedArchive, readBytes(path, archiveLimit));
    Database db(isolatedArchive);
    db.run("BEGIN");
    {
        detail::Statement header(db.get(),
                                 "SELECT application_id,user_version FROM "
                                 "pragma_application_id,pragma_user_version");
        if (!header.next() || header.integer(0) != 0x4154504a ||
            header.integer(1) != 1)
            fail(Code::unsupportedSchema,
                 "Unsupported project archive application or schema");
    }
    {
        detail::Statement schema(
            db.get(), "SELECT name,sql FROM sqlite_schema WHERE name "
                      "NOT GLOB 'sqlite_*' ORDER BY name");
        if (!schema.next() || schema.text(0) != "project_entries" ||
            schema.text(1) != entriesSql || !schema.next() ||
            schema.text(0) != "project_meta" || schema.text(1) != metaSql ||
            schema.next())
            fail(Code::corrupt, "Unexpected project archive schema");
        detail::Statement integrity(db.get(), "PRAGMA quick_check(1)");
        if (!integrity.next() || integrity.text(0) != "ok" || integrity.next())
            fail(Code::corrupt, "Project archive integrity check failed");
    }
    std::string expectedManifest;
    {
        detail::Statement meta(
            db.get(), "SELECT key,value FROM project_meta ORDER BY key");
        if (!meta.next() || meta.text(0) != "manifest_sha256")
            fail(Code::corrupt, "Project manifest is missing");
        expectedManifest = meta.text(1);
        if (meta.next())
            fail(Code::corrupt, "Unexpected project metadata");
    }
    fs::create_directory(destination / "soundscapes");
    fs::create_directory(destination / "exports");
    ProjectSummary summary;
    std::vector<Entry> entries;
    std::set<std::string> restoredIds;
    sqlite3_stmt *raw{};
    if (sqlite3_prepare_v2(db.get(),
                           "SELECT ordinal,path,name,bytes,sha256,content FROM "
                           "project_entries ORDER BY ordinal",
                           -1, &raw, nullptr) != SQLITE_OK)
        fail(Code::corrupt, "Project entries are unreadable");
    struct Finalize {
        sqlite3_stmt *value;
        ~Finalize() { sqlite3_finalize(value); }
    } guard{raw};
    int result{};
    while ((result = sqlite3_step(raw)) == SQLITE_ROW) {
        if (entries.size() >= 101)
            fail(Code::capacity, "Project exceeds 100 soundscapes");
        const auto text = [raw](int column) {
            if (sqlite3_column_type(raw, column) != SQLITE_TEXT ||
                sqlite3_column_bytes(raw, column) > 1024)
                fail(Code::corrupt, "Invalid project entry text");
            return std::string(
                reinterpret_cast<const char *>(
                    sqlite3_column_text(raw, column)),
                static_cast<std::size_t>(sqlite3_column_bytes(raw, column)));
        };
        if (sqlite3_column_type(raw, 0) != SQLITE_INTEGER ||
            sqlite3_column_int64(raw, 0) !=
                static_cast<sqlite3_int64>(entries.size()) ||
            sqlite3_column_type(raw, 3) != SQLITE_INTEGER ||
            sqlite3_column_type(raw, 5) != SQLITE_BLOB)
            fail(Code::corrupt, "Invalid project entry types or ordering");
        Entry entry{text(1), text(2), text(4),
                    static_cast<std::uint64_t>(sqlite3_column_int64(raw, 3))};
        const bool voices = entries.empty();
        if (entry.path !=
                (voices ? "voices.sqlite3" : scenePath(entries.size() - 1)) ||
            (voices && !entry.name.empty()))
            fail(Code::corrupt, "Unexpected project entry path");
        const auto limit = voices ? voiceLimit : sceneLimit;
        if (entry.bytes == 0 || entry.bytes > limit ||
            summary.totalBytes + entry.bytes > totalLimit)
            fail(Code::capacity, "Project entry byte capacity exceeded");
        const auto size = sqlite3_column_bytes(raw, 5);
        if (static_cast<std::uint64_t>(size) != entry.bytes)
            fail(Code::corrupt, "Project entry length mismatch");
        const std::string_view bytes(
            reinterpret_cast<const char *>(sqlite3_column_blob(raw, 5)),
            static_cast<std::size_t>(size));
        if (digest(bytes) != entry.hash)
            fail(Code::corrupt, "Project entry checksum mismatch");
        auto output = destination / entry.path;
        if (!voices) {
            const auto localId = digest(expectedManifest + ":" +
                                        std::to_string(entries.size() - 1))
                                     .substr(0, 32);
            if (!restoredIds.insert(localId).second)
                fail(Code::corrupt, "Restored soundscape identities collide");
            output = destination / "soundscapes" / (localId + ".atlas-scene");
        }
        writeBytes(output, bytes);
        if (voices)
            summary.voiceCount = voiceCount(output);
        else {
            const auto name = sceneName(output);
            if (entry.name != name)
                fail(Code::corrupt, "Project soundscape name mismatch");
            summary.sceneNames.push_back(name);
            ++summary.sceneCount;
        }
        summary.totalBytes += entry.bytes;
        entries.push_back(std::move(entry));
    }
    if (result != SQLITE_DONE || entries.empty() ||
        manifest(entries) != expectedManifest)
        fail(Code::corrupt,
             "Project manifest checksum mismatch or entries missing");
    return summary;
}
template <class Action> auto checked(Action action) {
    try {
        return action();
    } catch (const ProjectError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::storage, "Project operation failed");
    }
}
} // namespace
ProjectSummary createProjectBackup(const fs::path &voicesPath,
                                   const std::vector<fs::path> &scenePaths,
                                   const fs::path &outputPath) {
    return checked([&] {
        absent(outputPath);
        if (outputPath.extension() != ".atlas-project")
            fail(Code::invalidArgument,
                 "Choose a new .atlas-project archive file");
        if (scenePaths.size() > 100)
            fail(Code::capacity, "Project exceeds 100 soundscapes");
        Temporary staging(outputPath.parent_path().empty()
                              ? fs::path(".")
                              : outputPath.parent_path());
        const auto voices = staging.directory / "voices.sqlite3";
        snapshotVoices(voicesPath, voices);
        (void)voiceCount(voices);
        const auto archive = staging.directory / "backup.atlas-project";
        {
            Database db(archive, true);
            db.run(
                "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA "
                "application_id=1096044618; PRAGMA user_version=1; BEGIN "
                "IMMEDIATE");
            db.run(metaSql);
            db.run(entriesSql);
            std::vector<Entry> entries;
            std::uint64_t total{};
            auto add = [&](const fs::path &source, std::string name,
                           std::uint64_t limit) {
                const auto bytes = readBytes(source, limit);
                total += bytes.size();
                if (total > totalLimit)
                    fail(Code::capacity, "Project exceeds total byte capacity");
                const auto ordinal = entries.size();
                Entry entry{ordinal == 0 ? "voices.sqlite3"
                                         : scenePath(ordinal - 1),
                            std::move(name), digest(bytes), bytes.size()};
                insertEntry(db.get(), ordinal, entry, bytes);
                entries.push_back(std::move(entry));
            };
            add(voices, "", voiceLimit);
            std::set<fs::path> selected;
            for (const auto &source : scenePaths) {
                plainPath(source);
                if (!selected.insert(fs::absolute(source).lexically_normal())
                         .second)
                    fail(Code::invalidArgument,
                         "Duplicate soundscape selection");
                const auto copy = staging.directory / "selected.atlas-scene";
                writeBytes(copy, readBytes(source, sceneLimit));
                add(copy, sceneName(copy), sceneLimit);
                fs::remove(copy);
            }
            const auto sql =
                "INSERT INTO project_meta VALUES('manifest_sha256','" +
                manifest(entries) + "')";
            db.run(sql.c_str());
            db.run("COMMIT");
        }
        Temporary verification(staging.directory);
        auto summary = verifyAndExtract(archive, verification.directory);
        publishFile(archive, outputPath);
        return summary;
    });
}
ProjectSummary inspectProjectBackup(const fs::path &backupPath) {
    return checked([&] {
        Temporary staging(fs::canonical(fs::temp_directory_path()));
        return verifyAndExtract(backupPath, staging.directory);
    });
}
ProjectSummary restoreProjectBackup(const fs::path &backupPath,
                                    const fs::path &newStateDirectory) {
    return checked([&] {
        absent(newStateDirectory);
        Temporary staging(newStateDirectory.parent_path().empty()
                              ? fs::path(".")
                              : newStateDirectory.parent_path());
        auto summary = verifyAndExtract(backupPath, staging.directory);
        publishDirectory(staging.directory, newStateDirectory);
        return summary;
    });
}
void exportSoundscape(const fs::path &source, const fs::path &destination) {
    checked([&] {
        absent(destination);
        if (destination.extension() != ".atlas-scene")
            fail(Code::invalidArgument,
                 "Choose a new .atlas-scene export file");
        Temporary staging(destination.parent_path().empty()
                              ? fs::path(".")
                              : destination.parent_path());
        const auto copy = staging.directory / "scene.atlas-scene";
        writeBytes(copy, readBytes(source, sceneLimit));
        (void)sceneName(copy);
        publishFile(copy, destination);
    });
}
} // namespace atlas
