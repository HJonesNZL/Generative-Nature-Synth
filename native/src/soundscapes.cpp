#include "event_validation.hpp"
#include "sha256.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <array>
#include <atlas/soundscapes.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sqlite3.h>
#include <sstream>

namespace atlas {
SoundscapeError::SoundscapeError(SoundscapeErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
Soundscape::Soundscape(SoundscapeCapture capture, Timeline timeline,
                       std::string hash)
    : capture_(std::move(capture)), timeline_(std::move(timeline)),
      contentHash_(std::move(hash)) {}
namespace {
using Code = SoundscapeErrorCode;
using Metadata = std::map<std::string, std::string>;
constexpr int applicationId = 0x41545343;
constexpr std::size_t maximumBytes = 64 * 1024 * 1024;
constexpr auto metaSql = "CREATE TABLE scene_meta(key TEXT PRIMARY KEY NOT "
                         "NULL,value TEXT NOT NULL) WITHOUT ROWID";
constexpr auto eventsSql = "CREATE TABLE scene_events(ordinal INTEGER PRIMARY "
                           "KEY NOT NULL,original_seq INTEGER NOT "
                           "NULL,event_json TEXT NOT NULL) WITHOUT ROWID";
constexpr auto profilesSql =
    "CREATE TABLE scene_profiles(slot INTEGER PRIMARY KEY NOT NULL,species_id "
    "TEXT NOT NULL,tuning INTEGER NOT NULL,attack REAL NOT NULL,decay REAL NOT "
    "NULL,gain REAL NOT NULL,character INTEGER NOT NULL,brightness REAL NOT "
    "NULL,roughness REAL NOT NULL,pulse_hz REAL NOT NULL,sweep_semitones REAL "
    "NOT NULL) WITHOUT ROWID";
[[noreturn]] void fail(Code code, std::string message) {
    throw SoundscapeError(code, std::move(message));
}
void run(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        fail(Code::storage, "Soundscape SQLite operation failed");
}
std::string pathText(const std::filesystem::path &path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
class Database {
  public:
    explicit Database(const std::filesystem::path &path, bool writing = false) {
        const int flags = writing ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                  : SQLITE_OPEN_READONLY;
        if (sqlite3_open_v2(pathText(path).c_str(), &db_,
                            flags | SQLITE_OPEN_NOMUTEX,
                            nullptr) != SQLITE_OK) {
            if (db_)
                sqlite3_close(db_);
            db_ = nullptr;
            fail(Code::storage, "Unable to open soundscape file");
        }
        sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, 32768);
        sqlite3_limit(db_, SQLITE_LIMIT_SQL_LENGTH, 32768);
        sqlite3_limit(db_, SQLITE_LIMIT_COLUMN, 128);
        sqlite3_busy_timeout(db_, 1000);
        sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    }
    ~Database() {
        if (db_)
            sqlite3_close(db_);
    }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    sqlite3 *get() const noexcept { return db_; }
    void close() {
        if (sqlite3_close(db_) != SQLITE_OK)
            fail(Code::storage, "Soundscape close failed");
        db_ = nullptr;
    }

  private:
    sqlite3 *db_{};
};
class Insert {
  public:
    Insert(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &query_, nullptr) != SQLITE_OK)
            fail(Code::storage, "Soundscape insert preparation failed");
    }
    ~Insert() { sqlite3_finalize(query_); }
    void text(int index, std::string_view value) {
        if (sqlite3_bind_text(query_, index, value.data(),
                              static_cast<int>(value.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK)
            fail(Code::storage, "Soundscape text binding failed");
    }
    void integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(query_, index, value) != SQLITE_OK)
            fail(Code::storage, "Soundscape integer binding failed");
    }
    void number(int index, double value) {
        if (sqlite3_bind_double(query_, index, value == 0 ? 0 : value) !=
            SQLITE_OK)
            fail(Code::storage, "Soundscape numeric binding failed");
    }
    void execute() {
        if (sqlite3_step(query_) != SQLITE_DONE)
            fail(Code::storage, "Soundscape insert failed");
        sqlite3_reset(query_);
        sqlite3_clear_bindings(query_);
    }

  private:
    sqlite3_stmt *query_{};
};
bool hashText(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
void text(std::string_view value, std::size_t limit, bool empty = false) {
    if ((!empty && value.empty()) || value.size() > limit ||
        value.find('\0') != std::string_view::npos)
        fail(Code::invalidArgument, "Invalid soundscape metadata text");
}
std::string numberText(double value) {
    if (!std::isfinite(value))
        fail(Code::corrupt, "Nonfinite soundscape number");
    if (value == 0)
        return "0";
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{})
        fail(Code::corrupt, "Invalid soundscape number");
    return {buffer.data(), result.ptr};
}
std::int64_t integer(const Metadata &meta, const std::string &key) {
    const auto &value = meta.at(key);
    std::int64_t result{};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        value != std::to_string(result))
        fail(Code::corrupt, "Invalid canonical soundscape integer");
    return result;
}
double number(const Metadata &meta, const std::string &key) {
    const auto &value = meta.at(key);
    double result{};
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> result;
    if (!input || input.peek() != std::char_traits<char>::eof() ||
        numberText(result) != value)
        fail(Code::corrupt, "Invalid canonical soundscape number");
    return result;
}
void addBounds(
    Metadata &meta, std::string_view prefix,
    const std::optional<std::pair<Microseconds, Microseconds>> &bounds) {
    const auto key = std::string(prefix);
    meta[key + "_present"] = bounds ? "1" : "0";
    meta[key + "_start_us"] = std::to_string(bounds ? bounds->first : 0);
    meta[key + "_end_us"] = std::to_string(bounds ? bounds->second : 0);
}
std::optional<std::pair<Microseconds, Microseconds>>
readBounds(const Metadata &meta, std::string_view prefix) {
    const auto key = std::string(prefix);
    const auto present = integer(meta, key + "_present"),
               start = integer(meta, key + "_start_us"),
               end = integer(meta, key + "_end_us");
    if (present == 1 && start <= end && start >= -400000000000000000LL &&
        end <= 400000000000000000LL)
        return std::pair{start, end};
    if (present != 0 || start != 0 || end != 0)
        fail(Code::corrupt, "Invalid soundscape bounds");
    return {};
}
Metadata metadata(const SoundscapeCapture &capture, Microseconds duration) {
    const auto &session = capture.session;
    Metadata result{
        {"name", capture.name},
        {"source_library_label", capture.sourceLibraryLabel},
        {"source_session_id", session.id},
        {"source_session_name", session.name},
        {"source_session_state", session.state},
        {"source_session_origin", session.origin},
        {"source_content_hash", session.contentHash},
        {"source_metadata_integrity", session.metadataIntegrity},
        {"source_window_policy", session.observedWindowPolicy},
        {"source_truncated", session.truncated ? "1" : "0"},
        {"event_count", std::to_string(session.eventCount)},
        {"time_basis",
         capture.basis == TimeBasis::observed ? "observed" : "arrival"},
        {"speed_numerator", std::to_string(capture.speed.numerator)},
        {"speed_denominator", std::to_string(capture.speed.denominator)},
        {"duration_us", std::to_string(duration)},
        {"sample_rate", std::to_string(capture.audio.sampleRate)},
        {"root_midi", std::to_string(capture.audio.rootMidi)},
        {"attack_seconds", numberText(capture.audio.attackSeconds)},
        {"decay_seconds", numberText(capture.audio.decaySeconds)},
        {"master_db", numberText(capture.audio.masterDb)},
        {"synthesis_engine", "atlas-additive"},
        {"synthesis_revision", std::to_string(capture.synthesisRevision)},
        {"mapping_revision", std::to_string(capture.mappingRevision)},
        {"profile_count", std::to_string(capture.profiles.size())}};
    if (capture.schemaRevision == 2) {
        const auto &composition = capture.audio.composition;
        result["composition_enabled"] = composition.enabled ? "1" : "0";
        result["composition_scale"] =
            std::to_string(static_cast<unsigned>(composition.scale));
        result["composition_tonic_pitch_class"] =
            std::to_string(composition.tonicPitchClass);
        result["composition_tempo_bpm"] = std::to_string(composition.tempoBpm);
        result["composition_density"] =
            std::to_string(static_cast<unsigned>(composition.density));
        result["composition_variation"] = numberText(composition.variation);
        result["composition_seed"] = std::to_string(composition.seed);
        result["composition_quantize"] = composition.quantize ? "1" : "0";
    }
    addBounds(result, "observed", session.observedBounds);
    addBounds(result, "arrival", session.arrivalBounds);
    addBounds(result, "selected", capture.bounds);
    return result;
}
void hashValue(detail::Sha256 &hash, std::string_view value) {
    hash.update(std::to_string(value.size()));
    hash.update(":");
    hash.update(value);
}
void hashMetadata(detail::Sha256 &hash, const Metadata &meta, unsigned revision) {
    hashValue(hash, revision == 1 ? "atlas-scene-v1" : "atlas-scene-v2");
    for (const auto &[key, value] : meta) {
        hashValue(hash, key);
        hashValue(hash, value);
    }
}
std::string headerHash(const Metadata &meta, unsigned revision) {
    detail::Sha256 hash;
    hashMetadata(hash, meta, revision);
    return hash.hexDigest();
}
std::string contentHash(const Metadata &meta, const SoundscapeCapture &capture,
                        const Timeline &timeline) {
    detail::Sha256 hash;
    hashMetadata(hash, meta, capture.schemaRevision);
    hashValue(hash, "profiles");
    for (const auto &profile : capture.profiles) {
        hashValue(hash, profile.speciesId);
        hashValue(hash, std::to_string(profile.settings.tuningSemitones));
        hashValue(hash, numberText(profile.settings.attackSeconds));
        hashValue(hash, numberText(profile.settings.decaySeconds));
        hashValue(hash, numberText(profile.settings.gainDb));
        hashValue(hash, std::to_string(
                            static_cast<unsigned>(profile.settings.character)));
        hashValue(hash, numberText(profile.settings.brightness));
        hashValue(hash, numberText(profile.settings.roughness));
        hashValue(hash, numberText(profile.settings.pulseHz));
        hashValue(hash, numberText(profile.settings.sweepSemitones));
    }
    hashValue(hash, "events");
    for (const auto &event : timeline.events()) {
        hashValue(hash, std::to_string(event.revisionSequence));
        hashValue(hash, event.normalizedJson);
    }
    return hash.hexDigest();
}
Event parseEvent(sqlite3 *db, std::string raw, std::int64_t sequence) {
    detail::validateStoredEvent(db, raw);
    detail::Statement query(
        db,
        R"SQL(SELECT json_extract(?1,'$.event_id'),json_extract(?1,'$.source_id'),
        json_extract(?1,'$.event_kind'),json_extract(?1,'$.location_kind'),json_extract(?1,'$.species_id'),
        json_extract(?1,'$.individual_id'),json_extract(?1,'$.deployment_id'),json_extract(?1,'$.station_id'),
        json_extract(?1,'$.latitude'),json_extract(?1,'$.longitude'),json_extract(?1,'$.observed_at_utc'),
        json_extract(?1,'$.first_seen_at_utc'),json_extract(?1,'$.ingested_at_utc'))SQL");
    query.bindJson(raw);
    if (!query.next())
        fail(Code::corrupt, "Soundscape event missing");
    Event event;
    event.id = query.text(0);
    event.sourceId = query.text(1);
    event.kind = query.text(2);
    event.locationKind = query.text(3);
    event.speciesId = query.optionalText(4);
    event.individualId = query.optionalText(5);
    event.deploymentId = query.optionalText(6);
    event.stationId = query.optionalText(7);
    if (!query.null(8))
        event.position = Position{query.number(8), query.number(9)};
    event.observedAt = parseUtc(query.text(10));
    event.firstSeenAt = parseUtc(query.text(11));
    event.ingestedAt = parseUtc(query.text(12));
    event.revisionSequence = sequence;
    event.normalizedJson = std::move(raw);
    event.validate();
    return event;
}
bool sameEvent(const Event &a, const Event &b) {
    return a.id == b.id && a.sourceId == b.sourceId && a.kind == b.kind &&
           a.locationKind == b.locationKind && a.speciesId == b.speciesId &&
           a.individualId == b.individualId &&
           a.deploymentId == b.deploymentId && a.stationId == b.stationId &&
           a.observedAt == b.observedAt && a.firstSeenAt == b.firstSeenAt &&
           a.ingestedAt == b.ingestedAt &&
           a.position.has_value() == b.position.has_value() &&
           (!a.position || (a.position->latitude == b.position->latitude &&
                            a.position->longitude == b.position->longitude));
}
void validateCapture(const SoundscapeCapture &capture, std::size_t eventCount) {
    text(capture.name, 512);
    text(capture.sourceLibraryLabel, 1024, true);
    const auto &session = capture.session;
    text(session.id, 128);
    text(session.name, 512);
    text(session.origin, 256);
    text(session.observedWindowPolicy, 256);
    if ((session.state != "frozen" && session.state != "interrupted") ||
        !hashText(session.contentHash) ||
        (session.metadataIntegrity != "sha256-envelope-v1-verified" &&
         session.metadataIntegrity != "legacy-metadata-unverified"))
        fail(Code::invalidArgument,
             "Capture requires verified immutable source material");
    if (capture.basis != TimeBasis::observed &&
        capture.basis != TimeBasis::arrival)
        fail(Code::invalidArgument, "Invalid soundscape time basis");
    for (const auto &bounds : {session.observedBounds, session.arrivalBounds})
        if (bounds && (bounds->first > bounds->second ||
                       bounds->first < -400000000000000000LL ||
                       bounds->second > 400000000000000000LL))
            fail(Code::invalidArgument,
                 "Invalid original soundscape timeline bounds");
    if (capture.bounds != (capture.basis == TimeBasis::observed
                               ? session.observedBounds
                               : session.arrivalBounds))
        fail(Code::invalidArgument,
             "Selected bounds must preserve original source timeline bounds");
    if (!validCompositionSettings(capture.audio.composition))
        fail(Code::invalidArgument, "Invalid soundscape composition settings");
    const auto &composition = capture.audio.composition;
    const bool legacyComposition =
        !composition.enabled &&
        composition.scale == CompositionScale::majorPentatonic &&
        composition.tonicPitchClass == 0 && composition.tempoBpm == 90 &&
        composition.density == CompositionDensity::balanced &&
        composition.variation == .25 && composition.seed == 1 &&
        composition.quantize;
    if (!((capture.schemaRevision == 1 && capture.synthesisRevision == 2 &&
           capture.mappingRevision == 1 && legacyComposition) ||
          (capture.schemaRevision == 2 &&
           capture.synthesisRevision == soundscapeSynthesisRevision &&
           capture.mappingRevision == soundscapeMappingRevision)))
        fail(Code::unsupportedSchema,
             "Unsupported soundscape synthesis or mapping revision");
    if (session.eventCount != eventCount || eventCount > 50000)
        fail(Code::capacity,
             "Soundscape event count mismatch or capacity exceeded");
    if (capture.profiles.empty() ||
        !capture.profiles.front().speciesId.empty() ||
        capture.profiles.size() > voiceSlotCapacity)
        fail(Code::invalidArgument, "Soundscape requires explicit fallback and "
                                    "bounded complete species profiles");
    for (const auto &profile : capture.profiles)
        text(profile.speciesId, 256, true);
    (void)capture.speed.scale(0);
}
void checkCompleteProfiles(const Timeline &timeline,
                           const SoundscapeCapture &capture) {
    std::set<std::string> species;
    for (const auto &profile : capture.profiles)
        if (!species.insert(profile.speciesId).second)
            fail(Code::corrupt, "Duplicate soundscape species profile");
    for (const auto &event : timeline.events()) {
        if (event.speciesId && !species.contains(*event.speciesId))
            fail(Code::invalidArgument,
                 "Soundscape omits a species voice profile");
        if (capture.session.observedWindowPolicy == "preserve_requested" &&
            (!capture.session.observedBounds ||
             event.observedAt < capture.session.observedBounds->first ||
             event.observedAt >= capture.session.observedBounds->second))
            fail(Code::corrupt,
                 "Soundscape event lies outside original requested window");
    }
    // The engine remains the one authority for settings, voice, render bounds.
    (void)AudioPlan(timeline, capture.audio, capture.profiles);
}
unsigned checkSchema(sqlite3 *db) {
    detail::Statement identity(
        db, "SELECT (SELECT application_id FROM pragma_application_id),(SELECT "
            "user_version FROM pragma_user_version)");
    if (!identity.next() || identity.integer(0) != applicationId ||
        (identity.integer(1) != 1 && identity.integer(1) != 2))
        fail(Code::unsupportedSchema,
             "Unsupported soundscape application or schema identity");
    const std::map<std::string, std::string> expected{
        {"scene_meta", metaSql},
        {"scene_events", eventsSql},
        {"scene_profiles", profilesSql}};
    detail::Statement objects(
        db, "SELECT type,name,sql FROM sqlite_schema ORDER BY name LIMIT 5");
    std::size_t count{};
    while (objects.next()) {
        const auto found = expected.find(objects.text(1));
        if (++count > 3 || objects.text(0) != "table" ||
            found == expected.end() || objects.text(2) != found->second)
            fail(Code::corrupt,
                 "Unexpected soundscape schema object or column");
    }
    if (count != 3)
        fail(Code::corrupt, "Incomplete soundscape schema");
    return static_cast<unsigned>(identity.integer(1));
}
struct Header {
    SoundscapeCapture capture;
    Metadata metadata;
    Microseconds duration{};
    std::string hash;
    std::size_t profileCount{};
};
Header readHeader(sqlite3 *db) {
    const auto revision = checkSchema(db);
    detail::Statement query(
        db, "SELECT key,value FROM scene_meta ORDER BY key LIMIT 64");
    Metadata values;
    while (query.next()) {
        auto key = query.text(0), value = query.text(1);
        if (key.size() > 64 || value.size() > 1024 ||
            !values.emplace(std::move(key), std::move(value)).second)
            fail(Code::corrupt, "Invalid soundscape metadata row");
    }
    Header header;
    header.capture.schemaRevision = revision;
    header.hash = values.at("content_hash");
    const auto seal = values.at("header_hash");
    values.erase("header_hash");
    if (!hashText(header.hash) || seal != headerHash(values, revision))
        fail(Code::corrupt, "Soundscape metadata integrity mismatch");
    values.erase("content_hash");
    auto &capture = header.capture;
    auto &session = capture.session;
    capture.name = values.at("name");
    capture.sourceLibraryLabel = values.at("source_library_label");
    session.id = values.at("source_session_id");
    session.name = values.at("source_session_name");
    session.state = values.at("source_session_state");
    session.origin = values.at("source_session_origin");
    session.contentHash = values.at("source_content_hash");
    session.metadataIntegrity = values.at("source_metadata_integrity");
    session.observedWindowPolicy = values.at("source_window_policy");
    const auto count = integer(values, "event_count"),
               profiles = integer(values, "profile_count"),
               truncated = integer(values, "source_truncated");
    if (count < 0 || count > 50000 || profiles < 1 ||
        profiles > static_cast<std::int64_t>(voiceSlotCapacity) ||
        truncated < 0 || truncated > 1)
        fail(Code::capacity, "Invalid soundscape counts or truncation marker");
    session.eventCount = static_cast<std::size_t>(count);
    session.truncated = truncated != 0;
    header.profileCount = static_cast<std::size_t>(profiles);
    capture.profiles.resize(header.profileCount);
    session.observedBounds = readBounds(values, "observed");
    session.arrivalBounds = readBounds(values, "arrival");
    capture.bounds = readBounds(values, "selected");
    if (values.at("time_basis") != "observed" &&
        values.at("time_basis") != "arrival")
        fail(Code::corrupt, "Invalid soundscape time basis");
    capture.basis = values.at("time_basis") == "observed" ? TimeBasis::observed
                                                          : TimeBasis::arrival;
    capture.speed = {integer(values, "speed_numerator"),
                     integer(values, "speed_denominator")};
    const auto rate = integer(values, "sample_rate"),
               root = integer(values, "root_midi");
    if (rate < 0 || rate > 192000 || root < 0 || root > 127)
        fail(Code::corrupt, "Invalid soundscape audio integers");
    capture.audio = {
        static_cast<std::uint32_t>(rate), static_cast<unsigned>(root),
        number(values, "attack_seconds"), number(values, "decay_seconds"),
        number(values, "master_db")};
    const auto synthesis = integer(values, "synthesis_revision"),
               mapping = integer(values, "mapping_revision");
    if (synthesis < 0 || synthesis > std::numeric_limits<unsigned>::max() ||
        mapping < 0 || mapping > std::numeric_limits<unsigned>::max() ||
        values.at("synthesis_engine") != "atlas-additive")
        fail(Code::unsupportedSchema,
             "Unsupported soundscape synthesis or mapping revision");
    capture.synthesisRevision = static_cast<unsigned>(synthesis);
    capture.mappingRevision = static_cast<unsigned>(mapping);
    if (revision == 2) {
        const auto enabled = integer(values, "composition_enabled"),
                   scale = integer(values, "composition_scale"),
                   tonic = integer(values, "composition_tonic_pitch_class"),
                   tempo = integer(values, "composition_tempo_bpm"),
                   density = integer(values, "composition_density"),
                   seed = integer(values, "composition_seed"),
                   quantize = integer(values, "composition_quantize");
        if (enabled < 0 || enabled > 1 || scale < 0 || scale > 3 ||
            tonic < 0 || tonic > 11 || tempo < 40 || tempo > 180 ||
            density < 0 || density > 2 || seed < 0 ||
            seed > std::numeric_limits<std::uint32_t>::max() ||
            quantize < 0 || quantize > 1)
            fail(Code::corrupt, "Invalid soundscape composition integers");
        capture.audio.composition = {
            enabled != 0, static_cast<CompositionScale>(scale),
            static_cast<unsigned>(tonic), static_cast<unsigned>(tempo),
            static_cast<CompositionDensity>(density),
            number(values, "composition_variation"),
            static_cast<std::uint32_t>(seed), quantize != 0};
    }
    header.duration = integer(values, "duration_us");
    if (header.duration < 0 || header.duration > 1800000000LL)
        fail(Code::capacity, "Soundscape exceeds 30 minute render limit");
    validateCapture(capture, session.eventCount);
    // Validate scalar globals through the same engine boundary without reading
    // event/profile material. Empty plan construction stays bounded and small.
    (void)AudioPlan(Timeline({}, capture.basis, capture.speed), capture.audio);
    if (values != metadata(capture, header.duration))
        fail(Code::corrupt,
             "Unknown, missing or noncanonical soundscape metadata");
    header.metadata = std::move(values);
    return header;
}
void checkPath(const std::filesystem::path &path) {
    const auto state = std::filesystem::symlink_status(path);
    if (state.type() == std::filesystem::file_type::not_found)
        fail(Code::notFound, "Soundscape file not found");
    if (!std::filesystem::is_regular_file(state) ||
        path.extension() != ".atlas-scene")
        fail(Code::invalidArgument,
             "Soundscape must be a regular .atlas-scene file");
    if (std::filesystem::file_size(path) > maximumBytes + 4 * 1024 * 1024)
        fail(Code::capacity, "Soundscape file exceeds bounded capacity");
    for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
        auto sidecar = path;
        sidecar += suffix;
        if (std::filesystem::symlink_status(sidecar).type() !=
            std::filesystem::file_type::not_found)
            fail(Code::corrupt, "Soundscape must be a complete standalone file "
                                "without journal sidecars");
    }
}
struct Staging {
    std::filesystem::path directory;
    explicit Staging(const std::filesystem::path &output) : directory(output) {
        directory += ".saving";
        if (!std::filesystem::create_directory(directory))
            fail(
                Code::alreadyExists,
                "Soundscape staging directory exists; inspect before retrying");
    }
    ~Staging() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
} // namespace

Soundscape captureSoundscape(const Timeline &timeline,
                             SoundscapeCapture capture) {
    try {
        validateCapture(capture, timeline.events().size());
        Database scratch(":memory:", true);
        std::vector<Event> events;
        events.reserve(timeline.events().size());
        std::size_t bytes{};
        for (const auto &event : timeline.events()) {
            bytes += event.normalizedJson.size();
            if (bytes > maximumBytes)
                fail(Code::capacity,
                     "Soundscape copied event capacity exceeded");
            auto parsed = parseEvent(scratch.get(), event.normalizedJson,
                                     event.revisionSequence);
            if (!sameEvent(parsed, event))
                fail(Code::invalidArgument,
                     "Timeline event differs from its frozen source material");
            events.push_back(std::move(parsed));
        }
        Timeline copy(std::move(events), capture.basis, capture.speed,
                      capture.bounds);
        if (copy.durationUs() != timeline.durationUs() ||
            copy.schedule().size() != timeline.schedule().size())
            fail(Code::invalidArgument,
                 "Capture timeline settings do not match prepared material");
        for (std::size_t i = 0; i < copy.schedule().size(); ++i) {
            const auto &a = copy.schedule()[i], &b = timeline.schedule()[i];
            if (a.dueUs != b.dueUs ||
                a.revisionSequence != b.revisionSequence ||
                copy.events()[a.eventIndex].id !=
                    timeline.events()[b.eventIndex].id)
                fail(Code::invalidArgument, "Capture time basis or speed "
                                            "differs from prepared timeline");
        }
        checkCompleteProfiles(copy, capture);
        const auto hash =
            contentHash(metadata(capture, copy.durationUs()), capture, copy);
        return Soundscape(std::move(capture), std::move(copy), hash);
    } catch (const SoundscapeError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::invalidArgument,
             "Invalid soundscape capture material or settings");
    }
}
void saveSoundscape(const std::filesystem::path &output,
                    const Soundscape &scene) {
    try {
        if (output.empty() || output.extension() != ".atlas-scene")
            fail(Code::invalidArgument,
                 "Choose a new .atlas-scene output file");
        if (std::filesystem::symlink_status(output).type() !=
            std::filesystem::file_type::not_found)
            fail(Code::alreadyExists,
                 "Existing soundscape files and symlinks are never replaced");
        const auto parent = output.has_parent_path()
                                ? output.parent_path()
                                : std::filesystem::path(".");
        if (std::filesystem::space(parent).available <
            maximumBytes + 8 * 1024 * 1024)
            fail(Code::storage, "Insufficient free space for soundscape save");
        Staging staging(output);
        const auto temporary = staging.directory / "scene.atlas-scene";
        {
            Database db(temporary, true);
            run(db.get(), "PRAGMA journal_mode=DELETE; PRAGMA "
                          "synchronous=FULL; PRAGMA application_id=1096045379; "
                          "BEGIN IMMEDIATE");
            const auto revisionSql =
                "PRAGMA user_version=" +
                std::to_string(scene.capture().schemaRevision);
            run(db.get(), revisionSql.c_str());
            run(db.get(), metaSql);
            run(db.get(), eventsSql);
            run(db.get(), profilesSql);
            auto meta =
                metadata(scene.capture(), scene.timeline().durationUs());
            meta["content_hash"] = scene.contentHash();
            meta["header_hash"] =
                headerHash(meta, scene.capture().schemaRevision);
            {
                Insert query(db.get(), "INSERT INTO scene_meta VALUES(?1,?2)");
                for (const auto &[key, value] : meta) {
                    query.text(1, key);
                    query.text(2, value);
                    query.execute();
                }
            }
            {
                Insert query(db.get(),
                             "INSERT INTO scene_profiles "
                             "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
                std::int64_t slot{};
                for (const auto &profile : scene.capture().profiles) {
                    query.integer(1, slot++);
                    query.text(2, profile.speciesId);
                    query.integer(3, profile.settings.tuningSemitones);
                    query.number(4, profile.settings.attackSeconds);
                    query.number(5, profile.settings.decaySeconds);
                    query.number(6, profile.settings.gainDb);
                    query.integer(7, static_cast<std::int64_t>(
                                         profile.settings.character));
                    query.number(8, profile.settings.brightness);
                    query.number(9, profile.settings.roughness);
                    query.number(10, profile.settings.pulseHz);
                    query.number(11, profile.settings.sweepSemitones);
                    query.execute();
                }
            }
            {
                Insert query(db.get(),
                             "INSERT INTO scene_events VALUES(?1,?2,?3)");
                std::int64_t ordinal{};
                for (const auto &event : scene.timeline().events()) {
                    query.integer(1, ordinal++);
                    query.integer(2, event.revisionSequence);
                    query.text(3, event.normalizedJson);
                    query.execute();
                }
            }
            run(db.get(), "COMMIT");
            db.close();
        }
        // Reopen and verify the completed immutable file before publication.
        if (loadSoundscape(temporary).contentHash() != scene.contentHash())
            fail(Code::corrupt, "Soundscape save verification failed");
        std::error_code error;
        std::filesystem::create_hard_link(temporary, output, error);
        if (error) {
            if (std::filesystem::symlink_status(output).type() !=
                std::filesystem::file_type::not_found)
                fail(Code::alreadyExists,
                     "Soundscape output appeared during save");
            fail(Code::storage, "Atomic soundscape publication requires "
                                "same-filesystem hard-link support");
        }
    } catch (const SoundscapeError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::storage, "Soundscape save failed");
    }
}
Soundscape loadSoundscape(const std::filesystem::path &path) {
    try {
        checkPath(path);
        Database db(path);
        run(db.get(), "BEGIN");
        auto header = readHeader(db.get());
        auto &capture = header.capture;
        {
            detail::Statement query(
                db.get(),
                "SELECT "
                "slot,species_id,tuning,attack,decay,gain,character,brightness,"
                "roughness,pulse_hz,sweep_semitones,typeof(attack),typeof("
                "decay),typeof(gain),typeof(brightness),typeof(roughness),"
                "typeof(pulse_hz),typeof(sweep_semitones) FROM scene_profiles "
                "ORDER BY slot LIMIT 514");
            capture.profiles.clear();
            capture.profiles.reserve(header.profileCount);
            while (query.next()) {
                if (capture.profiles.size() >= header.profileCount ||
                    query.integer(0) !=
                        static_cast<std::int64_t>(capture.profiles.size()))
                    fail(Code::corrupt, "Invalid soundscape voice slots");
                const auto tuning = query.integer(2),
                           character = query.integer(6);
                if (tuning < -12 || tuning > 12 || character < 0 ||
                    character > 5)
                    fail(Code::corrupt, "Invalid soundscape voice integers");
                for (int index : {11, 12, 13, 14, 15, 16, 17})
                    if (query.text(index) != "real" &&
                        query.text(index) != "integer")
                        fail(Code::corrupt,
                             "Invalid soundscape voice numeric type");
                VoiceSettings settings{static_cast<int>(tuning),
                                       query.number(3), query.number(4),
                                       query.number(5)};
                settings.character = static_cast<VoiceCharacter>(character);
                settings.brightness = query.number(7);
                settings.roughness = query.number(8);
                settings.pulseHz = query.number(9);
                settings.sweepSemitones = query.number(10);
                capture.profiles.push_back({query.text(1), settings});
            }
            if (capture.profiles.size() != header.profileCount)
                fail(Code::corrupt, "Soundscape profile count mismatch");
        }
        std::vector<Event> events;
        events.reserve(capture.session.eventCount);
        std::size_t bytes{};
        {
            detail::Statement query(
                db.get(), "SELECT ordinal,original_seq,event_json FROM "
                          "scene_events ORDER BY ordinal LIMIT 50001");
            while (query.next()) {
                if (events.size() >= capture.session.eventCount ||
                    query.integer(0) !=
                        static_cast<std::int64_t>(events.size()))
                    fail(Code::corrupt,
                         "Invalid soundscape event ordinal/count");
                auto raw = query.text(2);
                bytes += raw.size();
                if (bytes > maximumBytes)
                    fail(Code::capacity,
                         "Soundscape event material exceeds capacity");
                events.push_back(
                    parseEvent(db.get(), std::move(raw), query.integer(1)));
            }
        }
        if (events.size() != capture.session.eventCount)
            fail(Code::corrupt, "Soundscape event count mismatch");
        Timeline timeline(std::move(events), capture.basis, capture.speed,
                          capture.bounds);
        if (timeline.durationUs() != header.duration)
            fail(Code::corrupt,
                 "Soundscape duration does not match saved timeline");
        validateCapture(capture, timeline.events().size());
        checkCompleteProfiles(timeline, capture);
        if (contentHash(header.metadata, capture, timeline) != header.hash)
            fail(Code::corrupt, "Soundscape content integrity mismatch");
        return Soundscape(std::move(capture), std::move(timeline),
                          std::move(header.hash));
    } catch (const SoundscapeError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::corrupt,
             "Malformed soundscape metadata, event material or settings");
    }
}
SoundscapeSummary inspectSoundscape(const std::filesystem::path &path) {
    try {
        checkPath(path);
        Database db(path);
        run(db.get(), "BEGIN");
        const auto header = readHeader(db.get());
        SoundscapeSummary summary;
        summary.path = path;
        summary.name = header.capture.name;
        summary.sourceSessionId = header.capture.session.id;
        summary.contentHash = header.hash;
        summary.eventCount = header.capture.session.eventCount;
        summary.durationUs = header.duration;
        summary.basis = header.capture.basis;
        summary.speed = header.capture.speed;
        summary.valid = true;
        return summary;
    } catch (const SoundscapeError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::corrupt, "Malformed soundscape metadata");
    }
}
std::vector<SoundscapeSummary>
listSoundscapes(const std::filesystem::path &directory, std::size_t limit) {
    if (limit < 1 || limit > 100)
        fail(Code::invalidArgument, "Soundscape list limit must be 1..100");
    try {
        std::vector<std::filesystem::path> paths;
        std::size_t inspected{};
        for (const auto &entry :
             std::filesystem::directory_iterator(directory)) {
            if (++inspected > 1000)
                fail(Code::capacity,
                     "Soundscape directory scan exceeds 1000 entries");
            if (entry.path().extension() == ".atlas-scene")
                paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
        if (paths.size() > limit)
            paths.resize(limit);
        std::vector<SoundscapeSummary> result;
        result.reserve(paths.size());
        for (const auto &path : paths) {
            SoundscapeSummary summary;
            summary.path = path;
            try {
                summary = inspectSoundscape(path);
            } catch (const SoundscapeError &error) {
                summary.error = error.what();
                summary.errorCode = error.code();
            } catch (const std::exception &) {
                summary.error = "Malformed soundscape metadata";
                summary.errorCode = Code::corrupt;
            }
            result.push_back(std::move(summary));
        }
        return result;
    } catch (const SoundscapeError &) {
        throw;
    } catch (const std::exception &) {
        fail(Code::storage, "Unable to list soundscape directory");
    }
}
} // namespace atlas
