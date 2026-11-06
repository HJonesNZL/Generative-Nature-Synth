#include "../src/sha256.hpp"
#include <array>
#include <atlas/soundscapes.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <sqlite3.h>

namespace {
using namespace atlas;
void check(bool passed, const char *message) {
    if (!passed)
        throw std::runtime_error(message);
}
template <class F> void rejected(F action) {
    try {
        action();
    } catch (const SoundscapeError &) {
        return;
    }
    throw std::runtime_error("Malformed soundscape accepted");
}
template <class F> void error(SoundscapeErrorCode code, F action) {
    try {
        action();
    } catch (const SoundscapeError &failure) {
        check(failure.code() == code, "Wrong soundscape error code");
        return;
    }
    throw std::runtime_error("Expected soundscape error");
}
struct Temporary {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("atlas-scenes-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directory(directory); }
    ~Temporary() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    std::filesystem::path file(std::string_view name) const {
        return directory / name;
    }
};
class RawDatabase {
  public:
    explicit RawDatabase(const std::filesystem::path &path) {
        const auto bytes = path.u8string();
        const std::string filename(reinterpret_cast<const char *>(bytes.data()),
                                   bytes.size());
        if (sqlite3_open(filename.c_str(), &db_) != SQLITE_OK)
            throw std::runtime_error("Fixture open failed");
    }
    ~RawDatabase() { sqlite3_close(db_); }
    void run(const std::string &sql) {
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) !=
            SQLITE_OK)
            throw std::runtime_error("Fixture SQL failed");
    }
    std::string scalar(const char *sql) {
        sqlite3_stmt *query{};
        check(sqlite3_prepare_v2(db_, sql, -1, &query, nullptr) == SQLITE_OK,
              "Fixture query failed");
        check(sqlite3_step(query) == SQLITE_ROW, "Fixture row absent");
        const auto *bytes = sqlite3_column_text(query, 0);
        const std::string result(reinterpret_cast<const char *>(bytes));
        sqlite3_finalize(query);
        return result;
    }
    void resealHeader() {
        sqlite3_stmt *query{};
        check(sqlite3_prepare_v2(db_,
                                 "SELECT key,value FROM scene_meta WHERE "
                                 "key!='header_hash' ORDER BY key",
                                 -1, &query, nullptr) == SQLITE_OK,
              "Fixture seal query failed");
        std::string material = "14:atlas-scene-v1";
        while (sqlite3_step(query) == SQLITE_ROW) {
            for (int column = 0; column < 2; ++column) {
                const auto size = sqlite3_column_bytes(query, column);
                material += std::to_string(size) + ":";
                material.append(reinterpret_cast<const char *>(
                                    sqlite3_column_text(query, column)),
                                static_cast<std::size_t>(size));
            }
        }
        sqlite3_finalize(query);
        detail::Sha256 hash;
        hash.update(material);
        run("UPDATE scene_meta SET value='" + hash.hexDigest() +
            "' WHERE key='header_hash'");
    }

  private:
    sqlite3 *db_{};
};
const std::array<std::string, 2> sourceEvents{
    R"({"schema_version":1,"event_id":"fixture:b","source_id":"fixture","provider_event_id":"b","observed_at_utc":"2026-11-07T00:00:01Z","event_kind":"acoustic_species_detection","location_kind":"station_detection","species_id":"fixture:bird","station_id":"fixture:station","latitude":-41.2,"longitude":174.7,"coordinate_order":"latitude,longitude","provenance_mode":"demo","first_seen_at_utc":"2026-11-07T00:00:00Z","ingested_at_utc":"2026-11-07T00:00:02Z","quality_flags":[],"permissions_status":"synthetic fixture"})",
    R"({"schema_version":1,"event_id":"fixture:a","source_id":"fixture","provider_event_id":"a","observed_at_utc":"2026-11-07T00:00:00Z","event_kind":"gps_fix","location_kind":"individual_fix","species_id":"fixture:fish","individual_id":"fixture:individual","deployment_id":"fixture:deployment","latitude":null,"longitude":null,"uncertainty_m":12,"coordinate_order":"latitude,longitude","provenance_mode":"demo","first_seen_at_utc":"2026-11-07T00:00:02Z","ingested_at_utc":"2026-11-07T00:00:02Z","quality_flags":["uncertain"]})"};
void makeSource(const std::filesystem::path &path) {
    RawDatabase db(path);
    db.run(
        "PRAGMA user_version=2; CREATE TABLE sessions(id TEXT,name TEXT,state "
        "TEXT,origin TEXT,event_count INTEGER,options_json TEXT,content_hash "
        "TEXT,created_at TEXT); CREATE TABLE session_events(session_id "
        "TEXT,ordinal INTEGER,original_seq INTEGER,event_json TEXT)");
    detail::Sha256 hash;
    for (std::size_t i = 0; i < sourceEvents.size(); ++i) {
        const auto sequence = std::to_string(i + 11);
        hash.update("{\"event\":");
        hash.update(sourceEvents[i]);
        hash.update(",\"original_seq\":");
        hash.update(sequence);
        hash.update("}\n");
        db.run("INSERT INTO session_events VALUES('source'," +
               std::to_string(i) + "," + sequence + ",'" + sourceEvents[i] +
               "')");
    }
    db.run("INSERT INTO sessions VALUES('source','Synthetic "
           "source','frozen','demo',2,'{\"timeline_bounds\":{\"observed\":["
           "\"2026-11-07T00:00:00Z\",\"2026-11-07T00:00:04Z\"],\"arrival\":["
           "\"2026-11-07T00:00:00Z\",\"2026-11-07T00:00:05Z\"]},\"window_"
           "policy\":\"preserve_requested\",\"capture_truncated\":true}','" +
           hash.hexDigest() + "','2026-11-07T00:00:00Z')");
}
SoundscapeCapture capture(const SavedSession &session) {
    SoundscapeCapture result;
    result.name = "Independent frozen soundscape";
    result.sourceLibraryLabel = "Disposable synthetic library";
    result.session = session;
    result.basis = TimeBasis::observed;
    result.speed = {2, 1};
    result.bounds = session.observedBounds;
    result.audio = {44100, 59, .009, .78, -19};
    VoiceSettings fallback{1, .010, .9, -7};
    fallback.character = VoiceCharacter::air;
    fallback.brightness = .4;
    fallback.roughness = .2;
    fallback.pulseHz = 3;
    fallback.sweepSemitones = -2;
    VoiceSettings bird{-2, .012, .8, -4};
    bird.character = VoiceCharacter::whistle;
    bird.brightness = .7;
    bird.roughness = .1;
    bird.pulseHz = 7;
    bird.sweepSemitones = 5;
    VoiceSettings fish{3, .004, 1.2, -11};
    fish.character = VoiceCharacter::pulse;
    fish.brightness = .1;
    fish.roughness = .8;
    fish.pulseHz = 2;
    fish.sweepSemitones = -7;
    result.profiles = {
        {"", fallback}, {"fixture:bird", bird}, {"fixture:fish", fish}};
    return result;
}
std::vector<float> render(const Soundscape &scene) {
    AudioPlan plan(scene.timeline(), scene.capture().audio,
                   scene.capture().profiles);
    AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "Fixture render failed");
    std::vector<float> samples(static_cast<std::size_t>(plan.totalFrames()) *
                               2);
    std::size_t offset{};
    while (offset < samples.size()) {
        const auto count = std::min<std::size_t>(512, samples.size() - offset);
        check(renderer.process(std::span(samples).subspan(offset, count)),
              "Fixture process failed");
        offset += count;
    }
    check(renderer.statistics().faults == 0, "Fixture render fault");
    return samples;
}
} // namespace
int main() {
    try {
        Temporary temp;
        const auto source = temp.file("disposable-source.sqlite3");
        makeSource(source);
        SavedSession saved;
        const auto timeline = ReadOnlyLibrary(source).load(
            "source", TimeBasis::observed, {2, 1}, &saved);
        const auto expectedCapture = capture(saved);
        const auto scene = captureSoundscape(timeline, expectedCapture);
        const auto originalSamples = render(scene);
        const auto target = temp.file("original.atlas-scene");
        saveSoundscape(target, scene);
        {
            RawDatabase db(target);
            check(db.scalar("PRAGMA application_id") == "1096045379" &&
                      db.scalar("PRAGMA user_version") == "1",
                  "Scene identity wrong");
            check(db.scalar("SELECT count(*) FROM sqlite_schema") == "3",
                  "Unexpected scene schema");
            check(db.scalar("SELECT count(*) FROM scene_events") == "2",
                  "Copied material missing");
            check(
                db.scalar(
                    "SELECT original_seq FROM scene_events WHERE ordinal=0") ==
                    "12",
                "Timeline ordering lost");
        }
        std::filesystem::remove(source);
        const auto inspected = inspectSoundscape(target);
        check(inspected.valid && !inspected.contentVerified &&
                  inspected.contentHash == scene.contentHash() &&
                  inspected.sourceSessionId == saved.id &&
                  inspected.eventCount == 2,
              "Sealed metadata inspection lost header or claimed full "
              "validation");
        const auto recalled = loadSoundscape(target);
        check(recalled.contentHash() == scene.contentHash() &&
                  recalled.capture().session.contentHash == saved.contentHash,
              "Scene or original provenance hash lost");
        check(recalled.capture().session.metadataIntegrity ==
                      "legacy-metadata-unverified" &&
                  recalled.capture().session.truncated,
              "Provenance quality upgraded or truncation lost");
        check(recalled.capture().bounds == saved.observedBounds &&
                  recalled.timeline().durationUs() == 2000000 &&
                  recalled.timeline().events()[0].id == "fixture:a",
              "Original timeline or event identity lost");
        check(recalled.capture().audio.sampleRate == 44100 &&
                  recalled.capture().audio.rootMidi == 59 &&
                  recalled.capture().audio.masterDb == -19,
              "Global settings lost");
        check(recalled.capture().profiles.size() == 3 &&
                  recalled.capture().profiles[0].settings.character ==
                      VoiceCharacter::air &&
                  recalled.capture().profiles[1].settings.pulseHz == 7 &&
                  recalled.capture().profiles[2].settings.sweepSemitones == -7,
              "Complete independent profiles lost");
        check(originalSamples == render(recalled),
              "Source-independent recalled PCM changed");
        error(SoundscapeErrorCode::alreadyExists,
              [&] { saveSoundscape(target, scene); });
        check(loadSoundscape(target).contentHash() == scene.contentHash(),
              "No-overwrite damaged scene");
        auto invalid = expectedCapture;
        invalid.profiles.pop_back();
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.profiles[0].speciesId = "not-fallback";
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.speed = {3, 1};
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.bounds->second += 1;
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.session.arrivalBounds =
            std::pair<Microseconds, Microseconds>{2, 1};
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.audio.masterDb = std::numeric_limits<double>::quiet_NaN();
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.profiles[1].settings.brightness = 2;
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.profiles[1].settings.character =
            static_cast<VoiceCharacter>(99);
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.profiles.resize(514);
        rejected([&] { captureSoundscape(timeline, invalid); });
        auto changedEvents = timeline.events();
        changedEvents.front().position = Position{1, 2};
        Timeline changedTimeline(changedEvents, expectedCapture.basis,
                                 expectedCapture.speed, expectedCapture.bounds);
        rejected([&] { captureSoundscape(changedTimeline, expectedCapture); });
        changedEvents = timeline.events();
        changedEvents.front().normalizedJson += std::string(16384, ' ');
        Timeline oversizedEvent(changedEvents, expectedCapture.basis,
                                expectedCapture.speed, expectedCapture.bounds);
        rejected([&] { captureSoundscape(oversizedEvent, expectedCapture); });
        const auto mutate = [&](const std::string &name, const std::string &sql,
                                bool reseal = false) {
            const auto path = temp.file(name + ".atlas-scene");
            std::filesystem::copy_file(target, path);
            {
                RawDatabase db(path);
                db.run(sql);
                if (reseal)
                    db.resealHeader();
            }
            rejected([&] { loadSoundscape(path); });
        };
        mutate("globals",
               "UPDATE scene_meta SET value='-20' WHERE key='master_db'");
        mutate("valid-globals-resealed",
               "UPDATE scene_meta SET value='-20' WHERE key='master_db'", true);
        mutate("profile-tuning",
               "UPDATE scene_profiles SET tuning=tuning+1 WHERE slot=1");
        mutate("profile-character",
               "UPDATE scene_profiles SET character=0 WHERE slot=1");
        mutate("profile-brightness",
               "UPDATE scene_profiles SET brightness=.2 WHERE slot=1");
        mutate("profile-roughness",
               "UPDATE scene_profiles SET roughness=.9 WHERE slot=1");
        mutate("profile-pulse",
               "UPDATE scene_profiles SET pulse_hz=1 WHERE slot=1");
        mutate("profile-sweep",
               "UPDATE scene_profiles SET sweep_semitones=0 WHERE slot=1");
        mutate("profile-bad-type",
               "UPDATE scene_profiles SET brightness='oops' WHERE slot=1");
        mutate("profile-missing", "DELETE FROM scene_profiles WHERE slot=2");
        mutate("profile-duplicate",
               "UPDATE scene_profiles SET species_id='' WHERE slot=1");
        mutate("event-corrupt",
               "UPDATE scene_events SET event_json='{}' WHERE ordinal=0");
        mutate("event-valid-change", "UPDATE scene_events SET "
                                     "event_json=json_set(event_json,'$."
                                     "uncertainty_m',14) WHERE ordinal=0");
        mutate("event-unknown",
               "UPDATE scene_events SET "
               "event_json=json_set(event_json,'$.future',1) WHERE ordinal=0");
        mutate("event-sequence",
               "UPDATE scene_events SET original_seq=999 WHERE ordinal=0");
        mutate("event-ordinal",
               "UPDATE scene_events SET ordinal=9 WHERE ordinal=0");
        mutate("unknown-field", "INSERT INTO scene_meta VALUES('future','1')",
               true);
        mutate("missing-field", "DELETE FROM scene_meta WHERE key='root_midi'",
               true);
        mutate("unknown-column",
               "ALTER TABLE scene_profiles ADD COLUMN future REAL");
        mutate("unknown-generated-column",
               "ALTER TABLE scene_profiles ADD COLUMN future REAL GENERATED "
               "ALWAYS AS(gain) VIRTUAL");
        mutate("unknown-table", "CREATE TABLE future(value)");
        mutate("unknown-version", "PRAGMA user_version=99");
        mutate("foreign-application", "PRAGMA application_id=0");
        mutate(
            "unknown-engine",
            "UPDATE scene_meta SET value='999' WHERE key='synthesis_revision'",
            true);
        mutate("unknown-mapping",
               "UPDATE scene_meta SET value='999' WHERE key='mapping_revision'",
               true);
        mutate("reversed-bounds",
               "UPDATE scene_meta SET value='9223372036854775807' WHERE "
               "key='selected_start_us'",
               true);
        mutate("count",
               "UPDATE scene_meta SET value='50001' WHERE key='event_count'",
               true);
        mutate(
            "duration",
            "UPDATE scene_meta SET value='1800000001' WHERE key='duration_us'",
            true);
        mutate("speed",
               "UPDATE scene_meta SET value='0' WHERE key='speed_denominator'",
               true);
        mutate("noncanonical-number",
               "UPDATE scene_meta SET value='-19.0' WHERE key='master_db'",
               true);
        check(inspectSoundscape(temp.file("profile-tuning.atlas-scene")).valid,
              "Metadata inspection unexpectedly parsed full event/profile "
              "content");
        rejected([&] { inspectSoundscape(temp.file("globals.atlas-scene")); });
        rejected(
            [&] { inspectSoundscape(temp.file("unknown-field.atlas-scene")); });
        const auto list = listSoundscapes(temp.directory, 100);
        std::size_t valid{}, corrupt{};
        for (const auto &entry : list) {
            check(!entry.contentVerified,
                  "Metadata listing claimed full content verification");
            if (entry.valid)
                ++valid;
            else {
                ++corrupt;
                check(!entry.error.empty(), "Corrupt entry silently omitted");
            }
        }
        check(valid > 0 && corrupt > 0,
              "Bounded listing did not expose valid and invalid entries");
        check(listSoundscapes(temp.directory, 1).size() == 1,
              "List limit ignored");
        error(SoundscapeErrorCode::invalidArgument,
              [&] { listSoundscapes(temp.directory, 101); });
        const auto blocked = temp.file("blocked.atlas-scene");
        std::filesystem::create_directory(blocked.string() + ".saving");
        error(SoundscapeErrorCode::alreadyExists,
              [&] { saveSoundscape(blocked, scene); });
        check(!std::filesystem::exists(blocked),
              "Failed save published partial file");
        const auto symlink = temp.file("symlink.atlas-scene");
        std::filesystem::create_symlink(target, symlink);
        error(SoundscapeErrorCode::alreadyExists,
              [&] { saveSoundscape(symlink, scene); });
        rejected([&] { loadSoundscape(symlink); });
        {
            std::ofstream garbage(temp.file("garbage.atlas-scene"));
            garbage << "not sqlite";
        }
        rejected([&] { loadSoundscape(temp.file("garbage.atlas-scene")); });
        const auto truncated = temp.file("truncated.atlas-scene");
        std::filesystem::copy_file(target, truncated);
        std::filesystem::resize_file(truncated, 100);
        rejected([&] { loadSoundscape(truncated); });
        const auto journal = temp.file("journal.atlas-scene");
        std::filesystem::copy_file(target, journal);
        {
            std::ofstream out(journal.string() + "-wal");
            out << "incomplete";
        }
        rejected([&] { loadSoundscape(journal); });
        error(SoundscapeErrorCode::notFound,
              [&] { loadSoundscape(temp.file("absent.atlas-scene")); });
        const auto oversizedFile = temp.file("oversized.atlas-scene");
        {
            std::ofstream sparse(oversizedFile);
        }
        std::filesystem::resize_file(oversizedFile, 68ULL * 1024 * 1024 + 1);
        error(SoundscapeErrorCode::capacity,
              [&] { loadSoundscape(oversizedFile); });
        Temporary crowded;
        for (int i = 0; i < 1001; ++i) {
            std::ofstream file(crowded.file(std::to_string(i)));
        }
        error(SoundscapeErrorCode::capacity,
              [&] { listSoundscapes(crowded.directory); });
        std::cout << "Soundscape atomic save, strict schema, provenance, "
                     "complete profiles, independent recall/PCM, corrupt input "
                     "and bounded listing passed\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
