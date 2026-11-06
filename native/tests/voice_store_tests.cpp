#include <atlas/presets.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sqlite3.h>

namespace {
void check(bool passed, const char *message) {
    if (!passed)
        throw std::runtime_error(message);
}
template <class F> void error(atlas::VoiceStoreErrorCode expected, F action) {
    try {
        action();
    } catch (const atlas::VoiceStoreError &failure) {
        check(failure.code() == expected, "Wrong voice store error code");
        return;
    }
    throw std::runtime_error("Expected voice store failure");
}
struct Temporary {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("atlas-voices-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(directory); }
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
        const auto utf8 = path.u8string();
        const std::string filename(reinterpret_cast<const char *>(utf8.data()),
                                   utf8.size());
        if (sqlite3_open(filename.c_str(), &db_) != SQLITE_OK)
            throw std::runtime_error("fixture SQLite open failed");
    }
    ~RawDatabase() { sqlite3_close(db_); }
    void run(const char *sql) {
        if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error("fixture SQLite failed");
    }
    int integer(const char *sql) {
        sqlite3_stmt *query{};
        if (sqlite3_prepare_v2(db_, sql, -1, &query, nullptr) != SQLITE_OK)
            throw std::runtime_error("fixture query failed");
        const int stepped = sqlite3_step(query);
        const int value = sqlite3_column_int(query, 0);
        sqlite3_finalize(query);
        if (stepped != SQLITE_ROW)
            throw std::runtime_error("fixture row missing");
        return value;
    }

  private:
    sqlite3 *db_{};
};
bool sameSettings(const atlas::VoiceSettings &a,
                  const atlas::VoiceSettings &b) {
    return a.tuningSemitones == b.tuningSemitones &&
           a.attackSeconds == b.attackSeconds &&
           a.decaySeconds == b.decaySeconds && a.gainDb == b.gainDb &&
           a.character == b.character && a.brightness == b.brightness &&
           a.roughness == b.roughness && a.pulseHz == b.pulseHz &&
           a.sweepSemitones == b.sweepSemitones;
}
atlas::VoiceSnapshot edit(atlas::VoiceStore &store, std::string_view id,
                          std::string_view parameter, double value) {
    store.setParameter(id, parameter, value, atlas::VoiceGesture::begin);
    store.setParameter(id, parameter, value, atlas::VoiceGesture::change);
    return store.setParameter(id, parameter, value, atlas::VoiceGesture::end);
}
} // namespace
int main() {
    using namespace atlas;
    try {
        Temporary temp;
        const auto path = temp.file("voices.sqlite3");
        const std::array<VoiceSpecies, 2> species{
            {{"source:bird/1", "Bird"}, {"source:fish/2", "Fish"}}};
        {
            VoiceStore store(path, species);
            check(store.voices().size() == 3,
                  "Default and independent species missing");
            const auto initial = store.getVoice("default");
            check(initial.savedRevision == 0 && !initial.dirty &&
                      initial.settings.tuningSemitones == 0 &&
                      initial.settings.attackSeconds == .008 &&
                      initial.settings.decaySeconds == .650 &&
                      initial.settings.gainDb == 0,
                  "Wrong defaults");
            const auto preview = store.previewParameter("default", "gain", 0,
                                                        VoiceGesture::begin);
            check(preview.gestureActive &&
                      !store.getVoice("default").gestureActive,
                  "Preview mutated draft");
            store.setParameter("default", "tuning", .91, VoiceGesture::begin);
            error(VoiceStoreErrorCode::gestureActive,
                  [&] { store.save("default", 0); });
            error(VoiceStoreErrorCode::gestureActive,
                  [&] { store.revert("default"); });
            const auto changed =
                store.setParameter("default", "tuning", .91, VoiceGesture::end);
            check(changed.settings.tuningSemitones == 10 && changed.dirty,
                  "Tuning not quantized");
            check(!store.getVoice("species:source:bird/1").dirty &&
                      store.getVoice("species:source:bird/1")
                              .settings.tuningSemitones == 0,
                  "Species inherited default edit");
            const auto saved = store.save("default", 0);
            check(saved.savedRevision == 1 && !saved.dirty &&
                      saved.changeRevision > changed.changeRevision,
                  "Save state invalid");
            error(VoiceStoreErrorCode::notFound,
                  [&] { store.getVoice("unknown"); });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "sustain", .5,
                                   VoiceGesture::begin);
            });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "gain",
                                   std::numeric_limits<double>::quiet_NaN(),
                                   VoiceGesture::begin);
            });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "gain",
                                   std::numeric_limits<double>::infinity(),
                                   VoiceGesture::begin);
            });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "gain", 1.1, VoiceGesture::begin);
            });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "gain", .5, VoiceGesture::end);
            });
            error(VoiceStoreErrorCode::invalidArgument, [&] {
                store.setParameter("default", "gain", .5,
                                   static_cast<VoiceGesture>(99));
            });
            error(VoiceStoreErrorCode::conflict,
                  [&] { store.save("default", 0); });
            edit(store, "species:source:bird/1", "gain", .5);
            store.save("species:source:bird/1", 0);
            edit(store, "species:source:fish/2", "gain", 0);
        }
        {
            VoiceStore first(path), second(path);
            check(first.voices().size() == 2,
                  "Durable species absent from catalogue lost");
            check(first.getVoice("default").settings.tuningSemitones == 10,
                  "Saved values did not survive reopen");
            check(first.getVoice("species:source:bird/1").settings.gainDb ==
                      -30,
                  "Saved species values lost");
            error(VoiceStoreErrorCode::notFound,
                  [&] { first.getVoice("species:source:fish/2"); });
            first.registerSpecies(species);
            check(!first.getVoice("species:source:fish/2").dirty &&
                      first.getVoice("species:source:fish/2").settings.gainDb ==
                          0,
                  "Draft was autosaved");
            edit(first, "default", "attack", 1);
            first.save("default", 1);
            edit(second, "default", "decay", 1);
            error(VoiceStoreErrorCode::conflict,
                  [&] { second.save("default", 1); });
            check(second.getVoice("default").dirty, "Conflict destroyed draft");
            const auto restored = second.revert("default");
            check(restored.savedRevision == 2 && !restored.dirty &&
                      std::abs(restored.settings.attackSeconds - .100) < 1e-12,
                  "Revert did not reload durable latest");
            VoiceStore readOnly(path, {}, VoiceStoreAccess::readOnly);
            edit(readOnly, "default", "gain", .5);
            error(VoiceStoreErrorCode::readOnly,
                  [&] { readOnly.save("default", 2); });
            check(readOnly.getVoice("default").dirty,
                  "Read-only save destroyed draft");
        }
        {
            RawDatabase raw(path);
            check(raw.integer("PRAGMA user_version") == 2 &&
                      raw.integer("PRAGMA application_id") == 0x41545653,
                  "Wrong sidecar metadata");
            check(
                raw.integer(
                    "SELECT count(*) FROM sqlite_schema WHERE type='table'") ==
                    1,
                "Unexpected sidecar tables");
            check(raw.integer("SELECT count(*) FROM voice_presets") == 2,
                  "Draft leaked into sidecar");
            raw.run("BEGIN IMMEDIATE; UPDATE voice_presets SET gain=0 WHERE "
                    "voice_id='default'; ROLLBACK;");
        }
        {
            VoiceStore store(path);
            check(store.getVoice("default").settings.gainDb == 0,
                  "Rolled back transaction changed durable values");
            edit(store, "default", "gain", .75);
            RawDatabase blocker(path);
            blocker.run("BEGIN IMMEDIATE");
            error(VoiceStoreErrorCode::busy, [&] { store.save("default", 2); });
            blocker.run("ROLLBACK");
            check(store.getVoice("default").dirty,
                  "Interrupted save destroyed draft");
            store.save("default", 2);
            check(store.getVoice("default").savedRevision == 3,
                  "Retry save failed");
        }
        {
            const auto owner = temp.file("owner.sqlite3");
            RawDatabase raw(owner);
            raw.run("CREATE TABLE events(id INTEGER); PRAGMA user_version=3;");
            error(VoiceStoreErrorCode::unsupportedSchema,
                  [&] { VoiceStore rejected(owner); });
            check(raw.integer("PRAGMA user_version") == 3 &&
                      raw.integer("SELECT count(*) FROM sqlite_schema WHERE "
                                  "name='voice_presets'") == 0,
                  "Owner database modified");
        }
        {
            const auto full = temp.file("capacity.sqlite3");
            VoiceStore store(full);
            std::vector<VoiceSpecies> many;
            for (int i = 0; i < 512; ++i)
                many.push_back({std::to_string(i), "Species"});
            store.registerSpecies(many);
            check(store.voices().size() == 513, "Capacity boundary rejected");
            const std::array<VoiceSpecies, 2> more{
                {{"0", "Rename"}, {"extra", "Extra"}}};
            error(VoiceStoreErrorCode::full,
                  [&] { store.registerSpecies(more); });
            check(store.getVoice("species:0").name == "Species",
                  "Failed registration partially applied");
        }
        {
            RawDatabase raw(path);
            raw.run(
                "UPDATE voice_presets SET tuning=.51 WHERE voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET tuning=0,schema_version=99 "
                    "WHERE voice_id='default'");
            error(VoiceStoreErrorCode::unsupportedSchema,
                  [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET schema_version=2,gain='oops' "
                    "WHERE voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run(
                "UPDATE voice_presets SET gain=1e999 WHERE voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET gain=0,saved_revision=-1 WHERE "
                    "voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET saved_revision=3; PRAGMA "
                    "user_version=99;");
            error(VoiceStoreErrorCode::unsupportedSchema,
                  [&] { VoiceStore bad(path); });
        }
        {
            const auto capacity = temp.file("concurrent-capacity.sqlite3");
            VoiceStore first(capacity), other(capacity);
            const std::array<VoiceSpecies, 1> last{{{"last", "Last species"}}};
            other.registerSpecies(last);
            std::vector<VoiceSpecies> many;
            for (int i = 0; i < 512; ++i)
                many.push_back({std::to_string(i), "Species"});
            first.registerSpecies(many);
            for (const auto &entry : many)
                first.save("species:" + entry.speciesId, 0);
            error(VoiceStoreErrorCode::full,
                  [&] { other.save("species:last", 0); });
            first.save("default", 0);
            VoiceStore reopened(capacity);
            check(reopened.voices().size() == 513,
                  "512 durable species plus implicit default lost");
        }
        {
            const auto revisions = temp.file("revisions.sqlite3");
            {
                VoiceStore store(revisions);
                store.save("default", 0);
            }
            RawDatabase raw(revisions);
            raw.run("UPDATE voice_presets SET saved_revision=9007199254740993");
            {
                VoiceStore store(revisions);
                check(store.getVoice("default").savedRevision ==
                          9007199254740993ULL,
                      "Revision lost integer precision");
                check(
                    store.save("default", 9007199254740993ULL).savedRevision ==
                        9007199254740994ULL,
                    "Revision did not increment exactly");
            }
            raw.run(
                "UPDATE voice_presets SET saved_revision=9223372036854775807");
            VoiceStore exhausted(revisions);
            edit(exhausted, "default", "gain", .5);
            error(VoiceStoreErrorCode::full,
                  [&] { exhausted.save("default", 9223372036854775807ULL); });
            check(exhausted.getVoice("default").dirty,
                  "Revision exhaustion destroyed draft");
        }
        {
            const auto tooMany = temp.file("durable-capacity.sqlite3");
            {
                VoiceStore store(tooMany);
            }
            RawDatabase raw(tooMany);
            raw.run("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 "
                    "FROM n WHERE i<513) INSERT INTO voice_presets SELECT "
                    "'species:'||i,2,1,0,.008,.650,0,.5,0,0,0,0 FROM n");
            error(VoiceStoreErrorCode::full,
                  [&] { VoiceStore store(tooMany); });
        }
        {
            const auto generated = temp.file("unknown-column.sqlite3");
            RawDatabase raw(generated);
            raw.run("CREATE TABLE voice_presets(voice_id TEXT PRIMARY KEY NOT "
                    "NULL,schema_version INTEGER NOT NULL,saved_revision "
                    "INTEGER NOT NULL,tuning REAL NOT NULL,attack REAL NOT "
                    "NULL,decay REAL NOT NULL,gain REAL NOT NULL,unexpected "
                    "REAL GENERATED ALWAYS AS(gain) STORED) WITHOUT ROWID; "
                    "PRAGMA application_id=1096046163; PRAGMA user_version=1;");
            error(VoiceStoreErrorCode::corrupt,
                  [&] { VoiceStore rejected(generated); });
        }
        {
            const auto injection = temp.file("identifiers.sqlite3");
            const std::array<VoiceSpecies, 1> names{
                {{"x'); DROP TABLE voice_presets; --/../",
                  "Literal identifier"}}};
            VoiceStore store(injection, names);
            const auto id = "species:" + names[0].speciesId;
            edit(store, id, "gain", .5);
            store.save(id, 0);
            VoiceStore reopened(injection);
            check(reopened.getVoice(id).settings.gainDb == -30,
                  "Opaque identifier did not roundtrip safely");
            RawDatabase raw(injection);
            raw.run("DELETE FROM voice_presets");
            error(VoiceStoreErrorCode::corrupt, [&] { reopened.revert(id); });
        }
        {
            const auto legacy = temp.file("legacy-v1.sqlite3");
            {
                RawDatabase raw(legacy);
                raw.run(
                    "CREATE TABLE voice_presets(voice_id TEXT PRIMARY KEY NOT "
                    "NULL,schema_version INTEGER NOT NULL,saved_revision "
                    "INTEGER NOT NULL,tuning REAL NOT NULL,attack REAL NOT "
                    "NULL,decay REAL NOT NULL,gain REAL NOT NULL) WITHOUT "
                    "ROWID; PRAGMA application_id=1096046163; PRAGMA "
                    "user_version=1; INSERT INTO voice_presets "
                    "VALUES('default',1,9007199254740993,.5,0,1,.25),('species:"
                    "legacy:bird',1,7,1,1,0,0)");
            }
            VoiceSettings legacyDefault, legacySpecies;
            {
                VoiceStore readOnly(legacy, {}, VoiceStoreAccess::readOnly);
                const auto global = readOnly.getVoice("default");
                legacyDefault = global.settings;
                legacySpecies =
                    readOnly.getVoice("species:legacy:bird").settings;
                check(
                    global.savedRevision == 9007199254740993ULL &&
                        global.settings.tuningSemitones == 0 &&
                        global.settings.attackSeconds == .002 &&
                        global.settings.decaySeconds == 3 &&
                        global.settings.gainDb == -45 &&
                        global.settings.character == VoiceCharacter::struck &&
                        global.settings.brightness == .5 &&
                        global.settings.roughness == 0 &&
                        global.settings.pulseHz == 0 &&
                        global.settings.sweepSemitones == 0,
                    "Legacy values/default semantics changed before migration");
                RawDatabase raw(legacy);
                check(raw.integer("PRAGMA user_version") == 1,
                      "Read-only open migrated legacy file");
                error(VoiceStoreErrorCode::readOnly,
                      [&] { readOnly.save("default", global.savedRevision); });
            }
            {
                VoiceStore migrated(legacy);
                const auto global = migrated.getVoice("default"),
                           bird = migrated.getVoice("species:legacy:bird");
                check(sameSettings(global.settings, legacyDefault) &&
                          sameSettings(bird.settings, legacySpecies) &&
                          global.savedRevision == 9007199254740993ULL &&
                          bird.savedRevision == 7 && !global.dirty &&
                          !bird.dirty,
                      "V1 migration changed four audible values, revision or "
                      "draft state");
                RawDatabase raw(legacy);
                check(raw.integer("PRAGMA user_version") == 2 &&
                          raw.integer("SELECT count(*) FROM "
                                      "pragma_table_xinfo('voice_presets')") ==
                              12 &&
                          raw.integer("SELECT count(*) FROM sqlite_schema "
                                      "WHERE name='legacy_voice_presets'") ==
                              0 &&
                          raw.integer(
                              "SELECT schema_version=2 AND tuning=0 AND "
                              "attack=.002 AND decay=3 AND gain=-45 AND "
                              "brightness=.5 AND roughness=0 AND pulse_hz=0 "
                              "AND sweep_semitones=0 AND character=0 FROM "
                              "voice_presets WHERE voice_id='default'") == 1,
                      "V1 migration did not persist the complete physical v2 "
                      "tuple");
            }
            VoiceStore reopened(legacy);
            check(sameSettings(reopened.getVoice("default").settings,
                               legacyDefault),
                  "Migrated physical values changed at second reopen");
        }
        {
            const auto malformedLegacy =
                temp.file("malformed-legacy-v1.sqlite3");
            RawDatabase raw(malformedLegacy);
            raw.run("CREATE TABLE voice_presets(voice_id TEXT PRIMARY KEY NOT "
                    "NULL,schema_version INTEGER NOT NULL,saved_revision "
                    "INTEGER NOT NULL,tuning REAL NOT NULL,attack REAL NOT "
                    "NULL,decay REAL NOT NULL,gain REAL NOT NULL) WITHOUT "
                    "ROWID; PRAGMA application_id=1096046163; PRAGMA "
                    "user_version=1; INSERT INTO voice_presets "
                    "VALUES('default',1,3,.5,0,1,.25),('species:bad',1,1,.5,0,"
                    "1,1e999)");
            error(VoiceStoreErrorCode::corrupt,
                  [&] { VoiceStore rejected(malformedLegacy); });
            check(
                raw.integer("PRAGMA user_version") == 1 &&
                    raw.integer("SELECT count(*) FROM "
                                "pragma_table_xinfo('voice_presets')") == 7 &&
                    raw.integer("SELECT count(*) FROM voice_presets") == 2 &&
                    raw.integer("SELECT schema_version=1 AND gain=.25 AND "
                                "saved_revision=3 FROM voice_presets WHERE "
                                "voice_id='default'") == 1 &&
                    raw.integer("SELECT count(*) FROM sqlite_schema WHERE "
                                "name='legacy_voice_presets'") == 0,
                "Malformed migration changed schema, valid rows or revisions");
            raw.run("UPDATE voice_presets SET gain=.25 WHERE "
                    "voice_id='species:bad'");
            VoiceStore retried(malformedLegacy);
            check(retried.getVoice("default").settings.gainDb == -45,
                  "Migration rollback could not be retried");
        }
        {
            const auto exactPath = temp.file("exact-v2.sqlite3");
            VoiceSettings exact{-4, .01357924680, .87654321012, -17.2345678910};
            exact.character = VoiceCharacter::reed;
            exact.brightness = .12345678901234567;
            exact.roughness = .9876543210987654;
            exact.pulseHz = 7.65432109876543;
            exact.sweepSemitones = -3.123456789012345;
            const std::array<VoiceProfile, 1> profiles{{{"", exact}}};
            {
                VoiceStore store(exactPath);
                auto staged = store.stageProfiles(profiles);
                check(store.getVoice("default").settings.character ==
                          VoiceCharacter::struck,
                      "Stage mutated live drafts");
                check(store.commitProfiles(std::move(staged)),
                      "Valid staged profile did not commit");
                check(!store.commitProfiles(std::move(staged)),
                      "Staged profile committed twice");
                const auto draft = store.getVoice("default");
                check(sameSettings(draft.settings, exact) && draft.dirty &&
                          draft.savedRevision == 0,
                      "Stage lost exact physical values or changed saved base");
                RawDatabase raw(exactPath);
                check(raw.integer("SELECT count(*) FROM voice_presets") == 0,
                      "Draft restore wrote durable presets");
                store.save("default", 0);
            }
            VoiceStore reopened(exactPath);
            const auto savedExact = reopened.getVoice("default");
            check(
                sameSettings(savedExact.settings, exact) &&
                    savedExact.savedRevision == 1 && !savedExact.dirty,
                "Exact eight values plus character changed after save/restart");
            edit(reopened, "default", "brightness", .9);
            const auto edited = reopened.getVoice("default");
            check(edited.settings.character == exact.character &&
                      edited.settings.attackSeconds == exact.attackSeconds &&
                      edited.settings.decaySeconds == exact.decaySeconds &&
                      edited.settings.roughness == exact.roughness &&
                      edited.settings.pulseHz == exact.pulseHz &&
                      edited.settings.sweepSemitones == exact.sweepSemitones,
                  "Editing one normalized value roundtripped unrelated "
                  "physical values");
            check(sameSettings(reopened.revert("default").settings, exact),
                  "Revert did not restore complete physical tuple");
        }
        {
            const auto stagePath = temp.file("staged.sqlite3");
            VoiceStore store(stagePath),
                other(temp.file("other-stage.sqlite3"));
            VoiceSettings desired{2, .023, 1.4, -9};
            desired.character = VoiceCharacter::bell;
            desired.brightness = .8;
            desired.roughness = .3;
            desired.pulseHz = 5;
            desired.sweepSemitones = 4;
            const std::array<VoiceProfile, 2> profiles{
                {{"", desired}, {"stage:bird", desired}}};
            const std::array<VoiceSpecies, 1> names{
                {{"stage:bird", "Restored bird"}}};
            const auto original = store.getVoice("default");
            {
                auto abandoned = store.stageProfiles(profiles, names);
            }
            check(sameSettings(store.getVoice("default").settings,
                               original.settings) &&
                      store.voices().size() == 1,
                  "Abandoned stage mutated drafts/catalogue");
            auto crossOwner = store.stageProfiles(profiles, names);
            check(!other.commitProfiles(std::move(crossOwner)) &&
                      other.voices().size() == 1,
                  "Other owner committed a foreign token");
            check(store.commitProfiles(std::move(crossOwner)) &&
                      store.getVoice("species:stage:bird").name ==
                          "Restored bird",
                  "Wrong-owner refusal damaged legitimate staged token");
            check(store.getVoice("default").changeRevision ==
                      store.getVoice("species:stage:bird").changeRevision,
                  "Atomic profile restore published mixed revisions");
            const std::array<VoiceProfile, 1> subset{
                {{"stage:bird", VoiceSettings{}}}};
            auto subsetUpdate = store.stageProfiles(subset);
            check(store.commitProfiles(std::move(subsetUpdate)),
                  "Single-species subset restore failed");
            check(sameSettings(store.getVoice("default").settings, desired) &&
                      store.getVoice("species:stage:bird").settings.character ==
                          VoiceCharacter::struck,
                  "Subset restore changed unrelated fallback");
            auto staleEdit = store.stageProfiles(profiles);
            edit(store, "default", "gain", .8);
            check(!store.commitProfiles(std::move(staleEdit)),
                  "Intervening edit did not stale staged profiles");
            const std::array<VoiceSpecies, 1> newSpecies{
                {{"stage:new", "New after stage"}}};
            auto staleRegistration = store.stageProfiles(profiles);
            store.registerSpecies(newSpecies);
            check(!store.commitProfiles(std::move(staleRegistration)) &&
                      store.getVoice("species:stage:new").name ==
                          "New after stage",
                  "Stage discarded intervening species registration");
            auto staleRename = store.stageProfiles(profiles);
            const std::array<VoiceSpecies, 1> renamed{
                {{"stage:new", "Renamed after stage"}}};
            store.registerSpecies(renamed);
            check(!store.commitProfiles(std::move(staleRename)) &&
                      store.getVoice("species:stage:new").name ==
                          "Renamed after stage",
                  "Stage discarded intervening catalogue rename");
            auto noOp = store.stageProfiles(profiles);
            store.registerSpecies(renamed);
            check(store.commitProfiles(std::move(noOp)),
                  "No-op registration incorrectly invalidated stage");
            auto staleSave = store.stageProfiles(profiles);
            store.save("default", 0);
            check(!store.commitProfiles(std::move(staleSave)),
                  "Intervening save did not stale stage");
            auto staleRevert = store.stageProfiles(profiles);
            store.revert("default");
            check(!store.commitProfiles(std::move(staleRevert)),
                  "Intervening revert did not stale stage");
            store.setParameter("default", "attack", .4, VoiceGesture::begin);
            error(VoiceStoreErrorCode::gestureActive,
                  [&] { store.stageProfiles(profiles); });
            store.setParameter("default", "attack", .4, VoiceGesture::end);
            auto staleGesture = store.stageProfiles(profiles);
            store.setParameter("default", "attack", .5, VoiceGesture::begin);
            check(!store.commitProfiles(std::move(staleGesture)) &&
                      store.getVoice("default").gestureActive,
                  "Stale stage erased an active gesture");
            store.setParameter("default", "attack", .5, VoiceGesture::end);
            const auto beforeBad = store.getVoice("default");
            const std::array<VoiceProfile, 2> duplicate{
                {{"", desired}, {"", desired}}};
            error(VoiceStoreErrorCode::invalidArgument,
                  [&] { store.stageProfiles(duplicate); });
            const std::array<VoiceSpecies, 1> badNames{{{"stage:unknown", ""}}};
            error(VoiceStoreErrorCode::invalidArgument,
                  [&] { store.stageProfiles(profiles, badNames); });
            const std::array<VoiceProfile, 2> lateFallback{
                {{"stage:bird", desired}, {"", desired}}};
            error(VoiceStoreErrorCode::invalidArgument,
                  [&] { store.stageProfiles(lateFallback); });
            check(sameSettings(store.getVoice("default").settings,
                               beforeBad.settings) &&
                      store.getVoice("default").changeRevision ==
                          beforeBad.changeRevision,
                  "Invalid stage partially mutated a live draft");
            RawDatabase raw(stagePath);
            check(raw.integer("SELECT count(*) FROM voice_presets") == 1,
                  "Scene staging accidentally saved species drafts");
        }
        {
            std::optional<VoiceStore::DraftUpdate> orphan;
            const std::array<VoiceProfile, 1> profiles{{{"", VoiceSettings{}}}};
            {
                VoiceStore old(temp.file("old-owner.sqlite3"));
                orphan.emplace(old.stageProfiles(profiles));
            }
            VoiceStore replacement(temp.file("new-owner.sqlite3"));
            check(!replacement.commitProfiles(std::move(*orphan)),
                  "Expired store lifetime token committed into replacement");
        }
        {
            const auto candidatePath = temp.file("candidate-defaults.sqlite3");
            const std::array<VoiceSpecies, 3> catalogue{
                {{"birdweather:species:2298", "Ruru"},
                 {"birdweather:species:102", "Kotare"},
                 {"birdweather:species:17651", "Unresolved bats"}}};
            const auto *ruru =
                speciesPreset("candidate:birdweather:species:2298:v1");
            const auto *kotare =
                speciesPreset("candidate:birdweather:species:102:v1");
            check(ruru && kotare, "Candidate fixtures absent");
            VoiceSettings persisted;
            {
                VoiceStore store(candidatePath, catalogue);
                const auto first =
                    store.getVoice("species:birdweather:species:2298");
                const auto second =
                    store.getVoice("species:birdweather:species:102");
                check(sameSettings(first.settings, ruru->settings) &&
                          sameSettings(second.settings, kotare->settings) &&
                          !sameSettings(first.settings, second.settings) &&
                          !first.dirty && !second.dirty &&
                          first.savedRevision == 0 && second.savedRevision == 0,
                      "Unpersisted species did not start at independent clean "
                      "candidate bases");
                check(
                    sameSettings(store.getVoice("default").settings,
                                 VoiceSettings{}) &&
                        sameSettings(
                            store.getVoice("species:birdweather:species:17651")
                                .settings,
                            VoiceSettings{}),
                    "Neutral fallback/unresolved species inferred a candidate "
                    "animal voice");
                edit(store, first.id, "gain", .5);
                check(sameSettings(store.revert(first.id).settings,
                                   ruru->settings),
                      "Unsaved Revert lost candidate base");
                edit(store, first.id, "gain", .6);
                persisted = store.save(first.id, 0).settings;
                RawDatabase raw(candidatePath);
                check(raw.integer("SELECT count(*) FROM voice_presets") == 1,
                      "Automatic candidates were saved implicitly");
            }
            VoiceStore reopened(candidatePath, catalogue);
            check(sameSettings(
                      reopened.getVoice("species:birdweather:species:2298")
                          .settings,
                      persisted) &&
                      sameSettings(
                          reopened.getVoice("species:birdweather:species:102")
                              .settings,
                          kotare->settings),
                  "Candidate lookup overwrote saved voice or failed to restore "
                  "unsaved base");
        }
        std::cout << "Voice drafts, normalized values, durable CAS, rollback, "
                     "read-only, corruption and capacity passed\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
