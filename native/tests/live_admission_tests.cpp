#include <atlas/live_admission.hpp>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("Live admission regression");
}
void sql(sqlite3 *db, const std::string &text) {
    if (sqlite3_exec(db, text.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
void add(sqlite3 *db, int seq, const std::string &kind = "new",
         bool eligible = true, const std::string &provenance = "live_source",
         const std::string &eventKind = "acoustic_species_detection",
         const std::string &locationKind = "station_detection") {
    const auto id = "bird:" + std::to_string(seq);
    const auto payload =
        "{\"schema_version\":1,\"event_id\":\"" + id +
        "\",\"source_id\":\"bird\",\"provider_event_id\":\"" +
        std::to_string(seq) +
        "\",\"observed_at_utc\":\"2026-11-07T01:00:00Z\",\"first_seen_at_utc\":"
        "\"2026-11-07T01:00:00Z\",\"ingested_at_utc\":\"2026-11-07T01:00:00Z\","
        "\"event_kind\":\"" +
        eventKind + "\",\"location_kind\":\"" + locationKind +
        "\",\"latitude\":null,\"longitude\":null,\"coordinate_order\":"
        "\"latitude,longitude\",\"provenance_mode\":\"" +
        provenance + "\"}";
    sql(db, "INSERT INTO "
            "revisions(seq,event_id,change_kind,live_eligible,event_json,"
            "source_id,observed_at,first_seen_at,ingested_at) VALUES(" +
                std::to_string(seq) + ",'" + id + "','" + kind + "'," +
                (eligible ? "1" : "0") + ",'" + payload +
                "','bird','2026-11-07T01:00:00Z','2026-11-07T01:00:00Z','2026-"
                "11-07T01:00:00Z')");
    sql(db, "INSERT OR REPLACE INTO events VALUES('" + id + "'," +
                std::to_string(seq) + ")");
}
} // namespace
int main() {
    sqlite3 *db{};
    try {
        check(sqlite3_open(":memory:", &db) == SQLITE_OK);
        sql(db, "CREATE TABLE metadata(key TEXT,value TEXT);INSERT INTO "
                "metadata VALUES('store_id','a');CREATE TABLE revisions(seq "
                "INTEGER PRIMARY KEY,event_id TEXT,change_kind "
                "TEXT,live_eligible INTEGER,event_json TEXT,source_id "
                "TEXT,observed_at TEXT,first_seen_at TEXT,ingested_at "
                "TEXT);CREATE TABLE "
                "events(id TEXT PRIMARY KEY,revision_seq INTEGER)");
        add(db, 1);
        atlas::LiveAdmission live(db);
        live.baseline();
        check(live.poll().payloads.empty()); // No startup backlog.
        add(db, 2);
        add(db, 3, "corrected");
        add(db, 4, "baseline");
        add(db, 5, "new", false);
        add(db, 6, "new", true, "imported");
        auto batch = live.poll();
        check(batch.received == 2 && batch.payloads.size() == 1 &&
              batch.dropped == 1);
        check(live.poll()
                  .payloads.empty()); // Duplicate polling never retriggers.
        add(db, 7);
        sql(db, "UPDATE events SET revision_seq=8 WHERE id='bird:7'");
        check(live.poll()
                  .payloads.empty()); // Superseded/withdrawn records excluded.
        for (int i = 8; i < 20; ++i)
            add(db, i);
        batch = live.poll();
        check(batch.received == 12 && batch.payloads.size() == 8 &&
              batch.dropped == 4);
        check(live.poll().payloads.empty()); // Overflow never deferred.
        for (int i = 20; i < 620; ++i)
            add(db, i);
        batch = live.poll();
        check(batch.skippedRevisions == 88 && batch.received == 512 &&
              batch.dropped == 504 && batch.payloads.size() == 8);
        sql(db, "UPDATE metadata SET value='b'");
        add(db, 620);
        check(live.poll().payloads.empty()); // Replaced store baselines.
        add(db, 621);
        live.baseline();
        check(
            live.poll()
                .payloads.empty()); // Reconnect baseline skips pending backlog.
        add(db, 622, "new", true, "live_source", "human_observation",
            "reported_location");
        add(db, 623, "new", true, "live_source", "satellite_fix",
            "individual_fix");
        add(db, 624, "new", true, "live_source", "acoustic_receiver_detection",
            "receiver_detection");
        batch = live.poll();
        check(batch.received == 3 && batch.payloads.size() == 3 &&
              batch.dropped == 0);
        add(db, 625);
        sql(db, "UPDATE revisions SET "
                "event_json=json_set(event_json,'$.event_id','bird:wrong') "
                "WHERE seq=625");
        batch = live.poll();
        check(batch.received == 1 && batch.dropped == 1 &&
              batch.payloads.empty());
        sql(db, "UPDATE metadata SET value='privacy-reset';DELETE FROM events "
                "WHERE id='bird:622';DELETE FROM revisions WHERE "
                "event_id='bird:622'");
        check(live.poll().storeReset);
        sqlite3_close(db);
        std::cout << "Live admission checks passed\n";
    } catch (const std::exception &e) {
        if (db)
            sqlite3_close(db);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
