#include <algorithm>
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
VoiceSettings settings(const VoiceValues &values) {
    return {static_cast<int>(
                std::lround(denormalize(voiceParameterSpecs[0], values[0]))),
            denormalize(voiceParameterSpecs[1], values[1]),
            denormalize(voiceParameterSpecs[2], values[2]),
            denormalize(voiceParameterSpecs[3], values[3])};
}
struct Durable {
    VoiceValues values{defaults()};
    std::uint64_t revision{};
};
constexpr const char *readRows =
    "SELECT voice_id,schema_version,saved_revision,tuning,attack,decay,gain "
    "FROM voice_presets";
Durable readRow(Query &query) {
    if (query.integer(1) != 1)
        fail(Code::unsupportedSchema, "Unsupported saved voice schema");
    Durable result;
    result.revision = query.integer(2);
    if (!result.revision)
        fail(Code::corrupt, "Saved voice revision must be positive");
    for (int i = 0; i < 4; ++i)
        result.values[static_cast<std::size_t>(i)] = query.number(i + 3);
    if (!validValues(result.values))
        fail(Code::corrupt, "Saved voice values are invalid");
    return result;
}
} // namespace

struct VoiceStore::Impl {
    sqlite3 *db{};
    VoiceStoreAccess access;
    struct Draft {
        VoiceSnapshot snapshot;
        VoiceValues saved;
        std::array<bool, 4> gestures{};
    };
    std::map<std::string, Draft, std::less<>> drafts;
    std::uint64_t changeRevision{};
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
        Query query(db, "SELECT "
                        "voice_id,schema_version,saved_revision,tuning,attack,"
                        "decay,gain FROM voice_presets WHERE voice_id=?");
        query.text(1, id);
        return query.next() ? readRow(query) : Durable{};
    }
    void refresh(Draft &draft, const Durable &saved) {
        draft.saved = saved.values;
        draft.snapshot.normalizedValues = saved.values;
        draft.snapshot.savedRevision = saved.revision;
        draft.snapshot.settings = settings(saved.values);
        draft.snapshot.dirty = false;
    }
    void changed(Draft &draft) {
        if (changeRevision == maxRevision)
            fail(Code::full, "Voice change revision exhausted");
        draft.snapshot.changeRevision = ++changeRevision;
    }
};

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
            run(impl_->db,
                "CREATE TABLE voice_presets(voice_id TEXT PRIMARY KEY NOT "
                "NULL,schema_version INTEGER NOT NULL,saved_revision INTEGER "
                "NOT NULL,tuning REAL NOT NULL,attack REAL NOT NULL,decay REAL "
                "NOT NULL,gain REAL NOT NULL) WITHOUT ROWID");
            run(impl_->db, "PRAGMA application_id=1096046163");
            run(impl_->db, "PRAGMA user_version=1");
            appId = applicationId;
            schema = 1;
        }
        if (appId != applicationId || schema != 1)
            fail(Code::unsupportedSchema,
                 "Unsupported voice sidecar application or schema");
        transaction.commit();
    } else if (appId != applicationId || schema != 1) {
        fail(Code::unsupportedSchema,
             "Unsupported voice sidecar application or schema");
    }
    Query layout(impl_->db, R"SQL(
        SELECT count(*)=7 AND sum(CASE
            WHEN cid=0 AND name='voice_id' AND type='TEXT' AND pk=1 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=1 AND name='schema_version' AND type='INTEGER' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=2 AND name='saved_revision' AND type='INTEGER' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=3 AND name='tuning' AND type='REAL' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=4 AND name='attack' AND type='REAL' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=5 AND name='decay' AND type='REAL' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            WHEN cid=6 AND name='gain' AND type='REAL' AND pk=0 AND "notnull"=1 AND hidden=0 THEN 1
            ELSE 0 END)=7 FROM pragma_table_xinfo('voice_presets')
    )SQL");
    if (!layout.next() || layout.integer(0) != 1)
        fail(Code::corrupt, "Invalid voice sidecar table schema");
    Query objects(impl_->db,
                  "SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE "
                  "'sqlite_%' AND (type!='table' OR name!='voice_presets')");
    if (!objects.next() || objects.integer(0) != 0)
        fail(Code::unsupportedSchema,
             "Unexpected voice sidecar schema objects");
    // Validate every durable row, even species absent from the current library
    // selection.
    Query rows(impl_->db, readRows);
    std::set<std::string> seen;
    while (rows.next()) {
        const auto id = rows.text(0);
        if (!validVoiceId(id) || !seen.insert(id).second)
            fail(Code::corrupt, "Invalid or duplicate saved voice identifier");
        if (seen.size() > voiceDefinitionCapacity)
            fail(Code::full, "Voice sidecar exceeds 513 definitions");
        const auto durable = readRow(rows);
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
    for (const auto &item : species) {
        if (!validId(item.speciesId) || !validId(item.name, 512) ||
            !seen.insert(item.speciesId).second)
            fail(Code::invalidArgument,
                 "Invalid or duplicate species identifier/name");
        const auto id = "species:" + item.speciesId;
        if (auto found = staged.find(id); found != staged.end())
            found->second.snapshot.name = item.name;
        else {
            if (staged.size() >= voiceDefinitionCapacity)
                fail(Code::full, "Voice definition capacity reached");
            Impl::Draft draft;
            draft.snapshot.id = id;
            draft.snapshot.name = item.name;
            draft.snapshot.speciesId = item.speciesId;
            impl_->refresh(draft, impl_->read(id));
            staged.emplace(id, std::move(draft));
        }
    }
    impl_->drafts.swap(staged);
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
        draft.snapshot.settings = settings(draft.snapshot.normalizedValues);
        draft.snapshot.dirty = draft.snapshot.normalizedValues != draft.saved;
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
    Query write(
        impl_->db,
        "INSERT INTO "
        "voice_presets(voice_id,schema_version,saved_revision,tuning,attack,"
        "decay,gain) VALUES(?,1,?,?,?,?,?) ON CONFLICT(voice_id) DO UPDATE SET "
        "saved_revision=excluded.saved_revision,tuning=excluded.tuning,attack="
        "excluded.attack,decay=excluded.decay,gain=excluded.gain");
    write.text(1, id);
    write.integer(2, expected + 1);
    for (int i = 0; i < 4; ++i)
        write.number(
            i + 3,
            draft.snapshot.normalizedValues[static_cast<std::size_t>(i)]);
    (void)write.next();
    transaction.commit();
    draft.saved = draft.snapshot.normalizedValues;
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
