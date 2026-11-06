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
            check(raw.integer("PRAGMA user_version") == 1 &&
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
            raw.run("UPDATE voice_presets SET tuning=.5,schema_version=99 "
                    "WHERE voice_id='default'");
            error(VoiceStoreErrorCode::unsupportedSchema,
                  [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET schema_version=1,gain='oops' "
                    "WHERE voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run(
                "UPDATE voice_presets SET gain=1e999 WHERE voice_id='default'");
            error(VoiceStoreErrorCode::corrupt, [&] { VoiceStore bad(path); });
            raw.run("UPDATE voice_presets SET gain=1,saved_revision=-1 WHERE "
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
            for (const auto &species : many)
                first.save("species:" + species.speciesId, 0);
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
                    "'species:'||i,1,1,.5,.5,.5,1 FROM n");
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
        std::cout << "Voice drafts, normalized values, durable CAS, rollback, "
                     "read-only, corruption and capacity passed\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
