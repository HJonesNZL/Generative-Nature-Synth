#include "../src/sha256.hpp"
#include <array>
#include <atlas/soundscapes.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
        std::string material = "14:atlas-scene-v" + scalar("PRAGMA user_version");
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
bool sameSpatial(const SpatialSettings &a, const SpatialSettings &b) {
    if (a.enabled != b.enabled || a.amount != b.amount ||
        a.regions.size() != b.regions.size())
        return false;
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        const auto &x = a.regions[i], &y = b.regions[i];
        if (x.id != y.id || x.name != y.name || x.south != y.south ||
            x.north != y.north || x.west != y.west || x.east != y.east ||
            x.pan != y.pan || x.gainDb != y.gainDb || x.send != y.send ||
            x.echoMix != y.echoMix || x.delayMs != y.delayMs ||
            x.feedback != y.feedback || x.roomSeconds != y.roomSeconds ||
            x.tone != y.tone || x.positionPan != y.positionPan)
            return false;
    }
    return true;
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
        auto legacyCapture = capture(saved);
        legacyCapture.schemaRevision = 1;
        legacyCapture.synthesisRevision = 2;
        legacyCapture.mappingRevision = 1;
        auto nonrepresentableLegacy = legacyCapture;
        nonrepresentableLegacy.audio.spatial.amount = .5;
        rejected([&] { captureSoundscape(timeline, nonrepresentableLegacy); });
        const auto legacy = captureSoundscape(timeline, legacyCapture);
        // Recorded before the schema-2/composition implementation. This oracle
        // seals the original metadata/profile/event framing, not a new hash.
        check(legacy.contentHash() ==
                  "a417408c4c8926f1a793e4a8d78cea7313094fbeb6aaa5bf7d3cf64ef16749bc",
              "Schema-1 content hash changed");
        const auto legacySamples = render(legacy);
        const auto *baseline = std::getenv("ATLAS_VERIFY_LEGACY_PCM_BASELINE");
        if (baseline && std::string_view(baseline) == "1") {
            // Exact artifact comparison with the pre-change arm64 Apple-clang
            // baseline. Float byte order/libm are not portable file contracts.
            detail::Sha256 legacyPcm;
            legacyPcm.update(std::string_view(
                reinterpret_cast<const char *>(legacySamples.data()),
                legacySamples.size() * sizeof(float)));
            check(legacyPcm.hexDigest() ==
                      "0f68e996870aa97401273e03f1bf7f7ef1f746a2d5fee340cefc59520197762e",
                  "Schema-1 legacy PCM baseline changed");
            std::cout << "Exact arm64 legacy PCM baseline passed\n";
        } else {
            std::cout << "Exact platform PCM artifact comparison not requested; "
                         "set ATLAS_VERIFY_LEGACY_PCM_BASELINE=1 on its "
                         "recorded platform\n";
        }
        const auto legacyPath = temp.file("legacy.atlas-scene");
        saveSoundscape(legacyPath, legacy);
        const auto legacyLoaded = loadSoundscape(legacyPath);
        check(legacyLoaded.capture().schemaRevision == 1 &&
                  legacyLoaded.capture().synthesisRevision == 2 &&
                  legacyLoaded.capture().mappingRevision == 1 &&
                  !legacyLoaded.capture().audio.composition.enabled &&
                  legacyLoaded.contentHash() == legacy.contentHash() &&
                  render(legacyLoaded) == legacySamples,
              "Legacy scene migrated or changed audible recipe");
        const auto legacyCopy = temp.file("legacy-copy.atlas-scene");
        saveSoundscape(legacyCopy, legacyLoaded);
        check(loadSoundscape(legacyCopy).contentHash() == legacy.contentHash(),
              "Saving recalled legacy scene silently upgraded its hash");
        {
            RawDatabase db(legacyCopy);
            check(db.scalar("PRAGMA user_version") == "1",
                  "Saving legacy scene silently upgraded its schema");
            db.run("UPDATE scene_profiles SET brightness=.2 WHERE slot=1");
        }
        rejected([&] { loadSoundscape(legacyCopy); });
        auto expectedCapture = capture(saved);
        expectedCapture.audio.composition = {
            true, CompositionScale::naturalMinor, 9, 127,
            CompositionDensity::active, std::nextafter(.37, 1.0),
            std::numeric_limits<std::uint32_t>::max(), false};
        expectedCapture.schemaRevision = 2;
        expectedCapture.synthesisRevision = 3;
        expectedCapture.mappingRevision = 2;
        const auto compositionOnly = captureSoundscape(timeline, expectedCapture);
        check(compositionOnly.contentHash() ==
                  "55377fdffad0fc41a1c3e8fa0048b73d75324488c59b5f709b3c617330606e02",
              "Schema-2 composition content hash changed");
        const auto compositionSamples = render(compositionOnly);
        if (baseline && std::string_view(baseline) == "1") {
            detail::Sha256 pcm;
            pcm.update(std::string_view(
                reinterpret_cast<const char *>(compositionSamples.data()),
                compositionSamples.size() * sizeof(float)));
            check(pcm.hexDigest() ==
                      "6e39c8aaf34d2617dd97154387f46d4fdcbf377798665c984b265dd886aea98c",
                  "Schema-2 composition PCM baseline changed");
            std::cout << "Exact arm64 composition PCM baseline passed\n";
        }
        const auto compositionPath = temp.file("composition-v2.atlas-scene");
        saveSoundscape(compositionPath, compositionOnly);
        const auto compositionLoaded = loadSoundscape(compositionPath);
        check(compositionLoaded.capture().schemaRevision == 2 &&
                  compositionLoaded.capture().synthesisRevision == 3 &&
                  compositionLoaded.capture().mappingRevision == 2 &&
                  sameSpatial(compositionLoaded.capture().audio.spatial,
                              SpatialSettings{}) &&
                  compositionLoaded.contentHash() ==
                      compositionOnly.contentHash() &&
                  render(compositionLoaded) == compositionSamples,
              "Schema-2 scene migrated or changed PCM");
        const auto compositionCopy = temp.file("composition-v2-copy.atlas-scene");
        saveSoundscape(compositionCopy, compositionLoaded);
        {
            RawDatabase db(compositionCopy);
            check(db.scalar("PRAGMA user_version") == "2" &&
                      loadSoundscape(compositionCopy).contentHash() ==
                          compositionOnly.contentHash(),
                  "Saving schema-2 scene silently migrated its identity");
        }
        auto nonrepresentable = expectedCapture;
        nonrepresentable.audio.spatial.amount = .5;
        rejected([&] { captureSoundscape(timeline, nonrepresentable); });
        expectedCapture.schemaRevision = soundscapeSchemaRevision;
        expectedCapture.synthesisRevision = soundscapeSynthesisRevision;
        expectedCapture.mappingRevision = soundscapeMappingRevision;
        RegionSettings pacific;
        pacific.id = "pacific";
        pacific.name = "Te Moana – Pacific";
        pacific.south = -60;
        pacific.north = 10;
        pacific.west = 160;
        pacific.east = -160;
        pacific.pan = -.73;
        pacific.gainDb = -7;
        pacific.send = .81;
        pacific.echoMix = .63;
        pacific.delayMs = std::nextafter(137.25, 200.0);
        pacific.feedback = .37;
        pacific.roomSeconds = .83;
        pacific.tone = .29;
        pacific.positionPan = true;
        RegionSettings world;
        world.id = "world";
        world.name = "Second overlapping region";
        world.pan = .67;
        world.gainDb = -11;
        world.send = .41;
        world.echoMix = .17;
        world.delayMs = 219.75;
        world.feedback = .19;
        world.roomSeconds = 1.73;
        world.tone = .87;
        expectedCapture.audio.spatial = {
            true, std::nextafter(.42, 1.0), {pacific, world}};
        const auto scene = captureSoundscape(timeline, expectedCapture);
        auto adjacentVariation = expectedCapture;
        adjacentVariation.audio.composition.variation =
            std::nextafter(expectedCapture.audio.composition.variation, 1.0);
        check(captureSoundscape(timeline, adjacentVariation).contentHash() !=
                  scene.contentHash(),
              "Adjacent representable composition doubles share a hash");
        auto adjacentSpatial = expectedCapture;
        adjacentSpatial.audio.spatial.regions[0].delayMs =
            std::nextafter(pacific.delayMs, 200.0);
        check(captureSoundscape(timeline, adjacentSpatial).contentHash() !=
                  scene.contentHash(),
              "Adjacent representable region doubles share a hash");
        const auto originalSamples = render(scene);
        auto reordered = expectedCapture;
        std::swap(reordered.audio.spatial.regions[0],
                  reordered.audio.spatial.regions[1]);
        const auto reorderedScene = captureSoundscape(timeline, reordered);
        check(reorderedScene.contentHash() != scene.contentHash() &&
                  render(reorderedScene) != originalSamples,
              "Region precedence/order lost from hash or rendering");
        const auto target = temp.file("original.atlas-scene");
        saveSoundscape(target, scene);
        {
            RawDatabase db(target);
            check(db.scalar("PRAGMA application_id") == "1096045379" &&
                      db.scalar("PRAGMA user_version") == "3",
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
        const auto &composition = recalled.capture().audio.composition;
        check(recalled.capture().schemaRevision == 3 &&
                  recalled.capture().synthesisRevision == 4 &&
                  recalled.capture().mappingRevision == 3 &&
                  composition.enabled &&
                  composition.scale == CompositionScale::naturalMinor &&
                  composition.tonicPitchClass == 9 &&
                  composition.tempoBpm == 127 &&
                  composition.density == CompositionDensity::active &&
                  composition.variation ==
                      expectedCapture.audio.composition.variation &&
                  composition.seed ==
                      std::numeric_limits<std::uint32_t>::max() &&
                  !composition.quantize,
              "Complete exact composition settings lost");
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
        check(sameSpatial(recalled.capture().audio.spatial,
                          expectedCapture.audio.spatial),
              "Complete ordered spatial recipe lost");
        bool stereo{};
        for (std::size_t i = 0; i < originalSamples.size(); i += 2)
            stereo = stereo || originalSamples[i] != originalSamples[i + 1];
        check(stereo, "Spatial recall fixture did not exercise stereo");
        auto maximumRegions = expectedCapture;
        while (maximumRegions.audio.spatial.regions.size() < 8) {
            auto extra = world;
            extra.id =
                "extra-" +
                std::to_string(maximumRegions.audio.spatial.regions.size());
            maximumRegions.audio.spatial.regions.push_back(std::move(extra));
        }
        auto disabledSpatial = expectedCapture;
        disabledSpatial.audio.spatial.enabled = false;
        const auto disabledScene = captureSoundscape(timeline, disabledSpatial);
        const auto disabledPath = temp.file("disabled-regions.atlas-scene");
        saveSoundscape(disabledPath, disabledScene);
        check(sameSpatial(loadSoundscape(disabledPath).capture().audio.spatial,
                          disabledSpatial.audio.spatial),
              "Disabled spatial configuration silently dropped its regions");
        const auto maximumScene = captureSoundscape(timeline, maximumRegions);
        const auto maximumPath = temp.file("eight-regions.atlas-scene");
        saveSoundscape(maximumPath, maximumScene);
        check(sameSpatial(loadSoundscape(maximumPath).capture().audio.spatial,
                          maximumRegions.audio.spatial),
              "Maximum complete region header lost or truncated");
        maximumRegions.audio.spatial.regions.push_back(world);
        rejected([&] { captureSoundscape(timeline, maximumRegions); });
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
        invalid.audio.spatial.regions[1].id = pacific.id;
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.audio.spatial.enabled = false;
        invalid.audio.spatial.regions[0].pan =
            std::numeric_limits<double>::quiet_NaN();
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.audio.composition.variation =
            std::numeric_limits<double>::quiet_NaN();
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.schemaRevision = 1;
        invalid.synthesisRevision = 2;
        invalid.mappingRevision = 1;
        rejected([&] { captureSoundscape(timeline, invalid); });
        invalid = expectedCapture;
        invalid.synthesisRevision = 2;
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
        for (const auto &[key, value] :
             std::array<std::pair<const char *, const char *>, 9>{{
                 {"composition_enabled", "2"}, {"composition_scale", "4"},
                 {"composition_tonic_pitch_class", "12"},
                 {"composition_tempo_bpm", "39"}, {"composition_density", "3"},
                 {"composition_variation", "1.1"},
                 {"composition_variation", "0.3700"},
                 {"composition_seed", "4294967296"},
                 {"composition_quantize", "-1"}}}) {
            mutate(std::string(key) + value,
                   "UPDATE scene_meta SET value='" + std::string(value) +
                       "' WHERE key='" + key + "'", true);
        }
        mutate("composition-valid-change",
               "UPDATE scene_meta SET value='91' WHERE "
               "key='composition_tempo_bpm'",
               true);
        mutate("composition-unknown",
               "INSERT INTO scene_meta VALUES('composition_future','1')",
               true);
        mutate("composition-missing",
               "DELETE FROM scene_meta WHERE key='composition_variation'",
               true);
        for (const auto &[key, value] :
             std::array<std::pair<const char *, const char *>, 19>{{
                 {"spatial_enabled", "2"}, {"spatial_amount", "1.1"},
                 {"region_count", "9"}, {"region_count", "-1"},
                 {"region_0_south", "-91"}, {"region_0_north", "-60"},
                 {"region_0_west", "181"}, {"region_0_east", "160"},
                 {"region_0_pan", "1.1"}, {"region_0_gain_db", "1"},
                 {"region_0_send", "-0.1"}, {"region_0_echo_mix", "1.1"},
                 {"region_0_delay_ms", "19"}, {"region_0_feedback", "0.51"},
                 {"region_0_room_seconds", "2.6"}, {"region_0_tone", "-0.1"},
                 {"region_0_position_pan", "2"},
                 {"region_0_delay_ms", "137.2500"},
                 {"region_0_id", "invalid id"}}}) {
            mutate(std::string(key) + value,
                   "UPDATE scene_meta SET value='" + std::string(value) +
                       "' WHERE key='" + key + "'", true);
        }
        mutate("region-name-invalid-utf8",
               "UPDATE scene_meta SET value=CAST(X'80' AS TEXT) "
               "WHERE key='region_0_name'", true);
        mutate("region-name-control",
               "UPDATE scene_meta SET value=CAST(X'610062' AS TEXT) "
               "WHERE key='region_0_name'", true);
        mutate("region-name-overlong",
               "UPDATE scene_meta SET value=replace(hex(zeroblob(65)),'0','x') "
               "WHERE key='region_0_name'", true);
        mutate("region-id-duplicate",
               "UPDATE scene_meta SET value='pacific' WHERE key='region_1_id'",
               true);
        mutate("region-blob-type",
               "UPDATE scene_meta SET value=CAST(value AS BLOB) "
               "WHERE key='region_0_delay_ms'", true);
        mutate("region-valid-resealed-edit",
               "UPDATE scene_meta SET value='138' WHERE key='region_0_delay_ms'",
               true);
        mutate("region-unknown-field",
               "INSERT INTO scene_meta VALUES('region_0_future','1')", true);
        mutate("region-unknown-ordinal",
               "INSERT INTO scene_meta VALUES('region_8_id','unknown')", true);
        mutate("region-missing-field",
               "DELETE FROM scene_meta WHERE key='region_1_name'", true);
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
