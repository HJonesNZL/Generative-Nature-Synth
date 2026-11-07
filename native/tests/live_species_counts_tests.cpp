#include "../src/live_species_counts.hpp"
#include <array>
#include <atlas/collection.hpp>
#include <atlas/piece_writer.hpp>
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
using namespace atlas;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Temporary {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("atlas-live-host-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directory(path); }
    ~Temporary() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
std::string bird(std::string id, std::string at, std::string longitude = "174",
                 std::string species = "42") {
    return "{\"id\":\"" + id + "\",\"timestamp\":\"" + at +
           "\",\"coords\":{\"lat\":-41,\"lon\":" + longitude +
           "},\"species\":{\"id\":" + species +
           ",\"scientificName\":\"Synthetic "
           "species\",\"commonName\":\"Synthetic "
           "bird\"},\"station\":{\"id\":\"fixture\"}}";
}
auto fetch(std::string nodes) {
    return [nodes](const CollectionRequest &) {
        return "{\"data\":{\"detections\":{\"nodes\":[" + nodes +
               "],\"pageInfo\":{\"hasNextPage\":false,\"endCursor\":null}}}}";
    };
}
void contains(const std::string &state, const std::string &field) {
    check(state.find(field) != std::string::npos, state.c_str());
}
} // namespace
std::int64_t number(const std::string &json, const std::string &key) {
    sqlite3 *db{};
    check(sqlite3_open(":memory:", &db) == SQLITE_OK, "json db");
    sqlite3_stmt *s{};
    const auto sql = "SELECT json_extract(?1,'$." + key + "')";
    check(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK,
          "json statement");
    sqlite3_bind_text(s, 1, json.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(s) == SQLITE_ROW, "json step");
    const auto result = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return result;
}
int main() {
    try {
        Temporary temporary;
        const auto path = temporary.path / "counts.sqlite3";
        Collector collector(path);
        collector.collect("birdweather", "2026-11-07T01:04:59Z",
                          fetch(bird("baseline", "2026-11-07T01:04:58Z")));
        sqlite3 *raw{};
        check(sqlite3_open(path.c_str(), &raw) == SQLITE_OK,
              "open disposable count library");
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(
            raw, sqlite3_close);
        const auto session = captureLiveSession(raw, "2026-11-07T01:05:00Z");
        std::string nodes;
        for (int i = 0; i < 7; ++i) {
            if (i)
                nodes += ",";
            nodes +=
                bird("row-" + std::to_string(i), "2026-11-07T01:05:01Z", "174",
                     std::to_string(i < 3   ? 42
                                    : i < 5 ? 43
                                            : 44));
        }
        nodes += "," + bird("late-old", "2026-11-07T01:04:50Z", "174", "99");
        collector.collect("birdweather", "2026-11-07T01:05:02Z", fetch(nodes));
        LiveSpeciesCounts counts;
        const auto first = counts.page(raw, session, session.id, 0, 1);
        check(number(first, "species[0].count") == 3,
              "full session totals count every current detection");
        contains(first, "birdweather:species:42");
        contains(first, "\"nextCursor\":\"1\"");
        const auto second = counts.page(raw, session, session.id, 1, 1);
        contains(second, "birdweather:species:43");
        const auto third = counts.page(raw, session, session.id, 2, 1);
        contains(third, "birdweather:species:44");
        contains(third, "\"nextCursor\":null");
        check(
            counts.page(raw, session, session.id, 0, 250).find("species:99") ==
                std::string::npos,
            "delayed pre-session observation is excluded even when inserted "
            "after start");
        collector.collect(
            "birdweather", "2026-11-07T01:05:03Z",
            fetch(bird("row-0", "2026-11-07T01:05:01Z", "174", "44")));
        const auto changed = counts.page(raw, session, session.id, 0, 250);
        contains(changed, "\"speciesId\":\"birdweather:species:44\"");
        check(number(changed, "species[0].count") == 3,
              "correction invalidates cached ranking and moves current species "
              "count");
        check(number(changed, "species[1].count") == 2 &&
                  number(changed, "species[2].count") == 2,
              "corrected event is counted once and equal-count identity ties "
              "stay stable");
        std::vector<std::string> pages(3);
        for (int i = 0; i < 600; ++i) {
            if (i % 250)
                pages[static_cast<std::size_t>(i / 250)] += ",";
            pages[static_cast<std::size_t>(i / 250)] +=
                bird("extra-" + std::to_string(i), "2026-11-07T01:05:03Z",
                     "174", std::to_string(200 + i));
        }
        std::size_t page{};
        collector.collect(
            "birdweather", "2026-11-07T01:05:04Z",
            [&](const CollectionRequest &) {
                const auto index = page++;
                const bool more = page < pages.size();
                return "{\"data\":{\"detections\":{\"nodes\":[" + pages[index] +
                       "],\"pageInfo\":{\"hasNextPage\":" +
                       (more ? "true" : "false") + ",\"endCursor\":" +
                       (more ? "\"page-" + std::to_string(page) + "\""
                             : "null") +
                       "}}}}";
            });
        const auto finalPage = counts.page(raw, session, session.id, 500, 250);
        check(number(finalPage, "species[102].count") == 1,
              "species beyond map/display sample remain available on later "
              "ranking pages");
        contains(finalPage, "\"nextCursor\":null");
        const auto appendCorrection = [&](const std::string &id,
                                          const std::string &jsonExpression) {
            const auto sql = "INSERT INTO "
                             "revisions(event_id,source_id,fingerprint,"
                             "observed_at,first_seen_at,ingested_at,change_"
                             "kind,live_eligible,event_json,raw_json) SELECT "
                             "event_id,source_id,fingerprint,observed_at,first_"
                             "seen_at,ingested_at,'corrected',0," +
                             jsonExpression +
                             ",raw_json FROM revisions WHERE seq=(SELECT "
                             "revision_seq FROM events WHERE id='" +
                             id +
                             "'); UPDATE events SET "
                             "revision_seq=last_insert_rowid() WHERE id='" +
                             id + "'";
            check(sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr) ==
                      SQLITE_OK,
                  "append adversarial current correction");
        };
        appendCorrection("birdweather:extra-0",
                         "json_set(event_json,'$.provenance_mode','imported')");
        appendCorrection("birdweather:extra-1",
                         "json_set(event_json,'$.schema_version',99)");
        collector.collect(
            "birdweather", "2026-11-07T01:05:05Z",
            fetch(bird("extra-2", "2026-11-07T01:04:59Z", "174", "202")));
        std::size_t totalSpecies{};
        for (std::size_t offset = 0; offset < 600; offset += 250) {
            const auto result =
                counts.page(raw, session, session.id, offset, 250);
            for (std::size_t n = 0; n < 250; ++n) {
                if (number(result,
                           "species[" + std::to_string(n) + "].count") == 0)
                    break;
                ++totalSpecies;
            }
        }
        check(totalSpecies == 600, "full aggregate excludes imported, "
                                   "malformed and pre-cutoff corrections");
        for (int poll = 0; poll < 7; ++poll) {
            std::vector<std::string> largePages(4);
            for (int i = 0; i < 900; ++i) {
                if (i % 250)
                    largePages[static_cast<std::size_t>(i / 250)] += ",";
                largePages[static_cast<std::size_t>(i / 250)] +=
                    bird("large-" + std::to_string(poll * 900 + i),
                         "2026-11-07T01:05:10Z");
            }
            std::size_t largePage{};
            const auto result = collector.collect(
                "birdweather",
                "2026-11-07T01:06:0" + std::to_string(poll) + "Z",
                [&](const CollectionRequest &) {
                    const auto index = largePage++;
                    const bool more = largePage < largePages.size();
                    return "{\"data\":{\"detections\":{\"nodes\":[" +
                           largePages[index] +
                           "],\"pageInfo\":{\"hasNextPage\":" +
                           (more ? "true" : "false") + ",\"endCursor\":" +
                           (more ? "\"large-page-" + std::to_string(largePage) +
                                       "\""
                                 : "null") +
                           "}}}}";
                });
            check(result.inserted == 900,
                  "large session uses genuine bounded collector pages");
        }
        LiveSpeciesCounts large;
        auto began = std::chrono::steady_clock::now();
        const auto initial = large.page(raw, session, session.id, 0, 250);
        const auto initialTime = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - began)
                                     .count();
        const auto initialRows = large.lastExaminedRevisions();
        check(initialRows > 6300 && number(initial, "species[0].count") == 6302,
              "large ranking counts every session row beyond map retention");
        collector.collect("birdweather", "2026-11-07T01:06:08Z",
                          fetch(bird("one-more", "2026-11-07T01:06:07Z")));
        began = std::chrono::steady_clock::now();
        const auto oneMore = large.page(raw, session, session.id, 0, 250);
        const auto deltaTime = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - began)
                                   .count();
        check(large.lastExaminedRevisions() == 1 &&
                  number(oneMore, "species[0].count") == 6303,
              "one new detection examines exactly one revision without "
              "revalidating prior session rows");
        (void)large.page(raw, session, session.id, 250, 250);
        check(large.lastExaminedRevisions() == 0,
              "next page reuses aggregate without revision scan");
        std::cout << "RANKING initial_revisions=" << initialRows
                  << " initial_ms=" << initialTime
                  << " delta_revisions=1 delta_ms=" << deltaTime << '\n';
        contains(counts.page(raw, session, "wrong-session", 0, 250),
                 "\"sessionActive\":false");
        contains(counts.page(raw, std::nullopt, session.id, 0, 250),
                 "\"sessionActive\":false");
        check(sqlite3_exec(
                  raw,
                  "DELETE FROM events WHERE id='birdweather:extra-3'; DELETE "
                  "FROM revisions WHERE event_id='birdweather:extra-3'; UPDATE "
                  "metadata SET value='replacement' WHERE key='store_id'",
                  nullptr, nullptr, nullptr) == SQLITE_OK,
              "replace disposable store identity");
        contains(counts.page(raw, session, session.id, 0, 250),
                 "\"sessionActive\":false");
        bool rejected{};
        try {
            (void)counts.page(raw, session, session.id, 0, 251);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "oversized species page is rejected");
        std::cout << "live species count tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
