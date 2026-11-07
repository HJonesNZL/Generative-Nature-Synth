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
        for (int i = 626; i < 638; ++i)
            add(db, i);
        batch = live.poll(512);
        check(batch.received == 12 && batch.payloads.size() == 12 &&
              batch.dropped == 0);
        add(db, 638);
        batch = live.poll(0);
        check(batch.received == 1 && batch.payloads.empty() &&
              batch.dropped == 1);
        check(live.poll(512).payloads.empty());
        // FIFO mode retains every eligible record beyond both page and caller
        // capacity, while consuming invalid/corrected records only once.
        live.baseline();
        for (int i = 639; i < 1839; ++i)
            add(db, i);
        batch = live.pollFifo(0);
        check(batch.hasMore && batch.payloads.empty());
        std::size_t admitted{};
        std::string previous;
        std::int64_t previousOffset = -1;
        do {
            batch = live.pollFifo(37, 1000000);
            check(batch.dropped == 0 && batch.skippedRevisions == 0 &&
                  batch.payloads.size() <= 37);
            check(batch.smoothTiming.size() == batch.payloads.size());
            for (const auto &timing : batch.smoothTiming) {
                check(timing.sourceId == "bird" && timing.offsetUs >= previousOffset &&
                      timing.offsetUs <= 900000000);
                previousOffset = timing.offsetUs;
            }
            for (const auto &payload : batch.payloads) {
                check(payload != previous);
                previous = payload;
                ++admitted;
            }
        } while (batch.hasMore);
        check(admitted == 1200 && previousOffset == 1000000 &&
              live.pollFifo().payloads.empty());
        add(db, 1839, "corrected");
        add(db, 1840, "new", true, "imported");
        add(db, 1841);
        batch = live.pollFifo(1);
        check(batch.payloads.size() == 1 && batch.dropped == 1 &&
              batch.received == 2 && !batch.hasMore);
        // Separate source collection with reversed provider observation order:
        // spacing remains monotonic across pages, retaining three observed seconds.
        for (int i = 1842; i < 1846; ++i) {
            add(db, i);
            const auto observed = "2026-11-07T01:00:0" + std::to_string(1845 - i) + "Z";
            sql(db, "UPDATE revisions SET ingested_at='2026-11-07T01:00:10Z',observed_at='" +
                    observed + "',event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:00:10Z','$.observed_at_utc','" + observed + "') WHERE seq=" +
                    std::to_string(i));
        }
        batch = live.pollFifo(2);
        check(batch.smoothTiming.size() == 2 && batch.hasMore &&
              batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs > 0);
        const auto secondOffset = batch.smoothTiming[1].offsetUs;
        batch = live.pollFifo(2);
        check(batch.smoothTiming.size() == 2 && !batch.hasMore &&
              batch.smoothTiming[0].offsetUs > secondOffset &&
              batch.smoothTiming[1].offsetUs == 3000000);
        // Ascending unequal observation gaps are retained proportionally.
        for (int i = 1846; i < 1849; ++i) {
            add(db, i);
            const auto seconds = i == 1846 ? "00" : (i == 1847 ? "01" : "10");
            const auto observed = std::string("2026-11-07T01:00:") + seconds + "Z";
            sql(db, "UPDATE revisions SET ingested_at='2026-11-07T01:00:20Z',observed_at='" +
                    observed + "',event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:00:20Z','$.observed_at_utc','" + observed + "') WHERE seq=" +
                    std::to_string(i));
        }
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 3 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 1000000 &&
              batch.smoothTiming[2].offsetUs == 10000000);
        // Pending FIFO rows must not survive privacy/store replacement.
        add(db, 1849);
        sql(db, "UPDATE metadata SET value='fifo-reset'");
        check(live.pollFifo().storeReset);
        check(live.pollFifo().payloads.empty());
        for (int i = 1850; i < 2550; ++i)
            add(db, i);
        batch = live.pollFifo(512);
        check(batch.payloads.size() == 512 && batch.hasMore && batch.dropped == 0);
        batch = live.pollFifo(512);
        check(batch.payloads.size() == 188 && !batch.hasMore && batch.dropped == 0);
        // Deleted sequence gaps do not turn into long runs of empty polls.
        add(db, 1000000);
        batch = live.pollFifo();
        check(batch.payloads.size() == 1 && !batch.hasMore);
        // Repeated timestamps mixed with irregular ascending observations
        // retain unequal data gaps, with distinct neutral intra-timestamp onsets.
        for (int i = 1000001; i <= 1000004; ++i) {
            add(db, i);
            const auto seconds = i < 1000003 ? "00" : (i == 1000003 ? "01" : "10");
            const auto observed = std::string("2026-11-07T01:00:") + seconds + "Z";
            sql(db, "UPDATE revisions SET ingested_at='2026-11-07T01:01:00Z',observed_at='" +
                    observed + "',event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:01:00Z','$.observed_at_utc','" + observed + "') WHERE seq=" +
                    std::to_string(i));
        }
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 4 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs > batch.smoothTiming[0].offsetUs &&
              batch.smoothTiming[2].offsetUs > batch.smoothTiming[1].offsetUs &&
              batch.smoothTiming[3].offsetUs > batch.smoothTiming[2].offsetUs &&
              batch.smoothTiming[3].offsetUs == 10000000);
        // An empty/partial successful poll measures cadence even though it
        // created no revisions. It prevents stretching to the last animal event.
        sql(db, "CREATE TABLE polls(source_id TEXT,completed_at TEXT,complete INTEGER);"
                "CREATE INDEX polls_completed ON polls(source_id,completed_at);"
                "INSERT INTO polls VALUES('bird','2026-11-07T01:01:55Z',0)");
        for (int i = 1000005; i <= 1000006; ++i) {
            add(db, i);
            sql(db, "UPDATE revisions SET ingested_at='2026-11-07T01:02:00Z',"
                    "event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:02:00Z') WHERE seq=" + std::to_string(i));
        }
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 5000000);
        // Monotonic newest-first batches retain genuine unequal reverse gaps.
        for (int i = 1000007; i <= 1000009; ++i) {
            add(db, i);
            const auto seconds = i == 1000007 ? "10" : (i == 1000008 ? "09" : "00");
            const auto observed = std::string("2026-11-07T01:00:") + seconds + "Z";
            sql(db, "UPDATE revisions SET ingested_at='2026-11-07T01:03:00Z',observed_at='" +
                    observed + "',event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:03:00Z','$.observed_at_utc','" + observed + "') WHERE seq=" +
                    std::to_string(i));
        }
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 3 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 1000000 &&
              batch.smoothTiming[2].offsetUs == 10000000);
        // A short poll interval does not squash a real 60-second observation
        // span: genuine observed timing is retained within normal source cadence.
        sql(db, "INSERT INTO polls VALUES('bird','2026-11-07T01:03:55Z',1)");
        for (int i = 1000010; i <= 1000011; ++i) {
            add(db, i);
            const auto observed = i == 1000010 ? "2026-11-07T01:00:00Z"
                                              : "2026-11-07T01:01:00Z";
            sql(db, std::string("UPDATE revisions SET ingested_at='2026-11-07T01:04:00Z',observed_at='") +
                    observed + "',event_json=json_set(event_json,'$.ingested_at_utc',"
                    "'2026-11-07T01:04:00Z','$.observed_at_utc','" + observed + "') WHERE seq=" +
                    std::to_string(i));
        }
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 60000000);
        sql(db, "ALTER TABLE polls ADD COLUMN details TEXT NOT NULL DEFAULT '{}'");
        const auto addTimed = [&](int seq, const std::string &ingested,
                                  const std::string &observed) {
            add(db, seq);
            sql(db, "UPDATE revisions SET ingested_at='" + ingested +
                    "',observed_at='" + observed +
                    "',event_json=json_set(event_json,'$.ingested_at_utc','" +
                    ingested + "','$.observed_at_utc','" + observed +
                    "') WHERE seq=" + std::to_string(seq));
        };
        sql(db, "INSERT INTO polls VALUES('bird','2026-11-07T01:04:00Z',1,"
                "'{\"configured_poll_seconds\":60}')");
        addTimed(1000012, "2026-11-07T01:05:00Z", "2026-11-07T01:00:00Z");
        addTimed(1000013, "2026-11-07T01:05:00Z", "2026-11-07T01:00:02Z");
        addTimed(1000014, "2026-11-07T01:05:00Z", "2026-11-07T01:00:14Z");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 3 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 2000000 &&
              batch.smoothTiming[2].offsetUs == 14000000);
        sql(db, "INSERT INTO polls VALUES('bird','2026-11-07T01:05:00Z',1,"
                "'{\"configured_poll_seconds\":60}')");
        addTimed(1000015, "2026-11-07T01:06:00Z", "2026-11-07T01:00:00Z");
        addTimed(1000016, "2026-11-07T01:06:00Z", "2026-11-07T01:00:00Z");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 && batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 60000000);
        // An hour's outage is bounded by this source's configured minute.
        sql(db, "INSERT INTO polls VALUES('bird','2026-11-07T01:06:00Z',1,"
                "'{\"configured_poll_seconds\":60}')");
        addTimed(1000017, "2026-11-07T02:06:00Z", "2026-11-07T01:00:00Z");
        addTimed(1000018, "2026-11-07T02:06:00Z", "2026-11-07T01:00:00Z");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 && batch.smoothTiming[1].offsetUs == 60000000);
        // Fast fixtures use genuine source poll cadence, not a playback override.
        sql(db, "INSERT INTO polls VALUES('bird','2026-11-07T02:06:00Z',1,"
                "'{\"configured_poll_seconds\":2}')");
        addTimed(1000019, "2026-11-07T02:06:02Z", "2026-11-07T01:00:00Z");
        addTimed(1000020, "2026-11-07T02:06:02Z", "2026-11-07T01:00:00Z");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 && batch.smoothTiming[1].offsetUs == 2000000);
        addTimed(1000021, "2026-11-07T02:06:04Z", "2026-11-07T01:00:00Z");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 1 && batch.smoothTiming[0].offsetUs == 0);
        // Regional empty polls retain collector identity, while detections use
        // canonical provider source_id. Bind through the current collection.
        sql(db, "CREATE INDEX polls_at_completion ON polls(completed_at,source_id);"
                "INSERT INTO polls VALUES('birdweather:oceania','2026-11-07T03:00:00Z',1,"
                "'{\"configured_poll_seconds\":60}');"
                "INSERT INTO polls VALUES('birdweather:oceania','2026-11-07T03:00:02Z',1,"
                "'{\"configured_poll_seconds\":60}')");
        addTimed(1000022, "2026-11-07T03:00:02Z", "2026-11-07T01:00:00Z");
        addTimed(1000023, "2026-11-07T03:00:02Z", "2026-11-07T01:00:00Z");
        sql(db, "UPDATE events SET id='birdweather:'||substr(id,6) "
                "WHERE revision_seq IN (1000022,1000023);"
                "UPDATE revisions SET source_id='birdweather',event_id='birdweather:'||seq,"
                "event_json=json_set(event_json,'$.source_id','birdweather',"
                "'$.event_id','birdweather:'||seq) WHERE seq IN (1000022,1000023)");
        batch = live.pollFifo();
        check(batch.smoothTiming.size() == 2 &&
              batch.smoothTiming[0].sourceId == "birdweather" &&
              batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 2000000);
        // Collect's frontier excludes existing rows and delayed older provider
        // observations in every live mode while preserving stored history.
        const auto boundary = atlas::captureLiveSession(db, "2026-11-07T01:00:10Z");
        check(boundary.baselineRevision == 1000023 && !boundary.id.empty());
        live.setSession(boundary);
        addTimed(1000024, "2026-11-07T04:00:00Z", "2026-11-07T01:00:09Z");
        addTimed(1000025, "2026-11-07T04:00:00Z", "2026-11-07T01:00:10Z");
        addTimed(1000026, "2026-11-07T04:00:00Z", "2026-11-07T01:00:12Z");
        batch = live.poll(512);
        check(batch.payloads.size() == 2 && batch.dropped == 0);
        const auto restarted = atlas::captureLiveSession(db, "2026-11-07T01:00:20Z");
        check(restarted.id != boundary.id && restarted.baselineRevision == 1000026);
        live.setSession(restarted);
        addTimed(1000027, "2026-11-07T04:01:00Z", "2026-11-07T01:00:19Z");
        addTimed(1000028, "2026-11-07T04:01:00Z", "2026-11-07T01:00:20Z");
        addTimed(1000029, "2026-11-07T04:01:00Z", "2026-11-07T01:00:22Z");
        batch = live.pollFifo();
        check(batch.payloads.size() == 2 && batch.smoothTiming.size() == 2 &&
              batch.smoothTiming[0].offsetUs == 0 &&
              batch.smoothTiming[1].offsetUs == 2000000);
        sqlite3_close(db);
        std::cout << "Live admission checks passed\n";
    } catch (const std::exception &e) {
        if (db)
            sqlite3_close(db);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
