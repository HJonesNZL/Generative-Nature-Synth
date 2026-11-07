#include <atlas/storage.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace atlas;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class Error, class Function> void rejects(Function&& function, const char* message) {
    bool rejected{};
    try { function(); } catch (const Error&) { rejected = true; }
    check(rejected, message);
}
class Database {
public:
    explicit Database(const std::filesystem::path& path) {
        if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK)
            throw std::runtime_error("Fixture database open failed");
    }
    ~Database() { sqlite3_close(db_); }
    void run(const std::string& sql) {
        char* error{};
        const auto result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
        const auto message = error ? std::string(error) : "Fixture database write failed";
        sqlite3_free(error);
        if (result != SQLITE_OK) throw std::runtime_error(message);
    }
private:
    sqlite3* db_{};
};
struct DisposableDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("atlas-library-browsing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    DisposableDirectory() { std::filesystem::create_directories(path); }
    ~DisposableDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
std::string id(std::size_t index) {
    std::ostringstream output;
    output << "piece-" << std::setw(5) << std::setfill('0') << index;
    return output.str();
}
std::string created(std::size_t index) {
    return "2026-11-0" + std::to_string(index % 3 + 1) + "T00:00:00Z";
}
void insert(Database& db, const std::string& sessionId, const std::string& date, const std::string& state = "frozen") {
    db.run("INSERT INTO sessions VALUES('" + sessionId + "','Synthetic saved material','" + state +
        "','demo',0,'{\"timeline_bounds\":{\"observed\":[\"2026-11-01T00:00:00Z\",\"2026-11-01T00:00:02Z\"],"
        "\"arrival\":[\"2026-11-01T00:00:00Z\",\"2026-11-01T00:00:03Z\"]},\"capture_truncated\":true}',"
        "'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855','" + date + "')");
}
void fixture(const std::filesystem::path& path, std::size_t count) {
    Database db(path);
    db.run("PRAGMA journal_mode=WAL; PRAGMA user_version=2; CREATE TABLE sessions("
        "id TEXT PRIMARY KEY,name TEXT,state TEXT,origin TEXT,event_count INTEGER,options_json TEXT,content_hash TEXT,created_at TEXT);"
        "CREATE TABLE session_events(session_id TEXT,ordinal INTEGER,original_seq INTEGER,event_json TEXT); BEGIN");
    for (std::size_t i = 0; i < count; ++i)
        insert(db, id(i), created(i), i % 2 == 0 ? "frozen" : "interrupted");
    db.run("COMMIT");
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void openFailures(const std::filesystem::path& directory) {
    const auto missing = directory / "missing.sqlite3";
    try {
        ReadOnlyLibrary library(missing);
        throw std::runtime_error("Missing library accepted");
    } catch (const LibraryOpenError& error) {
        check(error.failure() == LibraryOpenFailure::missing, "Missing library classification lost");
    }
    check(!std::filesystem::exists(missing), "Read-only open created missing database");
    try {
        ReadOnlyLibrary library(directory);
        throw std::runtime_error("Directory library accepted");
    } catch (const LibraryOpenError& error) {
        check(error.failure() == LibraryOpenFailure::unreadable, "Unreadable library classification lost");
    }
    const auto incompatible = directory / "incompatible.sqlite3";
    { std::ofstream output(incompatible); output << "not a SQLite database"; }
    const auto before = contents(incompatible);
    try {
        ReadOnlyLibrary library(incompatible);
        throw std::runtime_error("Incompatible library accepted");
    } catch (const LibraryOpenError& error) {
        check(error.failure() == LibraryOpenFailure::incompatible, "Incompatible library classification lost");
    }
    check(contents(incompatible) == before, "Reader altered incompatible file");
    const auto wrongSchema = directory / "wrong-schema.sqlite3";
    { Database db(wrongSchema); db.run("PRAGMA user_version=2; CREATE TABLE unrelated(value TEXT)"); }
    rejects<LibraryOpenError>([&] { ReadOnlyLibrary library(wrongSchema); }, "Incomplete schema accepted");
}
}

int main() {
    try {
        DisposableDirectory temporary;
        openFailures(temporary.path);
        const auto path = temporary.path / "large.sqlite3";
        constexpr std::size_t count = 6003;
        fixture(path, count);
        const auto original = contents(path);
        ReadOnlyLibrary library(path);
        const auto initial = library.revision();
        check(initial.sourceId.size() == 32 && !initial.revision.empty(), "Source/revision missing");
        check(library.revision() == initial, "Unchanged library revision changed");
        rejects<std::invalid_argument>([&] { (void)library.sessionPage(0); }, "Zero page size accepted");
        rejects<std::invalid_argument>([&] { (void)library.sessionPage(101); }, "Oversized page accepted");
        rejects<std::invalid_argument>([&] { (void)library.sessionPage(10, "broken"); }, "Malformed cursor accepted");
        rejects<std::invalid_argument>([&] { (void)library.sessionPage(10, std::string(2049, 'x')); }, "Unbounded cursor accepted");
        const auto first = library.sessionPage(100);
        check(first.nextCursor.has_value() && first.sessions.size() == 100, "First page omitted continuation");
        check(first.library == initial, "Page revision differs from library");
        const auto legacy = library.sessions(100);
        for (std::size_t i = 0; i < first.sessions.size(); ++i)
            check(first.sessions[i].id == legacy[i].id, "Legacy list ordering changed");
        auto damaged = *first.nextCursor;
        damaged.back() = damaged.back() == '0' ? '1' : '0';
        rejects<std::invalid_argument>([&] { (void)library.sessionPage(100, damaged); }, "Corrupt cursor accepted");
        ReadOnlyLibrary otherConnection(path);
        check(otherConnection.revision().sourceId != initial.sourceId, "Reopen reused source generation");
        rejects<StaleLibraryCursor>([&] { (void)otherConnection.sessionPage(100, *first.nextCursor); }, "Cross-source cursor accepted");
        std::vector<std::size_t> expected;
        for (std::size_t i = 0; i < count; ++i) expected.push_back(i);
        std::sort(expected.begin(), expected.end(), [](auto a, auto b) {
            return created(a) == created(b) ? a < b : created(a) > created(b);
        });
        const auto start = std::chrono::steady_clock::now();
        auto page = first;
        std::size_t seen{};
        for (;;) {
            for (const auto& saved : page.sessions) {
                check(seen < count && saved.id == id(expected[seen]), "Paging skipped, duplicated or reordered a piece");
                check(saved.origin == "demo" && saved.truncated && saved.observedBounds.has_value(), "Paging lost provenance/bounds");
                check(saved.state == (expected[seen] % 2 == 0 ? "frozen" : "interrupted"), "Paging lost saved state");
                check(saved.metadataIntegrity == "legacy-metadata-unverified", "Paging lost integrity status");
                ++seen;
            }
            if (!page.nextCursor) break;
            check(page.nextCursor->size() <= 2048, "Produced oversized cursor");
            page = library.sessionPage(100, *page.nextCursor);
        }
        check(seen == count, "Paging did not reach final piece");
        check(page.sessions.size() == count % 100, "Final partial page incorrect");
        SavedSession verified;
        const auto timeline = library.load(id(expected.back()), TimeBasis::observed, {}, &verified);
        check(verified.id == id(expected.back()) && timeline.durationUs() == 2000000, "Piece beyond first page cannot load");
        check(contents(path) == original && library.revision() == initial, "Read-only browsing altered library");
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Synthetic-library-6003-v1: all pages " << elapsed << " ms; page capacity 100; total " << seen << '\n';
        {
            Database writer(path);
            insert(writer, "new-piece", "2026-11-04T00:00:00Z");
        }
        const auto changed = library.revision();
        check(changed.sourceId == initial.sourceId && changed.revision != initial.revision, "External commit did not advance revision");
        rejects<StaleLibraryCursor>([&] { (void)library.sessionPage(100, *first.nextCursor); }, "Stale cursor crossed external commit");
        check(library.sessionPage(1).sessions.front().id == "new-piece", "Refresh did not see external commit");
        const auto emptyPath = temporary.path / "empty.sqlite3";
        fixture(emptyPath, 0);
        ReadOnlyLibrary empty(emptyPath);
        const auto emptyPage = empty.sessionPage();
        check(emptyPage.sessions.empty() && !emptyPage.nextCursor, "Empty library fabricated continuation");
        const auto exactPath = temporary.path / "exact.sqlite3";
        fixture(exactPath, 100);
        ReadOnlyLibrary exact(exactPath);
        check(!exact.sessionPage(100).nextCursor, "Exact full final page fabricated continuation");
        std::cout << "library browsing tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
