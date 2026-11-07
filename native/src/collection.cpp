#include "event_validation.hpp"
#include "json_text.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <atlas/collection.hpp>
#include <atlas/inaturalist.hpp>
#include <atlas/core.hpp>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sqlite3.h>
#include <vector>

namespace atlas {
namespace {
using detail::quoteJson;
constexpr std::size_t responseLimit = 4U * 1024U * 1024U;
constexpr std::size_t birdPageSize = 250;
constexpr std::string_view ownerKey = "animal_atlas_native_v1";
class Sql {
  public:
    Sql(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            throw std::runtime_error("collection database query failed");
    }
    ~Sql() { sqlite3_finalize(statement_); }
    Sql(const Sql &) = delete;
    Sql &operator=(const Sql &) = delete;
    void bind(int index, std::string_view value) {
        if (value.size() > responseLimit ||
            sqlite3_bind_text(statement_, index, value.data(),
                              static_cast<int>(value.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("collection database bind failed");
    }
    void bind(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK)
            throw std::runtime_error("collection database bind failed");
    }
    bool next() {
        const auto result = sqlite3_step(statement_);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        throw std::runtime_error("collection database operation failed");
    }
    std::string text(int index) const {
        const auto *bytes = sqlite3_column_text(statement_, index);
        const auto count = sqlite3_column_bytes(statement_, index);
        if (!bytes || count < 0 ||
            static_cast<std::size_t>(count) > responseLimit)
            throw std::runtime_error("collection database text invalid");
        return {reinterpret_cast<const char *>(bytes),
                static_cast<std::size_t>(count)};
    }
    std::int64_t number(int index) const {
        return sqlite3_column_int64(statement_, index);
    }

  private:
    sqlite3_stmt *statement_{};
};
void execute(sqlite3 *db, const char *text) {
    if (sqlite3_exec(db, text, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("collection database operation failed");
}
struct Database {
    sqlite3 *value{};
    ~Database() {
        if (value)
            sqlite3_close(value);
    }
    void open(const std::filesystem::path &path, int flags) {
        const auto utf8 = path.u8string();
        const std::string filename(reinterpret_cast<const char *>(utf8.data()),
                                   utf8.size());
        if (sqlite3_open_v2(filename.c_str(), &value,
                            flags | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK)
            throw std::runtime_error(
                "managed collection library cannot be opened");
        sqlite3_busy_timeout(value, 5000);
    }
};
struct Field {
    std::string type, json;
};
using Fields = std::map<std::string, std::string>;
std::string object(const Fields &fields) {
    std::string value = "{";
    for (const auto &[key, item] : fields) {
        if (value.size() > 1)
            value += ',';
        value += quoteJson(key) + ':' + item;
    }
    return value + '}';
}
class Json {
  public:
    Json(sqlite3 *db, std::string_view value) : db_(db) {
        Sql valid(db, "SELECT json_valid(?1)");
        valid.bind(1, value);
        if (!valid.next() || valid.number(0) != 1)
            throw std::invalid_argument("invalid provider JSON");
        Sql type(db, "SELECT json_type(?1)");
        type.bind(1, value);
        if (!type.next() || type.text(0) != "object")
            throw std::invalid_argument("provider object required");
        Sql fields(db, "SELECT key,type,CASE WHEN type IN ('array','object') "
                       "THEN json(value) "
                       "WHEN type='true' THEN 'true' WHEN type='false' THEN "
                       "'false' ELSE json_quote(value) END FROM json_each(?1)");
        fields.bind(1, value);
        while (fields.next()) {
            if (!fields_
                     .emplace(fields.text(0),
                              Field{fields.text(1), fields.text(2)})
                     .second)
                throw std::invalid_argument("duplicate provider JSON field");
        }
    }
    bool null(std::string_view key) const {
        const auto i = fields_.find(std::string(key));
        return i == fields_.end() || i->second.type == "null";
    }
    const Field &field(std::string_view key) const {
        const auto i = fields_.find(std::string(key));
        if (i == fields_.end())
            throw std::invalid_argument("required provider field missing");
        return i->second;
    }
    std::string json(std::string_view key) const {
        return null(key) ? "null" : field(key).json;
    }
    std::string text(std::string_view key, std::size_t maximum = 2048) const {
        if (field(key).type != "text")
            throw std::invalid_argument("provider text required");
        Sql query(db_, "SELECT json_extract(?1,'$')");
        query.bind(1, field(key).json);
        if (!query.next())
            throw std::invalid_argument("provider text required");
        auto result = query.text(0);
        if (result.empty() || result.size() > maximum ||
            result.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid provider text");
        return result;
    }
    std::string id(std::string_view key) const {
        if (field(key).type == "integer")
            return field(key).json;
        return text(key, 380);
    }
    Json nested(std::string_view key) const {
        return Json(db_, field(key).json);
    }
    bool boolean(std::string_view key) const {
        const auto &type = field(key).type;
        if (type != "true" && type != "false")
            throw std::invalid_argument("provider boolean required");
        return type == "true";
    }
    std::string numeric(std::string_view key) const {
        if (null(key))
            return "null";
        const auto &item = field(key);
        if (item.type != "integer" && item.type != "real")
            throw std::invalid_argument("provider number required");
        try {
            if (!std::isfinite(std::stod(item.json)))
                throw std::invalid_argument("provider finite number required");
        } catch (const std::out_of_range &) {
            throw std::invalid_argument("provider numeric field outside range");
        }
        return item.json;
    }
    std::vector<std::string> array(std::string_view key,
                                   std::size_t maximum) const {
        if (field(key).type != "array")
            throw std::invalid_argument("provider array required");
        Sql query(db_,
                  "SELECT CASE WHEN type IN ('object','array') THEN "
                  "json(value) ELSE json_quote(value) END FROM json_each(?1)");
        query.bind(1, field(key).json);
        std::vector<std::string> values;
        while (query.next()) {
            if (values.size() >= maximum)
                throw std::invalid_argument("provider array exceeds capacity");
            values.push_back(query.text(0));
        }
        return values;
    }

  private:
    sqlite3 *db_;
    std::map<std::string, Field> fields_;
};
std::string safeCode(std::string code) {
    if (code.empty() || code.size() > 64 ||
        std::any_of(code.begin(), code.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '_');
        }))
        return "fetch_failed";
    return code;
}
std::string randomId() {
    std::random_device random;
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        const auto byte = random() & 255U;
        result += hex[byte >> 4U];
        result += hex[byte & 15U];
    }
    return result;
}
struct Record {
    Fields event;
    std::string raw;
};
struct Batch {
    std::vector<Record> records;
    CollectionResult result;
    std::size_t pages{};
    bool exhausted{};
    std::vector<std::string> withdrawals;
    std::string revalidationCursor, revalidationError;
};
Batch sightings(sqlite3* db, const Collector::Fetch& callback, const std::string& now,
                const std::string& previousCursor) {
    const auto start = parseUtc(now) - 86400000000LL;
    const auto response = callback({inaturalistRequestUrl(formatUtc(start), now), {}});
    const auto parsed = parseInaturalistResponse(response, now);
    Batch batch;
    batch.pages = 1;
    batch.result.invalidRecords = parsed.invalidRecords;
    batch.exhausted = parsed.totalResults <= parsed.perPage;
    batch.result.complete = batch.exhausted && parsed.invalidRecords == 0;
    std::map<std::string, std::string> accepted, evidence;
    const auto accept = [&](const std::string& normalized, bool recentOnly,
                            const InaturalistPage& page) {
        const Json value(db, normalized);
        if (recentOnly && parseUtc(value.text("observed_at_utc")) < start) {
            ++batch.result.invalidRecords;
            batch.result.complete = false;
            return;
        }
        const auto id = value.text("event_id");
        accepted[id] = normalized;
        evidence[id] = page.originalTimestampEvidence.at(id);
    };
    for (const auto& normalized : parsed.normalizedRecords) accept(normalized, true, parsed);
    std::vector<std::string> ids;
    const auto loadIds = [&](const std::string& cursor) {
        Sql query(db, "SELECT substr(id,13) FROM events WHERE source_id='inaturalist' AND id>?1 ORDER BY id LIMIT 30");
        query.bind(1, cursor);
        while (query.next()) ids.push_back(query.text(0));
    };
    loadIds(previousCursor);
    if (ids.empty() && !previousCursor.empty()) loadIds("");
    if (!ids.empty()) {
        try {
            const auto recheck = parseInaturalistResponse(callback({inaturalistRevalidationUrl(ids), {}}), now);
            ++batch.pages;
            if (recheck.page != 1 || recheck.perPage != 30 || recheck.invalidRecords != 0 ||
                recheck.totalResults != recheck.returnedRecords || recheck.totalResults > ids.size())
                throw std::invalid_argument("incomplete revalidation");
            std::set<std::string> returned;
            for (const auto& normalized : recheck.normalizedRecords) {
                const auto id = Json(db, normalized).text("provider_event_id");
                if (std::find(ids.begin(), ids.end(), id) == ids.end() || !returned.insert(id).second)
                    throw std::invalid_argument("unexpected revalidation identity");
            }
            auto identities = returned;
            for (const auto& id : recheck.explicitlyRevokedIds) {
                if (std::find(ids.begin(), ids.end(), id) == ids.end() || !identities.insert(id).second)
                    throw std::invalid_argument("unexpected revoked observation identity");
            }
            for (const auto& id : ids) {
                if (!returned.contains(id)) {
                    batch.withdrawals.push_back("inaturalist:" + id);
                    accepted.erase("inaturalist:" + id);
                }
            }
            for (const auto& normalized : recheck.normalizedRecords) accept(normalized, false, recheck);
            batch.revalidationCursor = "inaturalist:" + ids.back();
        } catch (const CollectionFetchError& error) {
            if (std::string_view(error.what()) == "cancelled" ||
                error.retryAfterSeconds() > 0 || std::string_view(error.what()) == "http_429" ||
                std::string_view(error.what()) == "http_503") throw;
            batch.revalidationError = error.what();
        } catch (const std::invalid_argument&) {
            batch.revalidationError = "revalidation_incomplete";
        }
    }
    if (!batch.revalidationError.empty()) batch.result.complete = false;
    for (const auto& [id, normalized] : accepted) {
        (void)id;
        Fields fields;
        Sql query(db, "SELECT key,CASE WHEN type IN ('array','object') THEN json(value) ELSE json_quote(value) END FROM json_each(?1)");
        query.bind(1, normalized);
        while (query.next()) {
            const auto key = query.text(0);
            if (key != "first_seen_at_utc" && key != "ingested_at_utc") fields.emplace(key, query.text(1));
        }
        batch.records.push_back({std::move(fields), "{\"normalized_event\":" + normalized +
            ",\"original_timestamps\":" + evidence.at(id) + "}"});
    }
    return batch;
}
constexpr auto birdQuery =
    R"(query Atlas($first:Int,$after:String,$period:InputDuration,$ne:InputLocation,$sw:InputLocation) { detections(first:$first,after:$after,period:$period,ne:$ne,sw:$sw) { nodes { id timestamp coords {lat lon} species {id scientificName commonName} station {id} confidence score probability certainty } pageInfo {endCursor hasNextPage} } })";
Fields event(std::string_view source, const std::string &providerId,
             std::string observed, std::string_view kind,
             std::string_view location, std::string lat, std::string lon) {
    return {{"schema_version", "1"},
            {"event_id", quoteJson(std::string(source) + ":" + providerId)},
            {"source_id", quoteJson(source)},
            {"provider_event_id", quoteJson(providerId)},
            {"provider_record_revision", "null"},
            {"observed_at_utc", quoteJson(observed)},
            {"provider_published_at_utc", "null"},
            {"event_kind", quoteJson(kind)},
            {"location_kind", quoteJson(location)},
            {"latitude", std::move(lat)},
            {"longitude", std::move(lon)},
            {"coordinate_order", quoteJson("latitude,longitude")},
            {"uncertainty_m", "null"},
            {"species_id", "null"},
            {"scientific_name", "null"},
            {"common_name", "null"},
            {"individual_id", "null"},
            {"deployment_id", "null"},
            {"station_id", "null"},
            {"confidence", "null"},
            {"confidence_meaning", "null"},
            {"quality_flags", "[]"},
            {"provenance_mode", quoteJson("live_source")},
            {"permissions_status", quoteJson("unresolved")},
            {"attribution", "null"}};
}
void validate(sqlite3 *db, const Fields &fields, const std::string &now) {
    auto frozen = fields;
    frozen["first_seen_at_utc"] = quoteJson(now);
    frozen["ingested_at_utc"] = quoteJson(now);
    detail::validateStoredEvent(db, object(frozen));
}
std::string fetch(const Collector::Fetch &callback, CollectionRequest request) {
    auto value = callback(request);
    if (value.size() > responseLimit)
        throw CollectionFetchError("response_too_large");
    return value;
}
struct BirdRegion {
    const char *id;
    const char *southwest;
    const char *northeast;
};
// Oceania straddles the antimeridian: two explicit query slots avoid
// relying on undocumented wrapping of the provider's bounding boxes.
constexpr BirdRegion birdRegions[]{
    {"nz", R"({"lat":-48,"lon":166})", R"({"lat":-34,"lon":179.9})"},
    {"oceania_west", R"({"lat":-60,"lon":110})", R"({"lat":0,"lon":180})"},
    {"asia", R"({"lat":0,"lon":25})", R"({"lat":85,"lon":180})"},
    {"africa", R"({"lat":-60,"lon":-20})", R"({"lat":38,"lon":55})"},
    {"europe", R"({"lat":35,"lon":-25})", R"({"lat":85,"lon":45})"},
    {"north_america", R"({"lat":5,"lon":-180})", R"({"lat":85,"lon":-25})"},
    {"south_america", R"({"lat":-60,"lon":-90})", R"({"lat":15,"lon":-30})"},
    {"oceania_east", R"({"lat":-60,"lon":-180})", R"({"lat":0,"lon":-110})"}
};
Batch birds(sqlite3 *db, const Collector::Fetch &callback,
            const std::string &now, const BirdRegion &region, unsigned pageLimit) {
    Batch batch;
    std::string cursor = "null";
    std::set<std::string> cursors;
    for (unsigned page = 0; page < pageLimit; ++page) {
        Fields variables{{"first", std::to_string(birdPageSize)},
                         {"after", cursor},
                         {"period", R"({"count":10,"unit":"minute"})"},
                         {"sw", region.southwest},
                         {"ne", region.northeast}};
        const auto raw =
            fetch(callback, {"https://app.birdweather.com/graphql",
                             object({{"query", quoteJson(birdQuery)},
                                     {"variables", object(variables)}})});
        const Json root(db, raw);
        if (!root.null("errors") && !root.array("errors", 100).empty())
            throw CollectionFetchError("graphql_error");
        const auto connection = root.nested("data").nested("detections");
        const auto nodes = connection.array("nodes", birdPageSize);
        ++batch.pages;
        for (const auto &node : nodes) {
            try {
                const Json item(db, node);
                const auto when = parseUtc(item.text("timestamp"));
                if (when > parseUtc(now)) {
                    ++batch.result.deferredFuture;
                    continue;
                }
                const auto coords = item.null("coords") ? Json(db, "{}")
                                                        : item.nested("coords");
                auto fields =
                    event("birdweather", item.id("id"), formatUtc(when),
                          "acoustic_species_detection", "station_detection",
                          coords.numeric("lat"), coords.numeric("lon"));
                const auto species = item.null("species")
                                         ? Json(db, "{}")
                                         : item.nested("species");
                const auto station = item.null("station")
                                         ? Json(db, "{}")
                                         : item.nested("station");
                fields["species_id"] =
                    species.null("id")
                        ? "null"
                        : quoteJson("birdweather:species:" + species.id("id"));
                fields["scientific_name"] = species.json("scientificName");
                fields["common_name"] = species.json("commonName");
                fields["station_id"] =
                    station.null("id")
                        ? "null"
                        : quoteJson("birdweather:station:" + station.id("id"));
                fields["confidence"] = item.numeric("confidence");
                fields["confidence_meaning"] =
                    quoteJson("BirdWeather reported confidence; not "
                              "cross-provider calibrated");
                Fields scores{{"score", item.numeric("score")},
                              {"probability", item.numeric("probability")}};
                fields["provider_scores"] = object(scores);
                std::string flags =
                    R"(["station_location_not_individual_position","machine_classification_unverified")";
                if (coords.null("lat") && coords.null("lon"))
                    flags += R"(,"location_missing")";
                if (!item.null("certainty"))
                    flags += ',' + quoteJson("birdweather_certainty:" +
                                             item.text("certainty", 80));
                fields["quality_flags"] = flags + ']';
                fields["attribution"] =
                    quoteJson("BirdWeather and contributing station");
                fields["source_url"] =
                    quoteJson("https://app.birdweather.com/");
                validate(db, fields, now);
                batch.records.push_back({std::move(fields), node});
            } catch (const std::invalid_argument &) {
                ++batch.result.invalidRecords;
            }
        }
        const auto info = connection.nested("pageInfo");
        if (!info.boolean("hasNextPage")) {
            batch.exhausted = true;
            break;
        }
        cursor = quoteJson(info.text("endCursor", 2048));
        if (!cursors.insert(cursor).second)
            throw CollectionFetchError("schema_or_pagination_error");
    }
    batch.result.complete = batch.exhausted &&
                            batch.result.invalidRecords == 0 &&
                            batch.result.deferredFuture == 0;
    return batch;
}
Batch sharks(sqlite3 *db, const Collector::Fetch &callback,
             const std::string &now) {
    const Json bootstrap(
        db,
        fetch(
            callback,
            {"https://map.sustainableoceansociety.co.nz/public/bootstrap.json",
             {}}));
    constexpr auto configUrl =
        "https://map.sustainableoceansociety.co.nz/public/configs/config.json";
    if (bootstrap.text("configUrl") != configUrl)
        throw CollectionFetchError("public_config_changed_review_required");
    const Json config(db, fetch(callback, {configUrl, {}}));
    if (config.text("server") != "ermap-server.pamdas.org" ||
        config.text("public_name") != "nzsharkman")
        throw CollectionFetchError("public_server_changed_review_required");
    const auto group = config.text("subject_group", 128);
    // Query data are percent encoded; configuration can never change the
    // host/path.
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const char character : group) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~')
            encoded += static_cast<char>(byte);
        else {
            encoded += '%';
            encoded += hex[byte >> 4U];
            encoded += hex[byte & 15U];
        }
    }
    const Json response(
        db, fetch(callback, {"https://ermap-server.pamdas.org/nzsharkman/api/"
                             "v1.0/subjects?subject_group=" +
                                 encoded,
                             {}}));
    const auto subjects = response.nested("data").array("data", 1000);
    const auto names =
        config.null("subjects") ? Json(db, "{}") : config.nested("subjects");
    Batch batch;
    batch.pages = 1;
    batch.exhausted = true;
    for (const auto &raw : subjects) {
        try {
            const Json subject(db, raw);
            if (subject.null("last_position"))
                continue;
            const auto point = subject.nested("last_position");
            const auto geometry = point.nested("geometry");
            if (geometry.text("type") != "Point")
                throw std::invalid_argument("provider point required");
            const auto coords = geometry.array("coordinates", 2);
            if (coords.size() != 2)
                throw std::invalid_argument(
                    "provider coordinate pair required");
            // Numeric types are checked by the canonical event validator below.
            const auto props = point.nested("properties");
            const auto when = parseUtc(props.text("DateTime"));
            if (when > parseUtc(now)) {
                ++batch.result.deferredFuture;
                continue;
            }
            const auto observed = formatUtc(when), id = subject.id("id");
            auto fields =
                event("nzsharks", id + ":" + observed, observed,
                      "satellite_fix", "individual_fix", coords[1], coords[0]);
            fields["species_id"] =
                quoteJson("nzsharks:species:great_white_shark");
            fields["scientific_name"] = quoteJson("Carcharodon carcharias");
            fields["common_name"] = quoteJson("Great white shark");
            fields["provider_subject_id"] = quoteJson("nzsharks:subject:" + id);
            const auto named =
                names.null(id) ? Json(db, "{}") : names.nested(id);
            fields["individual_display_name"] = !named.null("animal_name")
                                                    ? named.json("animal_name")
                                                    : subject.json("name");
            fields["quality_flags"] =
                R"(["identity_reconciliation_pending","position_accuracy_unknown"])";
            fields["attribution"] =
                quoteJson("Sustainable Oceans Society / The Great White App");
            fields["source_url"] =
                quoteJson("https://map.sustainableoceansociety.co.nz/");
            validate(db, fields, now);
            batch.records.push_back(
                {std::move(fields),
                 object({{"subject_id", quoteJson(id)},
                         {"observed_at", props.json("DateTime")},
                         {"coordinates", geometry.json("coordinates")}})});
        } catch (const std::invalid_argument &) {
            ++batch.result.invalidRecords;
        }
    }
    batch.result.complete =
        batch.result.invalidRecords == 0 && batch.result.deferredFuture == 0;
    return batch;
}
} // namespace
CollectionFetchError::CollectionFetchError(std::string code,
                                           int retryAfterSeconds)
    : std::runtime_error(safeCode(std::move(code))),
      retryAfterSeconds_(std::clamp(retryAfterSeconds, 0, 86400)) {}
int CollectionFetchError::retryAfterSeconds() const noexcept {
    return retryAfterSeconds_;
}
struct Collector::Impl {
    Database database;
    std::filesystem::path path;
    std::set<std::string> establishedSources;
    Fields status(std::string_view source) {
        Sql query(database.value, "SELECT status FROM sources WHERE id=?1");
        query.bind(1, source);
        if (!query.next())
            return {};
        const auto raw = query.text(0);
        Json value(database.value, raw);
        (void)value;
        Fields result;
        Sql fields(
            database.value,
            "SELECT key,CASE WHEN type IN ('object','array') THEN json(value) "
            "WHEN type='true' THEN 'true' WHEN type='false' THEN 'false' ELSE "
            "json_quote(value) END FROM json_each(?1)");
        fields.bind(1, raw);
        while (fields.next())
            result.emplace(fields.text(0), fields.text(1));
        return result;
    }
    void saveStatus(std::string_view source, const Fields &fields) {
        Sql write(database.value,
                  "INSERT INTO sources(id,status) VALUES(?1,?2) ON "
                  "CONFLICT(id) DO UPDATE SET status=excluded.status");
        write.bind(1, source);
        write.bind(2, object(fields));
        write.next();
    }
    void failure(std::string_view source, const std::string &now,
                 std::string_view code, int retryAfter) {
        auto fields = status(source);
        const Json previous(database.value, object(fields));
        const auto errors =
            previous.null("consecutive_errors")
                ? 0
                : std::stoi(previous.numeric("consecutive_errors"));
        const auto delay = std::max(
            retryAfter, std::min(3600, 60 * (1 << std::min(errors, 6))));
        fields["last_attempt_at_utc"] = quoteJson(now);
        fields["last_error"] = quoteJson(code);
        fields["consecutive_errors"] =
            std::to_string(std::min(errors + 1, 1000000));
        fields["retry_delay_seconds"] = std::to_string(delay);
        fields["last_poll_complete"] = "false";
        saveStatus(source, fields);
    }
};
Collector::Collector(const std::filesystem::path &path)
    : impl_(std::make_unique<Impl>()) {
    if (!path.is_absolute() || path.filename().empty() ||
        !std::filesystem::is_directory(path.parent_path()))
        throw std::invalid_argument(
            "managed collection library requires an absolute existing parent");
    const auto status = std::filesystem::symlink_status(path);
    const bool exists = std::filesystem::exists(status);
    if (exists) {
        if (!std::filesystem::is_regular_file(status))
            throw std::invalid_argument(
                "managed collection library must be a regular file");
        Database check;
        check.open(path, SQLITE_OPEN_READONLY);
        Sql marker(
            check.value,
            "SELECT value FROM metadata WHERE key='native_collection_owner'");
        if (!marker.next() || marker.text(0) != ownerKey)
            throw std::invalid_argument(
                "refusing to write an unmanaged wildlife library");
        Sql version(check.value, "PRAGMA user_version");
        if (!version.next() || version.number(0) != 3)
            throw std::invalid_argument(
                "unsupported managed collection schema");
    }
    impl_->path = path;
    impl_->database.open(path,
                         exists ? SQLITE_OPEN_READWRITE
                                : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    auto *db = impl_->database.value;
    if (!exists) {
        execute(db, "BEGIN IMMEDIATE");
        try {
            Sql tables(db, "SELECT count(*) FROM sqlite_master");
            if (!tables.next() || tables.number(0) != 0)
                throw std::invalid_argument(
                    "refusing to initialize an existing database");
            execute(db, R"(
CREATE TABLE sources(id TEXT PRIMARY KEY,status TEXT NOT NULL);
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE polls(id INTEGER PRIMARY KEY AUTOINCREMENT,source_id TEXT NOT NULL,completed_at TEXT NOT NULL,complete INTEGER NOT NULL,details TEXT NOT NULL);
CREATE TABLE revisions(seq INTEGER PRIMARY KEY AUTOINCREMENT,event_id TEXT NOT NULL,source_id TEXT NOT NULL,fingerprint TEXT NOT NULL,observed_at TEXT NOT NULL,first_seen_at TEXT NOT NULL,ingested_at TEXT NOT NULL,change_kind TEXT NOT NULL,live_eligible INTEGER NOT NULL,event_json TEXT NOT NULL,raw_json TEXT NOT NULL);
CREATE INDEX revisions_source ON revisions(source_id,seq);
CREATE TABLE events(id TEXT PRIMARY KEY,source_id TEXT NOT NULL,observed_at TEXT NOT NULL,revision_seq INTEGER NOT NULL REFERENCES revisions(seq));
CREATE INDEX events_time ON events(observed_at,id);
CREATE INDEX events_source ON events(source_id,observed_at);
CREATE TABLE sessions(id TEXT PRIMARY KEY,name TEXT NOT NULL,state TEXT NOT NULL,created_at TEXT NOT NULL,ended_at TEXT,start_seq INTEGER NOT NULL,end_seq INTEGER,intended_end_at TEXT,origin TEXT NOT NULL,options_json TEXT NOT NULL,event_count INTEGER NOT NULL DEFAULT 0,content_hash TEXT,envelope_version INTEGER NOT NULL DEFAULT 0,envelope_json TEXT,envelope_hash TEXT);
CREATE TABLE session_events(session_id TEXT NOT NULL REFERENCES sessions(id),ordinal INTEGER NOT NULL,original_seq INTEGER NOT NULL,event_json TEXT NOT NULL,PRIMARY KEY(session_id,ordinal));
PRAGMA user_version=3;
)");
            Sql marker(
                db,
                "INSERT INTO metadata VALUES('native_collection_owner',?1)");
            marker.bind(1, ownerKey);
            marker.next();
            Sql identity(db, "INSERT INTO metadata VALUES('store_id',?1)");
            identity.bind(1, randomId());
            identity.next();
            execute(db, "COMMIT");
        } catch (...) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
    execute(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA "
                "foreign_keys=ON");
    // Bound every JSON boundary even when the HTTP adapter is replaced in
    // tests.
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH,
                  static_cast<int>(responseLimit + 65536));
}
Collector::~Collector() = default;
std::vector<CollectionSourceState> Collector::sourceStates() const {
    std::vector<CollectionSourceState> result;
    std::vector<std::string> ids{"birdweather", "nzsharks", "inaturalist"};
    if (!impl_->status("birdweather_world").empty()) {
        ids.emplace_back("birdweather_world");
        for (const auto &region : birdRegions)
            ids.emplace_back(std::string("birdweather:") + region.id);
    }
    for (const auto &id : ids) {
        const Json state(impl_->database.value, object(impl_->status(id)));
        CollectionSourceState item;
        item.id = id;
        if (!state.null("last_attempt_at_utc"))
            item.lastAttemptUtc = state.text("last_attempt_at_utc");
        if (!state.null("last_success_at_utc"))
            item.lastSuccessUtc = state.text("last_success_at_utc");
        if (!state.null("latest_observation_at_utc"))
            item.lastObservationUtc = state.text("latest_observation_at_utc");
        if (!state.null("last_error"))
            item.error = state.text("last_error");
        if (!state.null("unique_events"))
            item.records = std::stoull(state.numeric("unique_events"));
        if (!state.null("retry_delay_seconds"))
            item.retryDelaySeconds =
                static_cast<unsigned>(std::min<std::uint64_t>(
                    86400, std::stoull(state.numeric("retry_delay_seconds"))));
        if (!state.null("consecutive_errors"))
            item.consecutiveErrors =
                static_cast<unsigned>(std::min<std::uint64_t>(
                    1000000, std::stoull(state.numeric("consecutive_errors"))));
        item.complete = !state.null("last_poll_complete") &&
                        state.boolean("last_poll_complete");
        item.gap = !state.null("possible_unrecoverable_gap") &&
                   state.boolean("possible_unrecoverable_gap");
        result.push_back(std::move(item));
    }
    return result;
}
CollectionResult Collector::collect(std::string_view source,
                                    std::string_view nowUtc,
                                    const Fetch &callback) {
    if (source == "birdweather_world") {
        // Four distinct geographic slots get a turn before continuation.
        // Cursor paging is bounded to this request's relative lookback;
        // saturation stays explicitly incomplete rather than claiming coverage.
        const auto now = formatUtc(parseUtc(nowUtc));
        auto state = impl_->status(source);
        const Json prior(impl_->database.value, object(state));
        if (!prior.null("last_attempt_at_utc") &&
            parseUtc(now) - parseUtc(prior.text("last_attempt_at_utc")) < 60000000LL)
            throw CollectionFetchError("provider_budget_wait", 60);
        const unsigned count = std::size(birdRegions);
        unsigned next = prior.null("next_region") ? 0U :
            static_cast<unsigned>(std::stoul(prior.numeric("next_region"))) % count;
        state["last_attempt_at_utc"] = quoteJson(now);
        impl_->saveStatus(source, state);
        CollectionResult total;
        total.complete = true;
        unsigned successes{};
        std::string failure;
        int retry{};
        for (unsigned request = 0; request < 4; ++request) {
            const auto region = next++ % count;
            // Record the attempted slot before fetching; an interrupted region
            // cannot indefinitely starve the following regions after restart.
            state["next_region"] = std::to_string(next % count);
            impl_->saveStatus(source, state);
            try {
                const auto result = collect(std::string("birdweather:") +
                    birdRegions[region].id, now, callback);
                ++successes;
                total.inserted += result.inserted;
                total.corrected += result.corrected;
                total.duplicates += result.duplicates;
                total.invalidRecords += result.invalidRecords;
                total.deferredFuture += result.deferredFuture;
                total.complete = total.complete && result.complete;
            } catch (const CollectionFetchError &error) {
                if (std::string_view(error.what()) == "cancelled") throw;
                failure = error.what();
                total.complete = false;
                retry = error.retryAfterSeconds();
                if (retry || failure == "http_429" || failure == "http_503" ||
                    failure == "local_storage_capacity") break;
            }
        }
        std::string latest;
        std::uint64_t insertedByRegions{};
        bool gap{};
        for (const auto &slot : birdRegions) {
            const Json local(impl_->database.value,
                object(impl_->status(std::string("birdweather:") + slot.id)));
            if (!local.null("unique_events"))
                insertedByRegions += std::stoull(local.numeric("unique_events"));
            if (!local.null("latest_observation_at_utc"))
                latest = std::max(latest, local.text("latest_observation_at_utc"));
            gap |= !local.null("possible_unrecoverable_gap") &&
                   local.boolean("possible_unrecoverable_gap");
        }
        state["possible_unrecoverable_gap"] = gap ? "true" : "false";
        state["latest_observation_at_utc"] = latest.empty() ? "null" : quoteJson(latest);
        state["unique_events"] = std::to_string(insertedByRegions);
        state["last_poll_complete"] = total.complete ? "true" : "false";
        if (successes) state["last_success_at_utc"] = quoteJson(now);
        if (failure.empty()) {
            state["last_error"] = total.complete ? "null" :
                (total.invalidRecords || total.deferredFuture ? quoteJson("invalid_or_future_records") : quoteJson("pagination_limit_reached"));
            state["consecutive_errors"] = "0";
            state["retry_delay_seconds"] = "0";
        }
        impl_->saveStatus(source, state);
        if (!failure.empty()) {
            impl_->failure(source, now, failure, retry);
            throw CollectionFetchError(failure, retry);
        }
        return total;
    }
    const BirdRegion *region = nullptr;
    for (const auto &candidate : birdRegions)
        if (source == std::string("birdweather:") + candidate.id)
            region = &candidate;
    const bool regional = region != nullptr;
    const bool birdSource = regional || source == "birdweather";
    const auto eventSource = birdSource ? std::string_view("birdweather") : source;
    if (!birdSource && source != "nzsharks" && source != "inaturalist")
        throw std::invalid_argument("unsupported collection source");
    if (!callback)
        throw std::invalid_argument("collection fetch callback required");
    const auto now = formatUtc(parseUtc(nowUtc));
    auto *db = impl_->database.value;
    auto status = impl_->status(source);
    const Json priorState(db, object(status));
    const bool arrivalReady =
        (regional || impl_->establishedSources.contains(std::string(source))) &&
        !priorState.null("arrival_baseline_established") &&
        priorState.boolean("arrival_baseline_established");
    const auto arrivalCutoff =
        arrivalReady && !priorState.null("last_attempt_at_utc")
            ? parseUtc(priorState.text("last_attempt_at_utc"))
            : parseUtc(now);
    status["enabled"] = "true";
    status["configured_poll_seconds"] = birdSource ? "60" : "900";
    status["last_attempt_at_utc"] = quoteJson(now);
    status["permissions_status"] = quoteJson(source == "inaturalist" ? "cc0_or_cc_by_observations_at_fetch;media_excluded" : "unresolved");
    status["technical_status"] = quoteJson("endpoint_tested");
    status["monetary_cost"] = "0";
    impl_->saveStatus(source, status);
    try {
        std::uintmax_t bytes{};
        for (const auto &suffix : {"", "-wal", "-shm"}) {
            const auto file =
                std::filesystem::path(impl_->path.string() + suffix);
            if (std::filesystem::is_regular_file(file))
                bytes += std::filesystem::file_size(file);
        }
        if (bytes >= 512U * 1024U * 1024U ||
            std::filesystem::space(impl_->path.parent_path()).available <
                64U * 1024U * 1024U)
            throw CollectionFetchError("local_storage_capacity");
        auto batch = birdSource ? birds(db, callback, now, regional ? *region : birdRegions[0], regional ? 1U : 4U)
                                : source == "inaturalist" ? sightings(db, callback, now, priorState.null("revalidation_after_id") ? "" : priorState.text("revalidation_after_id"))
                                : sharks(db, callback, now);
        const Json previous(db, object(status));
        const bool baseline = previous.null("baseline_complete") ||
                              !previous.boolean("baseline_complete");
        bool gap = !previous.null("possible_unrecoverable_gap") &&
                   previous.boolean("possible_unrecoverable_gap");
        if (birdSource && !previous.null("last_success_at_utc") &&
            parseUtc(previous.text("last_success_at_utc")) <
                parseUtc(now) - 600000000LL)
            gap = true;
        execute(db, "BEGIN IMMEDIATE");
        try {
            if (source == "inaturalist") {
                for (const auto& id : batch.withdrawals) {
                    Sql removeEvent(db, "DELETE FROM events WHERE id=?1 AND source_id='inaturalist'");
                    removeEvent.bind(1, id); removeEvent.next();
                    Sql removeRevisions(db, "DELETE FROM revisions WHERE event_id=?1 AND source_id='inaturalist'");
                    removeRevisions.bind(1, id); removeRevisions.next();
                }
                if (!batch.withdrawals.empty()) {
                    Sql rotate(db, "UPDATE metadata SET value=?1 WHERE key='store_id'");
                    rotate.bind(1, randomId()); rotate.next();
                }
                if (!batch.revalidationCursor.empty()) {
                    status["revalidation_after_id"] = quoteJson(batch.revalidationCursor);
                    status["last_revalidation_at_utc"] = quoteJson(now);
                }
                status["last_revalidation_error"] = batch.revalidationError.empty() ? "null" : quoteJson(batch.revalidationError);
                status["last_revalidation_withdrawn"] = std::to_string(batch.withdrawals.size());
                status["revalidation_policy"] = quoteJson("round_robin_30_per_poll; managed_live_only; frozen_archives_not_rewritten");
            }
            for (const auto &record : batch.records) {
                detail::Sha256 hash;
                hash.update(object(record.event));
                const auto fingerprint = hash.hexDigest();
                const Json value(db, object(record.event));
                const auto id = value.text("event_id", 512);
                Sql prior(
                    db,
                    "SELECT r.fingerprint,r.first_seen_at FROM events e JOIN "
                    "revisions r ON r.seq=e.revision_seq WHERE e.id=?1");
                prior.bind(1, id);
                const bool existing = prior.next();
                if (existing && prior.text(0) == fingerprint) {
                    ++batch.result.duplicates;
                    continue;
                }
                const auto firstSeen = existing ? prior.text(1) : now;
                auto payload = record.event;
                payload["first_seen_at_utc"] = quoteJson(firstSeen);
                payload["ingested_at_utc"] = quoteJson(now);
                // A capped acoustic window can remain incomplete indefinitely.
                // Arrival readiness is separate: after an initial snapshot,
                // only observations newer than the prior attempt can pulse.
                // Newly discovered older material remains non-live baseline.
                const bool liveArrival =
                    !existing && arrivalReady &&
                    (!regional || parseUtc(now) - arrivalCutoff <= 600000000LL) &&
                    parseUtc(value.text("observed_at_utc")) > arrivalCutoff;
                const auto kind = existing      ? "corrected"
                                  : liveArrival ? "new"
                                                : "baseline";
                Sql insert(
                    db,
                    "INSERT INTO "
                    "revisions(event_id,source_id,fingerprint,observed_at,"
                    "first_seen_at,ingested_at,change_kind,live_eligible,event_"
                    "json,raw_json) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
                insert.bind(1, id);
                insert.bind(2, eventSource);
                insert.bind(3, fingerprint);
                insert.bind(4, value.text("observed_at_utc"));
                insert.bind(5, firstSeen);
                insert.bind(6, now);
                insert.bind(7, kind);
                insert.bind(8, static_cast<std::int64_t>(liveArrival));
                insert.bind(9, object(payload));
                insert.bind(10, record.raw);
                insert.next();
                const auto sequence = sqlite3_last_insert_rowid(db);
                Sql current(db, "INSERT INTO events VALUES(?1,?2,?3,?4) ON "
                                "CONFLICT(id) DO UPDATE SET "
                                "observed_at=excluded.observed_at,revision_seq="
                                "excluded.revision_seq");
                current.bind(1, id);
                current.bind(2, eventSource);
                current.bind(3, value.text("observed_at_utc"));
                current.bind(4, sequence);
                current.next();
                if (existing)
                    ++batch.result.corrected;
                else
                    ++batch.result.inserted;
            }
            status["last_success_at_utc"] = quoteJson(now);
            status["consecutive_errors"] = "0";
            status["retry_delay_seconds"] = "0";
            status["last_error"] =
                !batch.revalidationError.empty() ? quoteJson("revalidation_" + batch.revalidationError) :
                batch.result.invalidRecords || batch.result.deferredFuture
                    ? quoteJson("invalid_or_future_records")
                : !batch.exhausted ? quoteJson("pagination_limit_reached")
                                   : "null";
            status["last_poll_complete"] =
                batch.result.complete ? "true" : "false";
            status["pagination_exhausted"] = batch.exhausted ? "true" : "false";
            status["baseline_complete"] =
                !baseline || batch.result.complete ? "true" : "false";
            const bool snapshotEstablished =
                !batch.records.empty() || batch.result.complete;
            const bool priorArrivalBaseline =
                !previous.null("arrival_baseline_established") &&
                previous.boolean("arrival_baseline_established");
            status["arrival_baseline_established"] =
                priorArrivalBaseline || snapshotEstablished ? "true" : "false";
            status["arrival_window_start_utc"] = quoteJson(formatUtc(arrivalCutoff));
            if (batch.result.complete)
                status["last_complete_at_utc"] = quoteJson(now);
            status["last_poll_inserted"] =
                std::to_string(batch.result.inserted);
            status["last_poll_corrected"] =
                std::to_string(batch.result.corrected);
            status["last_poll_duplicates"] =
                std::to_string(batch.result.duplicates);
            status["invalid_records"] =
                std::to_string(batch.result.invalidRecords);
            status["deferred_future_records"] =
                std::to_string(batch.result.deferredFuture);
            status["pages_fetched"] = std::to_string(batch.pages);
            status["possible_unrecoverable_gap"] = gap ? "true" : "false";
            status["completeness"] =
                quoteJson(birdSource
                              ? "rolling_window_best_effort; no publication "
                                "cursor; late uploads may be missed"
                              : source == "inaturalist" ? "bounded recent public reported sightings; omissions possible"
                              : "latest_position_snapshots_only; intermediate "
                                "fixes can be missed");
            status["freshness_claim"] =
                quoteJson("inspect latest_observation_at_utc");
            status["publication_lag_seconds"] = "null";
            if (birdSource) {
                status["region"] = quoteJson(regional ? region->id : "nz");
                status["observation_window_start_utc"] =
                    quoteJson(formatUtc(parseUtc(now) - 600000000LL));
                status["observation_window_end_utc"] = quoteJson(now);
            }
            if (regional) {
                std::string newest = previous.null("latest_observation_at_utc") ? "" :
                    previous.text("latest_observation_at_utc");
                for (const auto& record : batch.records)
                    newest = std::max(newest, Json(db, object(record.event)).text("observed_at_utc"));
                status["latest_observation_at_utc"] = newest.empty() ? "null" : quoteJson(newest);
                status["unique_events"] = std::to_string(batch.result.inserted +
                    (previous.null("unique_events") ? 0ULL : std::stoull(previous.numeric("unique_events"))));
            } else {
                Sql newest(db, "SELECT json_quote(max(observed_at)),count(*) FROM events WHERE source_id=?1");
                newest.bind(1, eventSource);
                newest.next();
                status["latest_observation_at_utc"] = newest.text(0);
                status["unique_events"] = std::to_string(newest.number(1));
            }
            impl_->saveStatus(source, status);
            Sql poll(
                db,
                "INSERT INTO polls(source_id,completed_at,complete,details) "
                "VALUES(?1,?2,?3,?4)");
            poll.bind(1, source);
            poll.bind(2, now);
            poll.bind(3, static_cast<std::int64_t>(batch.result.complete));
            poll.bind(4, object(status));
            poll.next();
            execute(db, "COMMIT");
        } catch (...) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
        if (!batch.records.empty() || batch.result.complete)
            impl_->establishedSources.insert(std::string(source));
        return batch.result;
    } catch (const CollectionFetchError &error) {
        // An explicit pause still counts as a request attempt for provider
        // pacing, but does not replace a genuine prior fault or add backoff.
        if (std::string_view(error.what()) != "cancelled")
            impl_->failure(source, now, error.what(),
                           error.retryAfterSeconds());
        throw;
    } catch (const std::invalid_argument &) {
        impl_->failure(source, now, "schema_or_pagination_error", 0);
        throw CollectionFetchError("schema_or_pagination_error");
    } catch (const std::exception &) {
        impl_->failure(source, now, "internal_or_storage_error", 0);
        throw CollectionFetchError("internal_or_storage_error");
    }
}
} // namespace atlas
