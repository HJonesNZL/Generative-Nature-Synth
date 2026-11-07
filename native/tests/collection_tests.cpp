#include <atlas/collection.hpp>
#include <atlas/storage.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Call> void rejects(Call call, const char *message) {
    bool rejected{};
    try {
        call();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, message);
}
struct Query {
    sqlite3 *db{};
    explicit Query(const std::filesystem::path &path) {
        require(sqlite3_open_v2(path.string().c_str(), &db,
                                SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK,
                "open disposable query");
    }
    ~Query() { sqlite3_close(db); }
    std::string text(const std::string &sql) {
        sqlite3_stmt *statement{};
        require(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) ==
                    SQLITE_OK,
                "prepare test query");
        const int step = sqlite3_step(statement);
        if (step != SQLITE_ROW) {
            sqlite3_finalize(statement);
            throw std::runtime_error("test query row missing");
        }
        const auto *value = sqlite3_column_text(statement, 0);
        const std::string result =
            value ? reinterpret_cast<const char *>(value) : "null";
        sqlite3_finalize(statement);
        return result;
    }
    void execute(const char *sql) {
        require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
                "execute disposable SQL");
    }
};
std::string bird(std::string id = "one", std::string longitude = "174",
                 std::string timestamp = "2026-11-07T14:00:00+13:00") {
    return "{\"id\":\"" + id + "\",\"timestamp\":\"" + timestamp +
           "\",\"coords\":{\"lat\":-41,\"lon\":" + longitude +
           R"(},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"station-fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"})";
}
std::string page(std::string nodes, bool next = false,
                 std::string cursor = "cursor") {
    return R"({"data":{"detections":{"nodes":[)" + nodes +
           R"(],"pageInfo":{"hasNextPage":)" + (next ? "true" : "false") +
           R"(,"endCursor":")" + cursor + R"("}}}})";
}
auto fixed(std::string value) {
    return [value = std::move(value)](const atlas::CollectionRequest &) {
        return value;
    };
}
constexpr auto now = "2026-11-07T01:05:00Z";
std::string shark(std::string longitude = "174") {
    return R"({"data":{"data":[{"id":"subject-fixture","name":"Synthetic shark","last_position":{"geometry":{"type":"Point","coordinates":[)" +
           longitude +
           R"(,-41]},"properties":{"DateTime":"2026-11-06T22:00:00Z"}}},{"id":"no-location","last_position":null}]}})";
}
auto sharkFetch(std::string response) {
    return [response =
                std::move(response)](const atlas::CollectionRequest &request) {
        require(request.body.empty(), "shark GET body must be empty");
        if (request.url ==
            "https://map.sustainableoceansociety.co.nz/public/bootstrap.json")
            return std::string(
                R"({"configUrl":"https://map.sustainableoceansociety.co.nz/public/configs/config.json"})");
        if (request.url == "https://map.sustainableoceansociety.co.nz/public/"
                           "configs/config.json")
            return std::string(
                R"({"server":"ermap-server.pamdas.org","public_name":"nzsharkman","subject_group":"synthetic & group","private_token":"DO_NOT_STORE_FIXTURE","subjects":{"subject-fixture":{"animal_name":"Named fixture"}}})");
        require(request.url ==
                    "https://ermap-server.pamdas.org/nzsharkman/api/v1.0/"
                    "subjects?subject_group=synthetic%20%26%20group",
                "shark fixed host and encoded group");
        return response;
    };
}
} // namespace
int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("atlas-collection-test-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(root);
        // World collection uses disposable state, never the owner's library.
        const auto worldPath = root / "world.sqlite3";
        {
            atlas::Collector world(worldPath);
            Query query(worldPath);
            unsigned calls{};
            bool westSplit{}, eastSplit{};
            auto regional = [&](const atlas::CollectionRequest &request) {
                ++calls;
                westSplit = westSplit || request.body.find("\"lon\":180") != std::string::npos;
                eastSplit = eastSplit || (request.body.find("\"lon\":-180") != std::string::npos &&
                    request.body.find("\"lat\":-60") != std::string::npos);
                require(request.body.find("\"first\":250") != std::string::npos,
                        "world requests retain finite page size");
                return page(bird("shared"), true, "capped");
            };
            const auto first = world.collect("birdweather_world", now, regional);
            require(calls == 4 && first.inserted == 1 && first.duplicates == 3 &&
                        !first.complete,
                    "four distinct regions share dedupe and report saturation");
            rejects([&] { world.collect("birdweather_world", now, regional); },
                    "world provider minute budget enforced");
            require(calls == 4, "quota rejection makes no request");
            world.collect("birdweather_world", "2026-11-07T01:06:00Z", regional);
            require(westSplit && eastSplit, "Oceania queries split at antimeridian");
            require(calls == 8 && query.text(
                "SELECT count(*) FROM sources WHERE id LIKE 'birdweather:%'") == "8",
                "sparse and saturated regions each receive a turn");
            require(query.text("SELECT count(*) FROM revisions WHERE live_eligible=1") == "0",
                    "first regional baselines never replay as live");
            require(query.text("SELECT source_id FROM events") == "birdweather",
                    "world preserves genuine provider event namespace");
        }
        {
            atlas::Collector reopened(worldPath);
            Query query(worldPath);
            unsigned calls{};
            reopened.collect("birdweather_world", "2026-11-07T01:07:00Z",
                [&](const atlas::CollectionRequest &) {
                    ++calls;
                    return page(bird("arrival", "174", "2026-11-07T01:06:30Z"));
                });
            require(calls == 4 && query.text(
                "SELECT change_kind||':'||live_eligible FROM revisions ORDER BY seq DESC LIMIT 1") == "new:1",
                "regional arrival baseline survives restart");
        }
        const auto path = root / "live.sqlite3";
        {
            atlas::Collector collector(path);
            Query query(path);
            require(query.text("PRAGMA user_version") == "3", "schema v3");
            atlas::ReadOnlyLibrary reader(path);
            require(reader.sessions().empty(),
                    "existing reader accepts new managed library");
            const auto initial = collector.sourceStates();
            require(initial.size() == 3 && initial[0].lastAttemptUtc.empty(),
                    "no fake fetch state before poll");
            auto baseline = collector.collect(
                "birdweather", now,
                [&](const atlas::CollectionRequest &request) {
                    require(request.url ==
                                "https://app.birdweather.com/graphql",
                            "BirdWeather public fixed URL");
                    require(request.body.find("\"first\":250") !=
                                    std::string::npos &&
                                request.body.find("\"count\":10") !=
                                    std::string::npos,
                            "bounded rolling query");
                    return page(bird());
                });
            require(baseline.inserted == 1 && baseline.complete &&
                        baseline.invalidRecords == 0,
                    "string certainty is valid acoustic data");
            require(
                query.text(
                    "SELECT change_kind||':'||live_eligible FROM revisions") ==
                    "baseline:0",
                "baseline cannot trigger live");
            require(query.text("SELECT observed_at FROM revisions") ==
                        "2026-11-07T01:00:00.000000Z",
                    "provider timezone canonicalized");
            require(query.text(
                        "SELECT json_extract(event_json,'$.quality_flags[2]') "
                        "FROM revisions") ==
                        "birdweather_certainty:almost_certain",
                    "classification retained without numeric invention");
            require(
                query.text("SELECT count(*) FROM revisions WHERE "
                           "json_type(event_json,'$.individual_id')='null' AND "
                           "json_type(event_json,'$.deployment_id')='null'") ==
                    "1",
                "station detection cannot fabricate individuals");
            require(collector
                            .collect("birdweather", "2026-11-07T01:06:00Z",
                                     fixed(page(bird())))
                            .duplicates == 1,
                    "duplicate ignored");
            auto arrival = collector.collect(
                "birdweather", "2026-11-07T01:07:00Z",
                fixed(page(bird("two", "174", "2026-11-07T01:06:30Z"))));
            require(arrival.inserted == 1 &&
                        query.text(
                            "SELECT change_kind||':'||live_eligible FROM "
                            "revisions ORDER BY seq DESC LIMIT 1") == "new:1",
                    "new arrival after baseline live");
            auto correction = collector.collect(
                "birdweather", "2026-11-07T01:08:00Z",
                fixed(page(bird("two", "175", "2026-11-07T01:06:30Z"))));
            require(
                correction.corrected == 1 &&
                    query.text("SELECT change_kind||':'||live_eligible FROM "
                               "revisions ORDER BY seq DESC LIMIT 1") ==
                        "corrected:0",
                "correction not live");
            require(query.text("SELECT first_seen_at FROM revisions ORDER BY "
                               "seq DESC LIMIT 1") ==
                        "2026-11-07T01:07:00.000000Z",
                    "first seen retained on correction");
            auto malformed =
                collector.collect("birdweather", "2026-11-07T01:09:00Z",
                                  fixed(page(bird("bad", "999"))));
            require(malformed.invalidRecords == 1 && !malformed.complete &&
                        collector.sourceStates()[0].error ==
                            "invalid_or_future_records",
                    "invalid row visible/degraded");
            require(query.text(
                        "SELECT json_extract(status,'$.last_complete_at_utc') "
                        "FROM sources WHERE id='birdweather'") ==
                        "2026-11-07T01:08:00.000000Z",
                    "invalid row does not advance qualified checkpoint");
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", "2026-11-07T01:10:00Z",
                        [](const atlas::CollectionRequest &) -> std::string {
                            throw atlas::CollectionFetchError("http_429", 1200);
                        });
                },
                "transport failure propagated");
            require(query.text(
                        "SELECT json_extract(status,'$.retry_delay_seconds') "
                        "FROM sources WHERE id='birdweather'") == "1200",
                    "Retry-After retained");
            require(collector.sourceStates()[0].lastSuccessUtc ==
                            "2026-11-07T01:09:00.000000Z" &&
                        collector.sourceStates()[0].error == "http_429",
                    "failure preserves prior successful fetch and marks error");
            const auto gap = collector.collect(
                "birdweather", "2026-11-07T01:20:00Z", fixed(page("")));
            require(gap.complete && collector.sourceStates()[0].gap,
                    "downtime beyond observation window marks gap");
            require(collector.sourceStates()[0].lastObservationUtc ==
                        "2026-11-07T01:06:30.000000Z",
                    "empty fetch cannot invent recent observation");
            auto sharkBaseline =
                collector.collect("nzsharks", now, sharkFetch(shark()));
            require(sharkBaseline.inserted == 1 && sharkBaseline.complete,
                    "shark independent stream");
            require(query.text("SELECT "
                               "json_extract(event_json,'$.latitude')||':'||"
                               "json_extract(event_json,'$.longitude') FROM "
                               "revisions WHERE source_id='nzsharks'") ==
                        "-41:174",
                    "GeoJSON lon/lat converted correctly");
            require(
                query.text(
                    "SELECT count(*) FROM revisions WHERE source_id='nzsharks' "
                    "AND json_type(event_json,'$.individual_id')='null' AND "
                    "json_type(event_json,'$.deployment_id')='null' AND "
                    "json_extract(event_json,'$.provider_subject_id')='"
                    "nzsharks:subject:subject-fixture'") == "1",
                "source subject not promoted to individual/deployment");
            require(query.text("SELECT count(*) FROM revisions WHERE "
                               "event_json LIKE '%DO_NOT_STORE_FIXTURE%' OR "
                               "raw_json LIKE '%DO_NOT_STORE_FIXTURE%'") == "0",
                    "bootstrap config tokens not persisted");
            require(
                query.text("SELECT "
                           "json_extract(event_json,'$.individual_display_name'"
                           ") FROM revisions WHERE source_id='nzsharks'") ==
                    "Named fixture",
                "source display name preserved");
            require(collector
                            .collect("nzsharks", "2026-11-07T01:20:00Z",
                                     sharkFetch(shark("175")))
                            .corrected == 1,
                    "same timestamp shark coordinate correction");
            rejects(
                [&] {
                    collector.collect(
                        "nzsharks", now,
                        fixed(
                            R"({"configUrl":"https://private.invalid/config"})"));
                },
                "unexpected configuration route rejected");
            rejects(
                [&] { collector.collect("unrecognized", now, fixed("{}")); },
                "unknown source rejected");
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", now,
                        fixed(std::string(4U * 1024U * 1024U + 1U, 'x')));
                },
                "response hard cap");
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", now,
                        fixed(
                            R"({"data":{},"errors":[{"message":"DO_NOT_LOG_FIXTURE"}]})"));
                },
                "GraphQL errors not swallowed");
            require(collector.sourceStates()[0].error == "graphql_error",
                    "GraphQL raw message redacted");
            query.execute("CREATE TRIGGER reject_fixture BEFORE INSERT ON "
                          "events WHEN NEW.id='birdweather:rollback-b' BEGIN "
                          "SELECT RAISE(ABORT,'fixture'); END");
            const auto before = query.text("SELECT count(*) FROM revisions");
            rejects(
                [&] {
                    collector.collect("birdweather", now,
                                      fixed(page(bird("rollback-a") + "," +
                                                 bird("rollback-b"))));
                },
                "storage error propagated");
            require(query.text("SELECT count(*) FROM revisions") == before,
                    "ingestion transaction rollback includes earlier records");
            query.execute("DROP TRIGGER reject_fixture");
        }
        {
            atlas::Collector restarted(path);
            require(restarted
                            .collect("birdweather", "2026-11-07T01:21:00Z",
                                     fixed(page(bird())))
                            .duplicates == 1,
                    "restart retains identity/dedupe");
        }
        {
            const auto paged = root / "large-pages.sqlite3";
            atlas::Collector collector(paged);
            Query query(paged);
            auto nodes = [](unsigned first, unsigned count) {
                std::string result;
                for (unsigned i = 0; i < count; ++i) {
                    if (!result.empty())
                        result += ',';
                    result += bird("paged-" + std::to_string(first + i));
                }
                return result;
            };
            auto completeFetch = [&nodes, calls = 0U](
                                     const atlas::CollectionRequest &request) mutable {
                require(request.body.find("\"first\":250") != std::string::npos,
                        "request supports full 250-node pages");
                const auto pageIndex = calls++;
                require(pageIndex < 3, "complete pagination stops after last page");
                if (pageIndex != 0)
                    require(request.body.find("\"after\":\"large-" +
                                              std::to_string(pageIndex) + "\"") !=
                                std::string::npos,
                            "next page uses provider cursor");
                return page(nodes(pageIndex * 250, pageIndex == 2 ? 1 : 250),
                            pageIndex != 2,
                            "large-" + std::to_string(pageIndex + 1));
            };
            const auto initial = collector.collect("birdweather", now, completeFetch);
            require(initial.inserted == 501 && initial.complete,
                    "250 plus 250 plus one records complete beyond old 400 cap");
            require(query.text("SELECT count(*) FROM revisions WHERE "
                               "change_kind='baseline' AND live_eligible=0") == "501",
                    "large initial snapshot preserves baseline semantics");
            const auto repeated = collector.collect(
                "birdweather", "2026-11-07T01:06:00Z", completeFetch);
            require(repeated.duplicates == 501 && repeated.inserted == 0 &&
                        repeated.complete,
                    "large overlapping pages deduplicate without revisions");
            const auto before = query.text("SELECT count(*) FROM revisions");
            unsigned calls{};
            rejects([&] {
                collector.collect("birdweather", "2026-11-07T01:07:00Z",
                                  [&](const atlas::CollectionRequest &) {
                    ++calls;
                    return calls == 1
                               ? page(bird("uncommitted-page"), true, "oversized")
                               : page(nodes(1000, 251));
                });
            }, "oversized provider page rejected");
            require(calls == 2 &&
                        query.text("SELECT count(*) FROM revisions") == before,
                    "oversized later page commits no earlier records");
            calls = 0;
            const auto capped = collector.collect(
                "birdweather", "2026-11-07T01:08:00Z",
                [&](const atlas::CollectionRequest &) {
                    const auto pageIndex = calls++;
                    return page(nodes(2000 + pageIndex * 250, 250), true,
                                "saturated-" + std::to_string(calls));
                });
            require(calls == 4 && capped.inserted == 1000 && !capped.complete &&
                        collector.sourceStates()[0].error == "pagination_limit_reached",
                    "1000-record cap remains bounded and visibly incomplete");
        }
        {
            const auto incomplete = root / "incomplete.sqlite3";
            atlas::Collector collector(incomplete);
            Query query(incomplete);
            auto invalid = collector.collect(
                "birdweather", now, fixed(page("{\"id\":\"missing-fields\"}")));
            require(invalid.invalidRecords == 1 && !invalid.complete,
                    "all invalid is not success baseline");
            require(
                query.text("SELECT json_extract(status,'$.baseline_complete') "
                           "FROM sources WHERE id='birdweather'") == "0",
                "invalid baseline stays open");
            auto future = collector.collect(
                "birdweather", now,
                fixed(page(bird("future", "174", "2026-11-08T00:00:00Z"))));
            require(future.deferredFuture == 1 && !future.complete,
                    "future observations deferred explicitly");
            unsigned calls{};
            auto limited = collector.collect(
                "birdweather", now, [&](const atlas::CollectionRequest &) {
                    ++calls;
                    return page(bird("baseline-" + std::to_string(calls)), true,
                                "cursor-" + std::to_string(calls));
                });
            require(calls == 4 && !limited.complete && limited.inserted == 4,
                    "pagination four-page hard cap");
            require(collector.sourceStates()[0].error ==
                        "pagination_limit_reached",
                    "page cap visible");
            const auto before = query.text("SELECT count(*) FROM revisions");
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", now,
                        fixed(page(bird("repeat"), true, "repeat")));
                },
                "repeated cursor rejected");
            require(query.text("SELECT count(*) FROM revisions") == before,
                    "failed pagination commits no partial batch");
        }
        {
            const auto busy = root / "busy-capped.sqlite3";
            auto capped = [](std::string firstPage) {
                return [firstPage = std::move(firstPage),
                        count = 0U](const atlas::CollectionRequest &) mutable {
                    ++count;
                    return page(count == 1 ? firstPage : "", true,
                                "cap-" + std::to_string(count));
                };
            };
            {
                atlas::Collector collector(busy);
                Query query(busy);
                const auto initial = collector.collect(
                    "birdweather", now, capped(bird("busy-initial")));
                require(initial.inserted == 1 && !initial.complete,
                        "capped first snapshot remains incomplete");
                require(query.text("SELECT change_kind||':'||live_eligible "
                                   "FROM revisions") == "baseline:0",
                        "first capped snapshot cannot pulse old material");
                require(
                    query.text("SELECT "
                               "json_extract(status,'$.arrival_baseline_"
                               "established') FROM sources") == "1" &&
                        query.text(
                            "SELECT json_extract(status,'$.baseline_complete') "
                            "FROM sources") == "0",
                    "arrival readiness is independent of full coverage");
                const auto continued = collector.collect(
                    "birdweather", "2026-11-07T01:06:00Z",
                    capped(
                        bird("busy-fresh", "174", "2026-11-07T01:05:30Z") +
                        "," +
                        bird("busy-backfill", "174", "2026-11-07T01:04:30Z") +
                        "," + bird("busy-initial", "175")));
                require(!continued.complete && continued.inserted == 2 &&
                            continued.corrected == 1,
                        "capped source continues collecting fresh and "
                        "historical records");
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:busy-fresh'") == "new:1",
                    "fresh observations after previous attempt pulse despite "
                    "permanent page cap");
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:busy-backfill'") ==
                        "baseline:0",
                    "newly discovered backfill remains non-live");
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:busy-initial' ORDER BY "
                        "seq DESC LIMIT 1") == "corrected:0",
                    "capped-source corrections do not pulse");
                require(
                    collector.sourceStates()[0].error ==
                            "pagination_limit_reached" &&
                        !collector.sourceStates()[0].complete,
                    "arrival readiness must not conceal coverage degradation");
                rejects(
                    [&] {
                        collector.collect("birdweather", "2026-11-07T01:07:00Z",
                                          [](const atlas::CollectionRequest &)
                                              -> std::string {
                                              throw atlas::CollectionFetchError(
                                                  "network_or_tls_error");
                                          });
                    },
                    "capped source reports failed attempt");
                collector.collect("birdweather", "2026-11-07T01:08:00Z",
                                  capped(bird("recovered-old", "174",
                                              "2026-11-07T01:06:30Z") +
                                         "," +
                                         bird("recovered-fresh", "174",
                                              "2026-11-07T01:07:30Z")));
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:recovered-old'") ==
                            "baseline:0" &&
                        query.text("SELECT change_kind||':'||live_eligible "
                                   "FROM revisions WHERE "
                                   "event_id='birdweather:recovered-fresh'") ==
                            "new:1",
                    "recovery uses actual prior attempt to suppress backlog");
                rejects(
                    [&] {
                        collector.collect("birdweather", "2026-11-07T01:30:00Z",
                                          [](const atlas::CollectionRequest &)
                                              -> std::string {
                                              throw atlas::CollectionFetchError(
                                                  "network_or_tls_error");
                                          });
                    },
                    "outage attempt recorded");
                collector.collect(
                    "birdweather", "2026-11-07T01:31:00Z",
                    capped(bird("gap-old", "174", "2026-11-07T01:29:00Z")));
                require(collector.sourceStates()[0].gap,
                        "outage gap visible even without any complete poll");
                require(query.text("SELECT live_eligible FROM revisions WHERE "
                                   "event_id='birdweather:gap-old'") == "0",
                        "outage backlog does not pulse");
            }
            {
                atlas::Collector restarted(busy);
                Query query(busy);
                restarted.collect("birdweather", "2026-11-07T01:32:00Z",
                                  capped(bird("restart-backlog", "174",
                                              "2026-11-07T01:31:30Z")));
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:restart-backlog'") ==
                        "baseline:0",
                    "restart establishes a fresh non-live snapshot");
                restarted.collect("birdweather", "2026-11-07T01:33:00Z",
                                  capped(bird("restart-fresh", "174",
                                              "2026-11-07T01:32:30Z")));
                require(
                    query.text(
                        "SELECT change_kind||':'||live_eligible FROM revisions "
                        "WHERE event_id='birdweather:restart-fresh'") ==
                        "new:1",
                    "fresh observations resume after restart baseline despite "
                    "page cap");
            }
            {
                const auto empty = root / "initial-empty.sqlite3";
                atlas::Collector collector(empty);
                Query query(empty);
                collector.collect("birdweather", now, fixed(page("")));
                collector.collect("birdweather", "2026-11-07T01:06:00Z",
                                  fixed(page(bird("first-after-empty", "174",
                                                  "2026-11-07T01:05:30Z"))));
                require(query.text("SELECT change_kind||':'||live_eligible "
                                   "FROM revisions") == "new:1",
                        "a valid empty initial snapshot establishes arrival "
                        "readiness");
            }
        }
        {
            const auto cancelled = root / "cancelled.sqlite3";
            atlas::Collector collector(cancelled);
            Query query(cancelled);
            collector.collect("birdweather", now,
                              fixed(page(bird("cancel-initial"))));
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", "2026-11-07T01:06:00Z",
                        [](const atlas::CollectionRequest &) -> std::string {
                            throw atlas::CollectionFetchError("http_429", 1200);
                        });
                },
                "genuine failure before cancellation is recorded");
            unsigned calls{};
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", "2026-11-07T01:07:00Z",
                        [&](const atlas::CollectionRequest &) -> std::string {
                            if (++calls == 1)
                                return page(bird("cancel-staged", "174",
                                                 "2026-11-07T01:06:30Z"),
                                            true);
                            throw atlas::CollectionFetchError("cancelled");
                        });
                },
                "expected cancellation is propagated to service");
            require(calls == 2 &&
                        query.text("SELECT count(*) FROM revisions") == "1",
                    "cancelled multi-page request commits no buffered animal "
                    "records");
            auto state = collector.sourceStates()[0];
            require(state.lastAttemptUtc == "2026-11-07T01:07:00.000000Z" &&
                        state.lastSuccessUtc == "2026-11-07T01:05:00.000000Z",
                    "cancellation records pacing attempt without advancing "
                    "successful fetch");
            require(state.error == "http_429" && state.consecutiveErrors == 1 &&
                        state.retryDelaySeconds == 1200,
                    "cancellation preserves actual prior failure and backoff "
                    "without adding a fault");
            collector.collect("birdweather", "2026-11-07T01:08:00Z",
                              fixed(page("")));
            rejects(
                [&] {
                    collector.collect(
                        "birdweather", "2026-11-07T01:09:00Z",
                        [](const atlas::CollectionRequest &) -> std::string {
                            throw atlas::CollectionFetchError("cancelled");
                        });
                },
                "healthy source pause propagated");
            state = collector.sourceStates()[0];
            require(state.error.empty() && state.consecutiveErrors == 0 &&
                        state.retryDelaySeconds == 0 && state.complete,
                    "healthy cancellation cannot invent an error, retry delay "
                    "or incomplete result");
            require(state.lastAttemptUtc == "2026-11-07T01:09:00.000000Z" &&
                        state.lastSuccessUtc == "2026-11-07T01:08:00.000000Z",
                    "healthy cancellation preserves prior successful result "
                    "and actual attempt");
        }
        {
            const auto unmanaged = root / "owner.sqlite3";
            std::ofstream(unmanaged) << "DO_NOT_TOUCH";
            const auto before = std::filesystem::file_size(unmanaged);
            rejects([&] { atlas::Collector forbidden(unmanaged); },
                    "unmanaged existing file rejected");
            require(std::filesystem::file_size(unmanaged) == before &&
                        !std::filesystem::exists(unmanaged.string() + "-wal"),
                    "unmanaged file unchanged");
            const auto link = root / "alias.sqlite3";
            std::filesystem::create_symlink(path, link);
            rejects([&] { atlas::Collector forbidden(link); },
                    "symlink database rejected");
            require(std::string(atlas::CollectionFetchError(
                                    "https://secret.invalid/value")
                                    .what()) == "fetch_failed",
                    "unsafe errors redacted");
        }
        std::filesystem::remove_all(root);
        std::cout << "collection: managed ownership, acoustic/shark semantics, "
                     "provenance, dedupe/corrections, degraded baselines, "
                     "freshness, bounds and rollback passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "collection test failed: " << error.what() << "\n";
        std::filesystem::remove_all(root);
        return 1;
    }
}
