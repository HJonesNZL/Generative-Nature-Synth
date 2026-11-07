#include <atlas/collection.hpp>
#include <atlas/piece_import.hpp>
#include <atlas/storage.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <vector>
using namespace atlas;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F &&action, const char *message) {
    bool failed{};
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("atlas-import-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
std::vector<std::string> rows(const std::filesystem::path &path,
                              const std::string &sql) {
    sqlite3 *raw{};
    check(sqlite3_open_v2(path.string().c_str(), &raw, SQLITE_OPEN_READWRITE,
                          nullptr) == SQLITE_OK,
          "Test database open failed");
    struct Close {
        sqlite3 *db;
        ~Close() { sqlite3_close(db); }
    } close{raw};
    sqlite3_stmt *statement{};
    if (sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK)
        throw std::runtime_error("Test query failed: " + sql + ": " +
                                 sqlite3_errmsg(raw));
    struct Finalize {
        sqlite3_stmt *value;
        ~Finalize() { sqlite3_finalize(value); }
    } finalize{statement};
    std::vector<std::string> result;
    int step{};
    while ((step = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(statement, 0);
        result.emplace_back(text ? reinterpret_cast<const char *>(text)
                                 : "<NULL>");
    }
    check(step == SQLITE_DONE, "Test query incomplete");
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Pass bundled ATN library path");
        const std::filesystem::path bundled(argv[1]);
        Directory temporary;
        const auto source = temporary.path / "attached.sqlite3";
        std::filesystem::copy_file(bundled, source);
        const auto destination = temporary.path / "managed.sqlite3";
        Collector initialize(destination);
        const std::string id = "d774207b-342c-49bd-a328-5649f9de84ce";
        const auto before =
            rows(source, "SELECT quote(event_json) FROM session_events ORDER "
                         "BY session_id,ordinal");
        rejects([&] { importSavedPiece(source, source, id); },
                "Same-library import accepted");
        rejects([&] { importSavedPiece(source, destination, "latest"); },
                "Implicit latest accepted");
        for (const std::string stage :
             {"verifying", "reading", "saving", "writing", "committing"}) {
            rejects(
                [&] {
                    importSavedPiece(
                        source, destination, id,
                        [&](std::string_view current, std::size_t,
                            std::size_t) { return current != stage; });
                },
                "Cancellation ignored");
            check(rows(destination, "SELECT id FROM sessions").empty(),
                  "Cancellation exposed a partial piece");
        }
        const auto checkpoint = std::filesystem::path(
            destination.string() + ".atlas-import-stage.sqlite3");
        check(std::filesystem::exists(checkpoint),
              "Cancellation lost durable checkpoint");
        check(rows(checkpoint, "SELECT count(*) FROM import_rows") ==
                  std::vector<std::string>{"224"},
              "Persisted checkpoint count is wrong");
        std::size_t resumed{};
        const auto saved = importSavedPiece(
            source, destination, id,
            [&](std::string_view stage, std::size_t done, std::size_t) {
                if (stage == "reading")
                    resumed = done;
                return true;
            });
        check(resumed == 224,
              "Reopened import did not resume persisted ordinal count");
        check(!std::filesystem::exists(checkpoint),
              "Completed checkpoint was not removed");
        check(saved.eventCount == 224 &&
                  saved.metadataIntegrity == "sha256-envelope-v1-verified",
              "ATN import lost count or seal");
        check(importSavedPiece(source, destination, id).contentHash ==
                  saved.contentHash,
              "Idempotent retry failed");
        check(rows(destination, "SELECT id FROM sessions").size() == 1,
              "Retry duplicated piece");
        for (const std::string table : {"events", "revisions", "polls"})
            check(rows(destination, "SELECT * FROM " + table).empty(),
                  "Historical import entered live intake");
        const std::string events =
            "SELECT "
            "quote(ordinal)||':'||quote(original_seq)||':'||quote(event_json) "
            "FROM session_events WHERE session_id='" +
            id + "' ORDER BY ordinal";
        check(rows(source, events) == rows(destination, events),
              "Event order/revisions/attribution changed");
        const std::string metadata =
            "SELECT "
            "quote(name)||quote(state)||quote(origin)||quote(options_json)||"
            "quote(content_hash)||quote(envelope_json)||quote(envelope_hash)||"
            "quote(start_seq)||quote(end_seq)||quote(created_at)||quote(ended_"
            "at)||quote(intended_end_at) FROM sessions WHERE id='" +
            id + "'";
        check(rows(source, metadata) == rows(destination, metadata),
              "Immutable metadata/envelope changed");
        {
            ReadOnlyLibrary reopened(destination);
            check(reopened.load(id, TimeBasis::observed).events().size() == 224,
                  "Restart replay failed");
        }
        check(rows(source, "SELECT quote(event_json) FROM session_events ORDER "
                           "BY session_id,ordinal") == before,
              "Source was modified");
        const auto unmanaged = temporary.path / "unmanaged.sqlite3";
        std::filesystem::copy_file(bundled, unmanaged);
        rejects([&] { importSavedPiece(source, unmanaged, id); },
                "Unmanaged destination accepted");
        rows(source, "UPDATE session_events SET event_json=replace(event_json, "
                     "'ATN', 'BAD') WHERE session_id='" +
                         id + "'");
        rejects([&] { importSavedPiece(source, destination, id); },
                "Tampered event attribution accepted");
        check(rows(destination, "SELECT id FROM sessions").size() == 1,
              "Failed import changed destination");
        std::cout << "piece import tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
