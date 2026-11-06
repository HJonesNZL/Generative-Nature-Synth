#include <algorithm>
#include <atlas/presets.hpp>
#include <atlas/voices.hpp>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sqlite3.h>

namespace atlas {
VoiceStoreError::VoiceStoreError(VoiceStoreErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
namespace {
using Code = VoiceStoreErrorCode;
constexpr int applicationId =
    0x41545653; // ATVS; never accept the animal library as a sidecar.
constexpr std::uint64_t maxRevision =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());
[[noreturn]] void fail(Code code, std::string message) {
    throw VoiceStoreError(code, std::move(message));
}
[[noreturn]] void sqlFail(int result) {
    switch (result & 255) {
    case SQLITE_READONLY:
        fail(Code::readOnly, "Voice sidecar is read-only");
    case SQLITE_FULL:
        fail(Code::full, "Voice sidecar storage is full");
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        fail(Code::busy, "Voice sidecar is busy");
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
    case SQLITE_SCHEMA:
    case SQLITE_CONSTRAINT:
    case SQLITE_ERROR:
        fail(Code::corrupt,
             "Voice sidecar is corrupt or has an invalid schema");
    default:
        fail(Code::storage, "Voice sidecar operation failed: " +
                                std::string(sqlite3_errstr(result)));
    }
}
void run(sqlite3 *db, const char *sql) {
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (result != SQLITE_OK)
        sqlFail(result);
}
class Query {
  public:
    Query(sqlite3 *db, const char *sql) {
        const int result = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
        if (result != SQLITE_OK)
            sqlFail(result);
    }
    ~Query() { sqlite3_finalize(stmt_); }
    Query(const Query &) = delete;
    Query &operator=(const Query &) = delete;
    void text(int index, std::string_view value) {
        const int result =
            sqlite3_bind_text(stmt_, index, value.data(),
                              static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (result != SQLITE_OK)
            sqlFail(result);
    }
    void integer(int index, std::uint64_t value) {
        const int result =
            sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value));
        if (result != SQLITE_OK)
            sqlFail(result);
    }
    void number(int index, double value) {
        const int result = sqlite3_bind_double(stmt_, index, value);
        if (result != SQLITE_OK)
            sqlFail(result);
    }
    bool next() {
        const int result = sqlite3_step(stmt_);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        sqlFail(result);
    }
    std::string text(int index) const {
        if (sqlite3_column_type(stmt_, index) != SQLITE_TEXT)
            fail(Code::corrupt, "Invalid voice text field");
        const auto *data = sqlite3_column_text(stmt_, index);
        const int size = sqlite3_column_bytes(stmt_, index);
        if (!data || size < 1 || size > 512)
            fail(Code::corrupt, "Invalid voice identifier length");
        return {reinterpret_cast<const char *>(data),
                static_cast<std::size_t>(size)};
    }
    std::uint64_t integer(int index) const {
        if (sqlite3_column_type(stmt_, index) != SQLITE_INTEGER ||
            sqlite3_column_int64(stmt_, index) < 0)
            fail(Code::corrupt, "Invalid voice integer field");
        return static_cast<std::uint64_t>(sqlite3_column_int64(stmt_, index));
    }
    double number(int index) const {
        const auto type = sqlite3_column_type(stmt_, index);
        if (type != SQLITE_FLOAT && type != SQLITE_INTEGER)
            fail(Code::corrupt, "Invalid voice numeric field");
        return sqlite3_column_double(stmt_, index);
    }

  private:
    sqlite3_stmt *stmt_{};
};
class Transaction {
  public:
    explicit Transaction(sqlite3 *db) : db_(db) { run(db_, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!committed_)
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        run(db_, "COMMIT");
        committed_ = true;
    }

  private:
    sqlite3 *db_;
    bool committed_{};
};
bool validId(std::string_view id, std::size_t limit = 256) {
    return !id.empty() && id.size() <= limit &&
           std::none_of(id.begin(), id.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
bool validVoiceId(std::string_view id) {
    return id == "default" ||
           (id.starts_with("species:") && validId(id.substr(8)));
}
double normalize(const VoiceParameterSpec &spec, double value) {
    return spec.curve == "log"
               ? std::log(value / spec.min) / std::log(spec.max / spec.min)
               : (value - spec.min) / (spec.max - spec.min);
}
double denormalize(const VoiceParameterSpec &spec, double value) {
    if (value == 0)
        return spec.min;
    if (value == 1)
        return spec.max;
    if (value == normalize(spec, spec.defaultValue))
        return spec.defaultValue;
    return spec.curve == "log" ? spec.min * std::pow(spec.max / spec.min, value)
                               : spec.min + value * (spec.max - spec.min);
}
VoiceValues defaults() {
    VoiceValues values{};
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = normalize(voiceParameterSpecs[i],
                              voiceParameterSpecs[i].defaultValue);
    return values;
}
bool validValues(const VoiceValues &values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = values[i];
        if (!std::isfinite(value) || value < 0 || value > 1)
            return false;
        const auto &spec = voiceParameterSpecs[i];
        if (spec.step > 0) {
            const auto steps =
                (denormalize(spec, value) - spec.min) / spec.step;
            if (std::abs(steps - std::round(steps)) >= 1e-10)
                return false;
        }
    }
    return true;
}
VoiceValues physicalValues(const VoiceSettings &s) {
    return {static_cast<double>(s.tuningSemitones),
            s.attackSeconds,
            s.decaySeconds,
            s.gainDb,
            s.brightness,
            s.roughness,
            s.pulseHz,
            s.sweepSemitones};
}
VoiceValues normalizedValues(const VoiceSettings &s) {
    auto values = physicalValues(s);
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = normalize(voiceParameterSpecs[i], values[i]);
    return values;
}
VoiceSettings physicalSettings(const VoiceValues &v, VoiceCharacter character) {
    return {static_cast<int>(std::lround(v[0])),
            v[1],
            v[2],
            v[3],
            character,
            v[4],
            v[5],
            v[6],
            v[7]};
}
VoiceSettings settings(const VoiceValues &values,
                       VoiceCharacter character = VoiceCharacter::struck) {
    auto physical = values;
    for (std::size_t i = 0; i < values.size(); ++i)
        physical[i] = denormalize(voiceParameterSpecs[i], values[i]);
    return physicalSettings(physical, character);
}
bool sameSettings(const VoiceSettings &a, const VoiceSettings &b) {
    return a.character == b.character && physicalValues(a) == physicalValues(b);
}
struct Durable {
    VoiceSettings settings;
    std::uint64_t revision{};
};
constexpr const char *legacyRows =
    "SELECT voice_id,schema_version,saved_revision,tuning,attack,decay,gain "
    "FROM voice_presets";
constexpr const char *currentRows =
    "SELECT "
    "voice_id,schema_version,saved_revision,tuning,attack,decay,gain,"
    "brightness,roughness,pulse_hz,sweep_semitones,character FROM "
    "voice_presets";
constexpr const char *createCurrent =
    "CREATE TABLE voice_presets(voice_id TEXT PRIMARY KEY NOT "
    "NULL,schema_version INTEGER NOT NULL,saved_revision INTEGER NOT "
    "NULL,tuning REAL NOT NULL,attack REAL NOT NULL,decay REAL NOT NULL,gain "
    "REAL NOT NULL,brightness REAL NOT NULL,roughness REAL NOT NULL,pulse_hz "
    "REAL NOT NULL,sweep_semitones REAL NOT NULL,character INTEGER NOT NULL) "
    "WITHOUT ROWID";
Durable readRow(Query &query, std::uint64_t schema) {
    if (query.integer(1) != schema)
        fail(Code::unsupportedSchema, "Unsupported saved voice schema");
    Durable result;
    result.revision = query.integer(2);
    if (!result.revision)
        fail(Code::corrupt, "Saved voice revision must be positive");
    if (schema == 1) {
        auto values = defaults();
        for (int i = 0; i < 4; ++i)
            values[static_cast<std::size_t>(i)] = query.number(i + 3);
        if (!validValues(values))
            fail(Code::corrupt, "Saved voice values are invalid");
        result.settings = settings(values);
    } else {
        VoiceValues values{};
        for (int i = 0; i < 8; ++i)
            values[static_cast<std::size_t>(i)] = query.number(i + 3);
        const auto character = query.integer(11);
        if (!std::isfinite(values[0]) || values[0] < -12 || values[0] > 12 ||
            std::round(values[0]) != values[0] ||
            character >= voiceCharacterIds.size())
            fail(Code::corrupt, "Invalid saved voice identity or tuning");
        result.settings =
            physicalSettings(values, static_cast<VoiceCharacter>(character));
        if (!validVoiceSettings(result.settings))
            fail(Code::corrupt, "Saved voice settings are invalid");
    }
    return result;
}
void writeRow(sqlite3 *db, std::string_view id, const Durable &voice) {
    Query write(
        db,
        "INSERT INTO "
        "voice_presets(voice_id,schema_version,saved_revision,tuning,attack,"
        "decay,gain,brightness,roughness,pulse_hz,sweep_semitones,character) "
        "VALUES(?,2,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(voice_id) DO UPDATE SET "
        "schema_version=2,saved_revision=excluded.saved_revision,tuning="
        "excluded.tuning,attack=excluded.attack,decay=excluded.decay,gain="
        "excluded.gain,brightness=excluded.brightness,roughness=excluded."
        "roughness,pulse_hz=excluded.pulse_hz,sweep_semitones=excluded.sweep_"
        "semitones,character=excluded.character");
    write.text(1, id);
    write.integer(2, voice.revision);
    const auto values = physicalValues(voice.settings);
    for (int i = 0; i < 8; ++i)
        write.number(i + 3, values[static_cast<std::size_t>(i)]);
    write.integer(11, static_cast<std::uint64_t>(voice.settings.character));
    (void)write.next();
}
void validateLayout(sqlite3 *db, std::uint64_t schema) {
    constexpr std::array<std::string_view, 12> names{
        "voice_id", "schema_version",  "saved_revision",
        "tuning",   "attack",          "decay",
        "gain",     "brightness",      "roughness",
        "pulse_hz", "sweep_semitones", "character"};
    Query layout(db, "SELECT cid,name,type,pk,\"notnull\",hidden FROM "
                     "pragma_table_xinfo('voice_presets') ORDER BY cid");
    std::size_t count{};
    const auto size = schema == 1 ? 7u : 12u;
    while (layout.next()) {
        const auto type = count == 0                                ? "TEXT"
                          : count == 1 || count == 2 || count == 11 ? "INTEGER"
                                                                    : "REAL";
        if (count >= size || layout.integer(0) != count ||
            layout.text(1) != names[count] || layout.text(2) != type ||
            layout.integer(3) != (count == 0 ? 1u : 0u) ||
            layout.integer(4) != 1 || layout.integer(5) != 0)
            fail(Code::corrupt, "Invalid voice sidecar table schema");
        ++count;
    }
    if (count != size)
        fail(Code::corrupt, "Invalid voice sidecar table schema");
    Query objects(db,
                  "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE "
                  "'sqlite_%' AND (type!='table' OR name!='voice_presets')");
    if (!objects.next() || objects.integer(0) != 0)
        fail(Code::unsupportedSchema,
             "Unexpected voice sidecar schema objects");
}
} // namespace

struct VoiceStore::Impl {
    sqlite3 *db{};
    VoiceStoreAccess access;
    std::uint64_t schema{2};
    struct Draft {
        VoiceSnapshot snapshot;
        VoiceSettings saved;
        std::array<bool, 8> gestures{};
    };
    std::map<std::string, Draft, std::less<>> drafts;
    std::uint64_t changeRevision{};
    const std::shared_ptr<const int> identity = std::make_shared<const int>(0);
    ~Impl() {
        if (db)
            sqlite3_close(db);
    }
    Draft &find(std::string_view id) {
        const auto found = drafts.find(id);
        if (found == drafts.end())
            fail(Code::notFound, "Unknown voice definition");
        return found->second;
    }
    Durable read(std::string_view id) {
        const auto sql = std::string(schema == 1 ? legacyRows : currentRows) +
                         " WHERE voice_id=?";
        Query query(db, sql.c_str());
        query.text(1, id);
        if (query.next())
            return readRow(query, schema);
        Durable unsaved;
        if (id.starts_with("species:")) {
            const auto candidates = speciesPresets();
            const auto found =
                std::find_if(candidates.begin(), candidates.end(),
                             [&](const SpeciesPreset &preset) {
                                 return preset.speciesId == id.substr(8);
                             });
            if (found != candidates.end())
                unsaved.settings = found->settings;
        }
        return unsaved;
    }
    void refresh(Draft &draft, const Durable &saved) {
        draft.saved = saved.settings;
        draft.snapshot.normalizedValues = normalizedValues(saved.settings);
        draft.snapshot.savedRevision = saved.revision;
        draft.snapshot.settings = saved.settings;
        draft.snapshot.dirty = false;
    }
    void changed(Draft &draft) {
        if (changeRevision == maxRevision)
            fail(Code::full, "Voice change revision exhausted");
        draft.snapshot.changeRevision = ++changeRevision;
    }
};

struct VoiceStore::DraftUpdate::State {
    std::shared_ptr<const int> owner;
    std::uint64_t baseRevision{}, revision{};
    std::map<std::string, VoiceStore::Impl::Draft, std::less<>> drafts;
};
VoiceStore::DraftUpdate::DraftUpdate(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
VoiceStore::DraftUpdate::~DraftUpdate() = default;
VoiceStore::DraftUpdate::DraftUpdate(DraftUpdate &&) noexcept = default;
VoiceStore::DraftUpdate &
VoiceStore::DraftUpdate::operator=(DraftUpdate &&) noexcept = default;
VoiceStore::DraftUpdate
VoiceStore::stageProfiles(std::span<const VoiceProfile> profiles,
                          std::span<const VoiceSpecies> names) const {
    if (profiles.empty() || profiles.size() > voiceDefinitionCapacity)
        fail(Code::invalidArgument, "Invalid voice profile count");
    if (names.size() >= voiceDefinitionCapacity)
        fail(Code::invalidArgument, "Too many voice profile display names");
    std::set<std::string> nameIds;
    for (const auto &name : names)
        if (!validId(name.speciesId) || !validId(name.name, 512) ||
            !nameIds.insert(name.speciesId).second)
            fail(Code::invalidArgument,
                 "Invalid or duplicate voice display name");
    for (const auto &[id, draft] : impl_->drafts) {
        (void)id;
        if (draft.snapshot.gestureActive)
            fail(Code::gestureActive,
                 "Close voice gestures before restoring sound");
    }
    if (impl_->changeRevision == maxRevision)
        fail(Code::full, "Voice change revision exhausted");
    auto next = std::make_unique<DraftUpdate::State>();
    next->owner = impl_->identity;
    next->baseRevision = impl_->changeRevision;
    next->revision = impl_->changeRevision + 1;
    next->drafts = impl_->drafts;
    std::set<std::string> seen;
    bool firstProfile = true;
    for (const auto &profile : profiles) {
        if (profile.speciesId.empty() && !firstProfile)
            fail(Code::invalidArgument,
                 "Fallback profile must be first when present");
        firstProfile = false;
        if ((!profile.speciesId.empty() && !validId(profile.speciesId)) ||
            !validVoiceSettings(profile.settings) ||
            !seen.insert(profile.speciesId).second)
            fail(Code::invalidArgument, "Invalid voice profile");
        const auto id = profile.speciesId.empty()
                            ? std::string("default")
                            : "species:" + profile.speciesId;
        auto found = next->drafts.find(id);
        if (found == next->drafts.end()) {
            if (next->drafts.size() == voiceDefinitionCapacity)
                fail(Code::full, "Voice definition capacity reached");
            Impl::Draft draft;
            draft.snapshot.id = id;
            draft.snapshot.name = profile.speciesId;
            draft.snapshot.speciesId = profile.speciesId;
            impl_->refresh(draft, impl_->read(id));
            found = next->drafts.emplace(id, std::move(draft)).first;
        }
        auto &draft = found->second;
        for (const auto &name : names)
            if (name.speciesId == profile.speciesId) {
                draft.snapshot.name = name.name;
                break;
            }
        draft.snapshot.settings = profile.settings;
        draft.snapshot.normalizedValues = normalizedValues(profile.settings);
        draft.snapshot.changeRevision = next->revision;
        draft.snapshot.dirty = !sameSettings(profile.settings, draft.saved);
    }
    return DraftUpdate(std::move(next));
}
bool VoiceStore::commitProfiles(DraftUpdate &&update) noexcept {
    if (!update.state_ || update.state_->owner != impl_->identity ||
        update.state_->baseRevision != impl_->changeRevision)
        return false;
    impl_->drafts.swap(update.state_->drafts);
    impl_->changeRevision = update.state_->revision;
    return true;
}

VoiceStore::VoiceStore(const std::filesystem::path &path,
                       std::span<const VoiceSpecies> species,
                       VoiceStoreAccess access)
    : impl_(std::make_unique<Impl>()) {
    impl_->access = access;
    if (path.empty() || path == ":memory:" ||
        species.size() >= voiceDefinitionCapacity)
        fail(Code::invalidArgument,
             "A sidecar path and at most 512 species are required");
    auto add = [&](std::string id, std::string name,
                   std::optional<std::string> speciesId) {
        if (impl_->drafts.size() >= voiceDefinitionCapacity)
            fail(Code::full, "Voice definition capacity reached");
        Impl::Draft draft;
        draft.snapshot.id = id;
        draft.snapshot.name = std::move(name);
        draft.snapshot.speciesId = std::move(speciesId);
        impl_->refresh(draft, {});
        if (!impl_->drafts.emplace(std::move(id), std::move(draft)).second)
            fail(Code::invalidArgument, "Duplicate species identifier");
    };
    add("default", "Fallback voice", std::nullopt);
    const auto utf8 = path.u8string();
    const std::string filename(reinterpret_cast<const char *>(utf8.data()),
                               utf8.size());
    if (filename.find('\0') != std::string::npos)
        fail(Code::invalidArgument, "Invalid sidecar path");
    const int flags = access == VoiceStoreAccess::readOnly
                          ? SQLITE_OPEN_READONLY
                          : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    const int opened = sqlite3_open_v2(filename.c_str(), &impl_->db,
                                       flags | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (opened != SQLITE_OK)
        sqlFail(opened);
    sqlite3_extended_result_codes(impl_->db, 1);
    sqlite3_busy_timeout(impl_->db, 100);
    const auto metadata = [&] {
        Query query(impl_->db, "SELECT application_id,user_version FROM "
                               "pragma_application_id,pragma_user_version");
        if (!query.next())
            fail(Code::corrupt, "Missing sidecar metadata");
        return std::pair{query.integer(0), query.integer(1)};
    };
    auto [appId, schema] = metadata();
    if (appId == 0 && schema == 0) {
        if (access == VoiceStoreAccess::readOnly)
            fail(Code::readOnly, "Cannot initialize a read-only voice sidecar");
        // Another host may initialize this empty sidecar between discovery and
        // obtaining the write lock. Re-read only after our earlier readers
        // close.
        Transaction transaction(impl_->db);
        const auto current = metadata();
        appId = current.first;
        schema = current.second;
        if (appId == 0 && schema == 0) {
            {
                Query tables(impl_->db, "SELECT count(*) FROM sqlite_schema "
                                        "WHERE name NOT LIKE 'sqlite_%'");
                if (!tables.next() || tables.integer(0) != 0)
                    fail(Code::unsupportedSchema,
                         "Refusing a nonempty unrecognized database");
            }
            run(impl_->db, createCurrent);
            run(impl_->db, "PRAGMA application_id=1096046163");
            run(impl_->db, "PRAGMA user_version=2");
            appId = applicationId;
            schema = 2;
        }
        if (appId != applicationId || (schema != 1 && schema != 2))
            fail(Code::unsupportedSchema,
                 "Unsupported voice sidecar application or schema");
        transaction.commit();
    } else if (appId != applicationId || (schema != 1 && schema != 2)) {
        fail(Code::unsupportedSchema,
             "Unsupported voice sidecar application or schema");
    }
    validateLayout(impl_->db, schema);
    if (schema == 1 && access == VoiceStoreAccess::readWrite) {
        Transaction transaction(impl_->db);
        const auto current = metadata();
        if (current.first != applicationId ||
            (current.second != 1 && current.second != 2))
            fail(Code::unsupportedSchema,
                 "Voice schema changed during migration");
        schema = current.second;
        validateLayout(impl_->db, schema);
        if (schema == 1) {
            std::map<std::string, Durable> old;
            {
                Query rows(impl_->db, legacyRows);
                while (rows.next()) {
                    const auto id = rows.text(0);
                    if (!validVoiceId(id) ||
                        !old.emplace(id, readRow(rows, 1)).second)
                        fail(Code::corrupt, "Invalid legacy voice");
                    const auto speciesCount =
                        old.size() - (old.contains("default") ? 1u : 0u);
                    if (speciesCount >= voiceDefinitionCapacity)
                        fail(Code::full, "Legacy voice capacity exceeded");
                }
            }
            run(impl_->db,
                "ALTER TABLE voice_presets RENAME TO legacy_voice_presets");
            run(impl_->db, createCurrent);
            for (const auto &[id, voice] : old)
                writeRow(impl_->db, id, voice);
            run(impl_->db, "DROP TABLE legacy_voice_presets");
            run(impl_->db, "PRAGMA user_version=2");
            schema = 2;
        }
        transaction.commit();
    }
    impl_->schema = schema;
    // Validate every durable row, even species absent from the current library
    // selection.
    Query rows(impl_->db, schema == 1 ? legacyRows : currentRows);
    std::set<std::string> seen;
    while (rows.next()) {
        const auto id = rows.text(0);
        if (!validVoiceId(id) || !seen.insert(id).second)
            fail(Code::corrupt, "Invalid or duplicate saved voice identifier");
        if (seen.size() > voiceDefinitionCapacity)
            fail(Code::full, "Voice sidecar exceeds 513 definitions");
        const auto durable = readRow(rows, schema);
        if (!impl_->drafts.contains(id))
            add(id, id.substr(8), id.substr(8));
        impl_->refresh(impl_->drafts.at(id), durable);
    }
    run(impl_->db, "PRAGMA synchronous=FULL");
    registerSpecies(species);
}
VoiceStore::~VoiceStore() = default;
void VoiceStore::registerSpecies(std::span<const VoiceSpecies> species) {
    if (species.size() >= voiceDefinitionCapacity)
        fail(Code::full, "At most 512 species definitions are supported");
    auto staged = impl_->drafts;
    std::set<std::string> seen;
    bool changed{};
    for (const auto &item : species) {
        if (!validId(item.speciesId) || !validId(item.name, 512) ||
            !seen.insert(item.speciesId).second)
            fail(Code::invalidArgument,
                 "Invalid or duplicate species identifier/name");
        const auto id = "species:" + item.speciesId;
        if (auto found = staged.find(id); found != staged.end()) {
            if (found->second.snapshot.name != item.name) {
                found->second.snapshot.name = item.name;
                changed = true;
            }
        } else {
            if (staged.size() >= voiceDefinitionCapacity)
                fail(Code::full, "Voice definition capacity reached");
            Impl::Draft draft;
            draft.snapshot.id = id;
            draft.snapshot.name = item.name;
            draft.snapshot.speciesId = item.speciesId;
            impl_->refresh(draft, impl_->read(id));
            staged.emplace(id, std::move(draft));
            changed = true;
        }
    }
    if (!changed)
        return;
    if (impl_->changeRevision == maxRevision)
        fail(Code::full, "Voice change revision exhausted");
    const auto revision = impl_->changeRevision + 1;
    for (auto &[id, draft] : staged) {
        const auto previous = impl_->drafts.find(id);
        if (previous == impl_->drafts.end() ||
            previous->second.snapshot.name != draft.snapshot.name)
            draft.snapshot.changeRevision = revision;
    }
    impl_->drafts.swap(staged);
    impl_->changeRevision = revision;
}
std::vector<VoiceSnapshot> VoiceStore::voices() const {
    std::vector<VoiceSnapshot> result;
    result.reserve(impl_->drafts.size());
    for (const auto &[id, draft] : impl_->drafts) {
        (void)id;
        result.push_back(draft.snapshot);
    }
    return result;
}
VoiceSnapshot VoiceStore::getVoice(std::string_view id) const {
    return impl_->find(id).snapshot;
}
VoiceSnapshot VoiceStore::previewParameter(std::string_view id,
                                           std::string_view parameterId,
                                           double value,
                                           VoiceGesture gesture) const {
    auto draft = impl_->find(id);
    const auto parameter = std::find(voiceParameterIds.begin(),
                                     voiceParameterIds.end(), parameterId);
    if (parameter == voiceParameterIds.end() || !std::isfinite(value) ||
        value < 0 || value > 1)
        fail(Code::invalidArgument,
             "Unknown parameter or invalid normalized value");
    const auto index =
        static_cast<std::size_t>(parameter - voiceParameterIds.begin());
    if (gesture != VoiceGesture::begin && gesture != VoiceGesture::change &&
        gesture != VoiceGesture::end)
        fail(Code::invalidArgument, "Unknown voice gesture");
    if ((gesture == VoiceGesture::begin) == draft.gestures[index])
        fail(Code::invalidArgument,
             "Voice gesture must follow begin, change, end");
    if (impl_->changeRevision == maxRevision)
        fail(Code::full, "Voice change revision exhausted");
    if (gesture == VoiceGesture::begin)
        draft.gestures[index] = true;
    else {
        const auto &spec = voiceParameterSpecs[index];
        if (spec.step > 0) {
            const auto physical = denormalize(spec, value);
            value = normalize(
                spec, spec.min + std::round((physical - spec.min) / spec.step) *
                                     spec.step);
        }
        draft.snapshot.normalizedValues[index] = value;
        auto physical = physicalValues(draft.snapshot.settings);
        physical[index] = denormalize(voiceParameterSpecs[index], value);
        draft.snapshot.settings =
            physicalSettings(physical, draft.snapshot.settings.character);
        draft.snapshot.dirty =
            !sameSettings(draft.snapshot.settings, draft.saved);
        if (gesture == VoiceGesture::end)
            draft.gestures[index] = false;
    }
    draft.snapshot.gestureActive =
        std::any_of(draft.gestures.begin(), draft.gestures.end(),
                    [](bool active) { return active; });
    draft.snapshot.changeRevision = impl_->changeRevision + 1;
    return draft.snapshot;
}
VoiceSnapshot VoiceStore::setParameter(std::string_view id,
                                       std::string_view parameterId,
                                       double value, VoiceGesture gesture) {
    auto staged = previewParameter(id, parameterId, value, gesture);
    auto &draft = impl_->find(id);
    const auto parameter = std::find(voiceParameterIds.begin(),
                                     voiceParameterIds.end(), parameterId);
    draft.gestures[static_cast<std::size_t>(
        parameter - voiceParameterIds.begin())] = gesture != VoiceGesture::end;
    draft.snapshot = std::move(staged);
    ++impl_->changeRevision;
    return draft.snapshot;
}
VoiceSnapshot VoiceStore::save(std::string_view id, std::uint64_t expected) {
    auto &draft = impl_->find(id);
    if (draft.snapshot.gestureActive)
        fail(Code::gestureActive, "Close voice gestures before saving");
    if (impl_->access == VoiceStoreAccess::readOnly)
        fail(Code::readOnly, "Voice sidecar is read-only");
    if (expected > maxRevision || expected != draft.snapshot.savedRevision)
        fail(Code::conflict, "Saved voice revision changed; revert to reload");
    if (impl_->changeRevision == maxRevision || expected == maxRevision)
        fail(Code::full, "Voice revision exhausted");
    Transaction transaction(impl_->db);
    const auto durable = impl_->read(id);
    if (durable.revision != expected)
        fail(Code::conflict, "Voice was saved elsewhere; revert to reload");
    if (!durable.revision && id != "default") {
        Query count(
            impl_->db,
            "SELECT count(*) FROM voice_presets WHERE voice_id!='default'");
        if (!count.next() || count.integer(0) >= voiceDefinitionCapacity - 1)
            fail(Code::full, "Voice sidecar species capacity reached");
    }
    writeRow(impl_->db, id, Durable{draft.snapshot.settings, expected + 1});
    transaction.commit();
    draft.saved = draft.snapshot.settings;
    draft.snapshot.savedRevision = expected + 1;
    draft.snapshot.dirty = false;
    impl_->changed(draft);
    return draft.snapshot;
}
VoiceSnapshot VoiceStore::revert(std::string_view id) {
    auto &draft = impl_->find(id);
    if (draft.snapshot.gestureActive)
        fail(Code::gestureActive, "Close voice gestures before reverting");
    if (impl_->changeRevision == maxRevision)
        fail(Code::full, "Voice change revision exhausted");
    const auto durable = impl_->read(id);
    if (draft.snapshot.savedRevision && !durable.revision)
        fail(Code::corrupt, "Saved voice row disappeared");
    impl_->refresh(draft, durable);
    impl_->changed(draft);
    return draft.snapshot;
}
} // namespace atlas
