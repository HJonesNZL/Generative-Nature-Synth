#include <atlas/collection.hpp>
#include <atlas/piece_writer.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed{};
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "operation should fail");
}
std::string scalar(const std::filesystem::path &path, const std::string &sql) {
    sqlite3 *db{};
    require(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE,
                            nullptr) == SQLITE_OK,
            "fixture scalar open");
    struct Close {
        sqlite3 *db;
        ~Close() { sqlite3_close(db); }
    } close{db};
    sqlite3_stmt *query{};
    require(sqlite3_prepare_v2(db, sql.c_str(), -1, &query, nullptr) ==
                SQLITE_OK,
            "fixture scalar prepare");
    struct Finalize {
        sqlite3_stmt *s;
        ~Finalize() { sqlite3_finalize(s); }
    } finalize{query};
    require(sqlite3_step(query) == SQLITE_ROW, "fixture scalar row");
    const auto *text = sqlite3_column_text(query, 0);
    require(text != nullptr, "fixture scalar missing");
    return reinterpret_cast<const char *>(text);
}
void independentWriter(const std::filesystem::path &path) {
    sqlite3 *db{};
    require(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE,
                            nullptr) == SQLITE_OK,
            "concurrent fixture open");
    struct Close {
        sqlite3 *db;
        ~Close() { sqlite3_close(db); }
    } close{db};
    sqlite3_busy_timeout(db, 0);
    require(sqlite3_exec(db,
                         "BEGIN IMMEDIATE; UPDATE metadata SET value=value "
                         "WHERE key='native_collection_owner'; COMMIT",
                         nullptr, nullptr, nullptr) == SQLITE_OK,
            "selection or validation held the destination writer lock");
}
} // namespace
int main() {
    const auto dir =
        std::filesystem::temp_directory_path() /
        ("atlas-freeze-test-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{dir};
    try {
        const auto path = dir / "live.sqlite";
        atlas::Collector collector(path);
        atlas::FreezePieceRequest request;
        request.id = "empty-history";
        request.name = "Empty historical window";
        request.createdAt = "2026-11-07T02:00:00Z";
        request.observedFrom = "2026-11-07T00:00:00Z";
        request.observedTo = "2026-11-07T01:00:00Z";
        const auto result = atlas::freezeObservedPiece(path, request);
        require(result.eventCount == 0, "empty freeze count");
        atlas::ReadOnlyLibrary reader(path);
        atlas::SavedSession verified;
        (void)reader.load(result.id, atlas::TimeBasis::observed, {}, &verified);
        require(verified.metadataIntegrity == "sha256-envelope-v1-verified",
                "sealed empty window");
        require(verified.observedBounds->first ==
                    atlas::parseUtc(request.observedFrom),
                "preserve requested start");
        require(verified.observedBounds->second ==
                    atlas::parseUtc(request.observedTo),
                "preserve requested end");
        auto emptySource = request;
        emptySource.id = "empty-requested-source";
        emptySource.sourceId = "birdweather";
        atlas::freezeObservedPiece(path, emptySource);
        require(
            scalar(path, "SELECT "
                         "json_extract(options_json,'$.original_session.source_"
                         "coverage[0].source_id')||':'||json_extract(options_"
                         "json,'$.original_session.source_coverage[0].captured_"
                         "event_count')||':'||json_extract(options_json,'$."
                         "original_session.source_coverage[0].completeness') "
                         "FROM sessions WHERE id='empty-requested-source'") ==
                "birdweather:0:unknown",
            "empty requested source coverage fabricated completeness");
        request.createdAt = "2026-11-07T03:00:00Z";
        require(atlas::freezeObservedPiece(path, request).id == result.id,
                "retry ignores host clock");
        request.speciesId = "species-different";
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        request.speciesId.reset();
        request.seed = 7;
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        request.seed = 0;
        request.id = "cancelled";
        rejects([&] {
            atlas::freezeObservedPiece(
                path, request,
                [](auto stage, auto, auto) { return stage != "committing"; });
        });
        rejects([&] { reader.session(request.id); });
        request.id = "material";
        collector.collect(
            "birdweather", "2026-11-07T01:01:00Z",
            [](const atlas::CollectionRequest &) {
                return R"({"data":{"detections":{"nodes":[{"id":"inside","timestamp":"2026-11-07T00:59:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"},{"id":"upper","timestamp":"2026-11-07T01:00:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"}],"pageInfo":{"hasNextPage":false,"endCursor":"end"}}}})";
            });
        const auto material = atlas::freezeObservedPiece(path, request);
        require(material.eventCount == 1,
                "half-open end excludes exact boundary");
        (void)reader.load(material.id, atlas::TimeBasis::observed);
        require(
            scalar(
                path,
                "SELECT "
                "json_extract(options_json,'$.original_session.source_coverage["
                "0].source_id')||':'||json_extract(options_json,'$.original_"
                "session.source_coverage[0].captured_event_count')||':'||json_"
                "extract(options_json,'$.original_session.source_coverage[0]."
                "completeness') FROM sessions WHERE id='material'") ==
                "birdweather:1:unknown",
            "captured source coverage lost observed count or uncertainty");
        require(scalar(path,
                       "SELECT "
                       "json_extract(options_json,'$.original_session.coverage_"
                       "scope') FROM sessions WHERE id='material'") ==
                    "saved_snapshot_only",
                "source coverage scope omitted");
        require(reader.session("empty-history").eventCount == 0,
                "earlier piece remains immutable");
        request.id = "concurrent-writer";
        bool selectedWithoutLock{}, validatedWithoutLock{};
        const auto concurrent = atlas::freezeObservedPiece(
            path, request, [&](auto stage, auto done, auto total) {
                if (stage == "selecting") {
                    require(total == 0,
                            "selection reported capacity as a known total");
                    independentWriter(path);
                    selectedWithoutLock = true;
                }
                if (stage == "validating" && total > 0) {
                    require(total == 1 && done <= total,
                            "scratch progress lost actual count");
                    independentWriter(path);
                    validatedWithoutLock = true;
                }
                return true;
            });
        require(concurrent.eventCount == 1 && selectedWithoutLock &&
                    validatedWithoutLock,
                "freeze did not expose cancellable lock-free phases");
        request.id = "cancel-scratch";
        rejects([&] {
            atlas::freezeObservedPiece(
                path, request, [](auto stage, auto, auto total) {
                    return stage != "validating" || total == 0;
                });
        });
        rejects([&] { reader.session(request.id); });
        request.id = "racing-freeze";
        bool raced{};
        atlas::SavedSession winning;
        const auto race = atlas::freezeObservedPiece(
            path, request, [&](auto stage, auto, auto) {
                if (stage == "validated" && !raced) {
                    raced = true;
                    collector.collect(
                        "birdweather", "2026-11-07T01:03:00Z",
                        [](const atlas::CollectionRequest &) {
                            return R"({"data":{"detections":{"nodes":[{"id":"arrived-during-validation","timestamp":"2026-11-07T00:58:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"}],"pageInfo":{"hasNextPage":false,"endCursor":"end-2"}}}})";
                        });
                    auto another = request;
                    another.createdAt = "2026-11-07T04:00:00Z";
                    winning = atlas::freezeObservedPiece(path, another);
                }
                return true;
            });
        require(raced && winning.eventCount == 2 &&
                    race.eventCount == winning.eventCount &&
                    race.contentHash == winning.contentHash,
                "publication overwrote or rejected winning immutable retry");
        require(reader.session("material").eventCount == 1,
                "race mutated earlier piece");
        const auto oversizedStage = std::filesystem::path(
            path.string() + ".atlas-import-stage.sqlite3");
        {
            std::ofstream create(oversizedStage);
        }
        std::filesystem::resize_file(oversizedStage, 512ULL * 1024 * 1024);
        request.id = "capacity-refused";
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        rejects([&] { reader.session(request.id); });
        std::filesystem::remove(oversizedStage);
        request.observedTo = request.observedFrom;
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        std::cout << "piece writer tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
