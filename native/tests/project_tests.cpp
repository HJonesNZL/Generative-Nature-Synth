#include "../src/sha256.hpp"
#include <atlas/projects.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
using namespace atlas;
namespace fs = std::filesystem;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Action> void rejected(Action action) {
    try {
        action();
    } catch (const ProjectError &) {
        return;
    }
    throw std::runtime_error("Invalid project operation was accepted");
}
template <class Action> void error(ProjectErrorCode expected, Action action) {
    try {
        action();
    } catch (const ProjectError &failure) {
        check(failure.code() == expected, "Unexpected project error code");
        return;
    }
    throw std::runtime_error("Expected project failure");
}
struct Temporary {
    fs::path directory =
        fs::canonical(fs::temp_directory_path()) /
        ("atlas-project-tests-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { fs::create_directory(directory); }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
    fs::path file(std::string_view name) const { return directory / name; }
};
std::string bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    check(static_cast<bool>(out), "Fixture write failed");
}
class RawDatabase {
  public:
    explicit RawDatabase(const fs::path &path) {
        check(sqlite3_open(path.string().c_str(), &db_) == SQLITE_OK,
              "Fixture database open failed");
    }
    ~RawDatabase() { sqlite3_close(db_); }
    void run(const std::string &sql) {
        check(sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) ==
                  SQLITE_OK,
              "Fixture SQL failed");
    }
    void reseal() {
        sqlite3_stmt *rows{};
        check(sqlite3_prepare_v2(db_,
                                 "SELECT path,name,bytes,sha256 FROM "
                                 "project_entries ORDER BY ordinal",
                                 -1, &rows, nullptr) == SQLITE_OK,
              "Fixture manifest query failed");
        detail::Sha256 hash;
        auto field = [&](std::string_view value) {
            hash.update(std::to_string(value.size()));
            hash.update(":");
            hash.update(value);
        };
        field("atlas-project-v1");
        while (sqlite3_step(rows) == SQLITE_ROW)
            for (int column = 0; column < 4; ++column) {
                const auto *value = sqlite3_column_text(rows, column);
                field({reinterpret_cast<const char *>(value),
                       static_cast<std::size_t>(
                           sqlite3_column_bytes(rows, column))});
            }
        sqlite3_finalize(rows);
        run("UPDATE project_meta SET value='" + hash.hexDigest() + "'");
    }
    void replaceContent(int ordinal, std::string_view content) {
        detail::Sha256 hash;
        hash.update(content);
        sqlite3_stmt *row{};
        check(
            sqlite3_prepare_v2(db_,
                               "UPDATE project_entries SET "
                               "content=?1,bytes=?2,sha256=?3 WHERE ordinal=?4",
                               -1, &row, nullptr) == SQLITE_OK,
            "Fixture update preparation failed");
        const auto digest = hash.hexDigest();
        check(sqlite3_bind_blob(row, 1, content.data(),
                                static_cast<int>(content.size()),
                                SQLITE_TRANSIENT) == SQLITE_OK &&
                  sqlite3_bind_int64(
                      row, 2, static_cast<sqlite3_int64>(content.size())) ==
                      SQLITE_OK &&
                  sqlite3_bind_text(row, 3, digest.data(),
                                    static_cast<int>(digest.size()),
                                    SQLITE_TRANSIENT) == SQLITE_OK &&
                  sqlite3_bind_int(row, 4, ordinal) == SQLITE_OK &&
                  sqlite3_step(row) == SQLITE_DONE,
              "Fixture content update failed");
        sqlite3_finalize(row);
        reseal();
    }

  private:
    sqlite3 *db_{};
};
void makeScene(const fs::path &path) {
    Timeline timeline({}, TimeBasis::observed);
    SoundscapeCapture capture;
    capture.name = "Disposable scene — kōkako";
    capture.sourceLibraryLabel = "Disposable fixture";
    capture.session.id = "fixture:empty";
    capture.session.name = "Synthetic empty source";
    capture.session.state = "frozen";
    capture.session.origin = "demo";
    capture.session.contentHash = std::string(64, '0');
    capture.session.metadataIntegrity = "legacy-metadata-unverified";
    capture.session.observedWindowPolicy = "fixture";
    capture.profiles = {{"", VoiceSettings{}}};
    saveSoundscape(path, captureSoundscape(timeline, std::move(capture)));
}
#if defined(__unix__) || defined(__APPLE__)
template <class Action> void fileGrowthFailure(Action action, bool interrupt) {
    const auto child = fork();
    check(child >= 0, "File-growth fixture could not fork");
    if (child == 0) {
        const rlimit noCore{0, 0}, fileLimit{1024, 1024};
        if (setrlimit(RLIMIT_CORE, &noCore) != 0 ||
            setrlimit(RLIMIT_FSIZE, &fileLimit) != 0 ||
            std::signal(SIGXFSZ, interrupt ? SIG_DFL : SIG_IGN) == SIG_ERR)
            _exit(41);
        try {
            action();
            _exit(42);
        } catch (const ProjectError &failure) {
            _exit(failure.code() == ProjectErrorCode::storage ? 0 : 43);
        } catch (...) {
            _exit(44);
        }
    }
    int status{};
    check(waitpid(child, &status, 0) == child,
          "File-growth fixture could not reap child");
    if (interrupt)
        check(WIFSIGNALED(status) && WTERMSIG(status) == SIGXFSZ,
              "Expected actual file-growth signal interruption");
    else
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "File growth failure was not reported as storage failure");
}
void writeFailureEvidence(const fs::path &root, const fs::path &scene,
                          const fs::path &backup, const fs::path &voices) {
    const auto originalScene = bytes(scene), originalBackup = bytes(backup);
    const auto originalVoices = bytes(voices);
    const auto wal = fs::path(voices.string() + "-wal");
    const auto originalWal = fs::exists(wal) ? bytes(wal) : std::string{};
    const auto failures = root / "write-failures";
    fs::create_directory(failures);
    const auto failedBackup = failures / "backup.atlas-project";
    fileGrowthFailure([&] { createProjectBackup({}, {}, failedBackup); },
                      false);
    const auto failedRestore = failures / "restored";
    fileGrowthFailure([&] { restoreProjectBackup(backup, failedRestore); },
                      false);
    const auto failedExport = failures / "export.atlas-scene";
    fileGrowthFailure([&] { exportSoundscape(scene, failedExport); }, false);
    check(!fs::exists(failedBackup) && !fs::exists(failedRestore) &&
              !fs::exists(failedExport) && fs::is_empty(failures),
          "Write failure published output or retained ordinary staging");
    const auto interrupted = root / "interrupted";
    fs::create_directory(interrupted);
    const auto interruptedBackup = interrupted / "backup.atlas-project";
    fileGrowthFailure([&] { createProjectBackup({}, {}, interruptedBackup); },
                      true);
    check(!fs::exists(interruptedBackup) && !fs::is_empty(interrupted),
          "Interrupted writer published output or did not reach staging");
    check(bytes(scene) == originalScene && bytes(backup) == originalBackup &&
              bytes(voices) == originalVoices &&
              (!fs::exists(wal) ? originalWal.empty()
                                : bytes(wal) == originalWal),
          "Failure injection changed original scene, backup or saved voice "
          "bytes");
    check(inspectProjectBackup(backup).sceneCount == 1,
          "Original backup unusable after injected write failures");
}
#endif
void tests() {
    Temporary fixture;
    const auto voicesPath = fixture.file("voices.sqlite3");
    const auto scene = fixture.file("source.atlas-scene");
    const auto backup = fixture.file("portable.atlas-project");
    makeScene(scene);
    VoiceStore voices(voicesPath);
    voices.setParameter("default", "brightness", .6, VoiceGesture::begin);
    voices.setParameter("default", "brightness", .6, VoiceGesture::end);
    voices.save("default", 0);
    // Keep a live WAL connection: the backup must include the latest committed
    // row even when it has not been checkpointed into the database file.
    RawDatabase wal(voicesPath);
    wal.run("PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0");
    voices.setParameter("default", "brightness", .72, VoiceGesture::begin);
    voices.setParameter("default", "brightness", .72, VoiceGesture::end);
    const auto saved = voices.save("default", 1);
    voices.setParameter("default", "brightness", .13, VoiceGesture::begin);
    voices.setParameter("default", "brightness", .13, VoiceGesture::end);
    write(fixture.file("settings.json"), "private settings excluded");
    fs::create_directory(fixture.file("exports"));
    write(fixture.file("exports/private.wav"), "not project material");
    const auto original = bytes(scene);
    const auto summary = createProjectBackup(voicesPath, {scene}, backup);
    check(summary.sceneCount == 1 && summary.voiceCount == 1 &&
              summary.sceneNames ==
                  std::vector<std::string>{"Disposable scene — kōkako"} &&
              summary.totalBytes > original.size(),
          "Backup summary is wrong");
    const auto archiveBytes = bytes(backup);
    const auto inspected = inspectProjectBackup(backup);
    check(inspected.totalBytes == summary.totalBytes &&
              inspected.sceneNames == summary.sceneNames,
          "Inspection summary changed");
    const auto destination = fixture.file("restored");
    const auto restored = restoreProjectBackup(backup, destination);
    const auto restoredScene =
        fs::directory_iterator(destination / "soundscapes")->path();
    check(restoredScene.stem().string().size() == 32,
          "Restored scene lacks a managed identity");
    check(restored.sceneCount == 1 && bytes(restoredScene) == original &&
              fs::is_empty(destination / "exports") &&
              !fs::exists(destination / "settings.json"),
          "Restore did not preserve only portable material");
    check(std::distance(fs::directory_iterator(destination),
                        fs::directory_iterator{}) == 3,
          "Restore leaked verification files");
    {
        VoiceStore recovered(destination / "voices.sqlite3", {},
                             VoiceStoreAccess::readOnly);
        const auto voice = recovered.getVoice("default");
        check(voice.savedRevision == saved.savedRevision &&
                  voice.settings.brightness == .72 && !voice.dirty,
              "Restore included unsaved draft or omitted WAL commit");
    }
    check(voices.getVoice("default").dirty &&
              voices.getVoice("default").settings.brightness == .13 &&
              bytes(scene) == original && bytes(backup) == archiveBytes,
          "Backup or restore modified source state");
    const auto exported = fixture.file("export.atlas-scene");
    exportSoundscape(scene, exported);
    check(bytes(exported) == original, "Soundscape export changed bytes");
    error(ProjectErrorCode::alreadyExists,
          [&] { exportSoundscape(scene, exported); });
    error(ProjectErrorCode::alreadyExists,
          [&] { createProjectBackup(voicesPath, {scene}, backup); });
    error(ProjectErrorCode::alreadyExists,
          [&] { restoreProjectBackup(backup, destination); });
    fs::create_directory(fixture.file("empty-existing"));
    error(ProjectErrorCode::alreadyExists, [&] {
        restoreProjectBackup(backup, fixture.file("empty-existing"));
    });
    check(fs::is_empty(fixture.file("empty-existing")) &&
              bytes(exported) == original && bytes(backup) == archiveBytes,
          "Conflicts altered existing data");
    const auto emptyBackup = fixture.file("empty.atlas-project");
    check(createProjectBackup({}, {}, emptyBackup).voiceCount == 0 &&
              inspectProjectBackup(emptyBackup).sceneCount == 0,
          "Empty project backup failed");
    restoreProjectBackup(emptyBackup, fixture.file("empty-restored"));
    {
        VoiceStore empty(fixture.file("empty-restored/voices.sqlite3"), {},
                         VoiceStoreAccess::readOnly);
        check(empty.getVoice("default").savedRevision == 0,
              "Empty project fabricated saved voice");
    }
    error(ProjectErrorCode::notFound, [&] {
        createProjectBackup(fixture.file("missing.sqlite3"), {},
                            fixture.file("missing.atlas-project"));
    });
    error(ProjectErrorCode::notFound, [&] {
        createProjectBackup(voicesPath, {fixture.file("missing.atlas-scene")},
                            fixture.file("missing.atlas-project"));
    });
    error(ProjectErrorCode::capacity, [&] {
        createProjectBackup(voicesPath, std::vector<fs::path>(101, scene),
                            fixture.file("large.atlas-project"));
    });
    error(ProjectErrorCode::invalidArgument, [&] {
        createProjectBackup(voicesPath, {scene, scene},
                            fixture.file("duplicate.atlas-project"));
    });
    rejected([&] {
        inspectProjectBackup(fixture.directory /
                             "absent/../portable.atlas-project");
    });
    fs::create_symlink(scene, fixture.file("link.atlas-scene"));
    rejected([&] {
        exportSoundscape(fixture.file("link.atlas-scene"),
                         fixture.file("link-export.atlas-scene"));
    });
    fs::create_symlink(backup, fixture.file("link.atlas-project"));
    rejected([&] { inspectProjectBackup(fixture.file("link.atlas-project")); });
    fs::create_directory_symlink(destination, fixture.file("linked-parent"));
    rejected([&] {
        restoreProjectBackup(backup, fixture.file("linked-parent/new-state"));
    });
    // Each malformed archive is independently copied so rejection cannot be
    // caused by an earlier mutation. Re-seal selected mutations to exercise
    // semantic verification beyond the manifest checksum.
    unsigned mutation{};
    auto malformed = [&](auto mutate) {
        const auto path = fixture.file("tamper-" + std::to_string(mutation++) +
                                       ".atlas-project");
        fs::copy_file(backup, path);
        {
            RawDatabase db(path);
            mutate(db);
        }
        rejected([&] { inspectProjectBackup(path); });
        const auto failedRestore =
            fixture.file("failed-restore-" + std::to_string(mutation));
        rejected([&] { restoreProjectBackup(path, failedRestore); });
        check(!fs::exists(failedRestore),
              "Malformed restore published partial state");
    };
    malformed([](auto &db) { db.run("PRAGMA user_version=99"); });
    malformed([](auto &db) { db.run("UPDATE project_meta SET value='bad'"); });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET sha256='bad' WHERE ordinal=1");
    });
    malformed([](auto &db) {
        db.run("DELETE FROM project_entries WHERE ordinal=0");
        db.reseal();
    });
    malformed([](auto &db) {
        db.run("DELETE FROM project_entries WHERE ordinal=1");
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET path='../escaped' WHERE ordinal=1");
        db.reseal();
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET name='fabricated' WHERE ordinal=1");
        db.reseal();
    });
    malformed([](auto &db) { db.run("CREATE TABLE unexpected(secret TEXT)"); });
    malformed([](auto &db) {
        db.run("CREATE TABLE sqliteXunexpected(secret TEXT)");
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET bytes=67108865 WHERE ordinal=1");
        db.reseal();
    });
    malformed([](auto &db) {
        db.run("INSERT INTO project_entries SELECT "
               "2,'unknown','',bytes,sha256,content FROM project_entries WHERE "
               "ordinal=0");
        db.reseal();
    });
    malformed([](auto &db) { db.replaceContent(0, "not a voice database"); });
    malformed([](auto &db) { db.replaceContent(1, "not a scene database"); });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET bytes=-1 WHERE ordinal=1");
        db.reseal();
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET bytes=9223372036854775807 WHERE "
               "ordinal=1");
        db.reseal();
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET bytes=1.5 WHERE ordinal=1");
    });
    malformed([](auto &db) {
        db.run("UPDATE project_entries SET ordinal=9223372036854775807 WHERE "
               "ordinal=1");
        db.reseal();
    });
    const auto truncated = fixture.file("truncated.atlas-project");
    write(truncated,
          std::string_view(archiveBytes).substr(0, archiveBytes.size() / 2));
    rejected([&] { inspectProjectBackup(truncated); });
    const auto badScene = fixture.file("broken.atlas-scene");
    write(badScene, "invalid");
    rejected([&] {
        exportSoundscape(badScene, fixture.file("broken-export.atlas-scene"));
    });
    check(!fs::exists(fixture.file("broken-export.atlas-scene")) &&
              bytes(backup) == archiveBytes &&
              !fs::exists(fixture.file("escaped")),
          "Failed operation escaped staging or modified backup");
#if defined(__unix__) || defined(__APPLE__)
    writeFailureEvidence(fixture.directory, scene, backup, voicesPath);
#endif
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "Project archive tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
