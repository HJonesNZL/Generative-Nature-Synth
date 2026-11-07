#include <atlas/inaturalist.hpp>
#include <atlas/core.hpp>
#include "event_validation.hpp"
#include "json_text.hpp"
#include <sqlite3.h>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace atlas {
namespace {
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
struct Query {
    sqlite3_stmt* value{};
    Query(sqlite3* db, const char* sql, std::string_view json) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
            throw std::invalid_argument("inaturalist schema error");
        if (sqlite3_bind_text(value, 1, json.data(), static_cast<int>(json.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
            sqlite3_finalize(value);
            throw std::invalid_argument("inaturalist input error");
        }
    }
    ~Query() { sqlite3_finalize(value); }
    bool next() {
        const auto code = sqlite3_step(value);
        if (code == SQLITE_ROW) return true;
        if (code == SQLITE_DONE) return false;
        throw std::invalid_argument("inaturalist malformed JSON");
    }
    std::string text(int column) const {
        const auto* bytes = sqlite3_column_text(value, column);
        return bytes ? std::string(reinterpret_cast<const char*>(bytes),
                                  static_cast<std::size_t>(sqlite3_column_bytes(value, column))) : "";
    }
};
std::string encode(std::string_view text) {
    std::string result;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const char byte : text) {
        const auto c = static_cast<unsigned char>(byte);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
}
std::string inaturalistRequestUrl(std::string_view startUtc, std::string_view endUtc, unsigned page) {
    const auto start = parseUtc(startUtc), end = parseUtc(endUtc);
    if (start >= end || end - start > 7LL * 86400 * 1000000 || page < 1 || page > 50)
        throw std::invalid_argument("inaturalist request outside bounded window");
    return "https://api.inaturalist.org/v1/observations?taxon_id=1&license=cc0,cc-by&geo=true&geoprivacy=open&taxon_geoprivacy=open&per_page=30&order=desc&order_by=updated_at&d1=" +
        encode(startUtc) + "&d2=" + encode(endUtc) + "&page=" + std::to_string(page);
}
std::string inaturalistRevalidationUrl(const std::vector<std::string>& ids) {
    if (ids.empty() || ids.size() > 30) throw std::invalid_argument("inaturalist revalidation capacity");
    std::string joined;
    for (const auto& id : ids) {
        if (id.empty() || id.size() > 19 || id.front() == '0' ||
            id.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("inaturalist observation ID invalid");
        if (!joined.empty()) joined += ',';
        joined += id;
    }
    return "https://api.inaturalist.org/v1/observations?id=" + joined +
        "&taxon_id=1&license=cc0,cc-by&geo=true&geoprivacy=open&taxon_geoprivacy=open&per_page=30&page=1";
}
InaturalistPage parseInaturalistResponse(std::string_view json, std::string_view nowUtc) {
    (void)parseUtc(nowUtc);
    if (json.size() > 4U * 1024U * 1024U) throw std::invalid_argument("inaturalist response exceeds capacity");
    sqlite3* raw{};
    if (sqlite3_open(":memory:", &raw) != SQLITE_OK) {
        if (raw) sqlite3_close(raw);
        throw std::runtime_error("inaturalist parser unavailable");
    }
    Database db(raw, &sqlite3_close);
    Query root(db.get(), "SELECT json_type(?1),json_type(?1,'$.results'),json_extract(?1,'$.total_results'),json_extract(?1,'$.page'),json_extract(?1,'$.per_page'),json_array_length(?1,'$.results'),json_type(?1,'$.total_results'),json_type(?1,'$.page'),json_type(?1,'$.per_page')", json);
    if (!root.next() || root.text(0) != "object" || root.text(1) != "array" || root.text(6) != "integer" || root.text(7) != "integer" || root.text(8) != "integer")
        throw std::invalid_argument("inaturalist response schema error");
    const auto total = sqlite3_column_int64(root.value,2), page = sqlite3_column_int64(root.value,3), perPage = sqlite3_column_int64(root.value,4), count = sqlite3_column_int64(root.value,5);
    if (total < 0 || page < 1 || page > 50 || perPage < 1 || perPage > 200 || count > perPage || count > total)
        throw std::invalid_argument("inaturalist pagination exceeds capacity");
    Query duplicates(db.get(), "SELECT fullkey FROM json_tree(?1) GROUP BY fullkey HAVING count(*) > 1 LIMIT 1", json);
    if (duplicates.next()) throw std::invalid_argument("duplicate inaturalist JSON field");
    InaturalistPage result{{},{},{},0,static_cast<std::size_t>(total),static_cast<std::size_t>(page),static_cast<std::size_t>(perPage),static_cast<std::size_t>(count)};
    Query rows(db.get(), "SELECT value,type FROM json_each(?1,'$.results')", json);
    while (rows.next()) {
        try {
            if (rows.text(1) != "object") throw std::invalid_argument("invalid sighting");
            const auto row = rows.text(0);
            Query fields(db.get(), R"SQL(SELECT
                json_extract(?1,'$.id'),json_extract(?1,'$.time_observed_at'),
                json_extract(?1,'$.license_code'),json_extract(?1,'$.geoprivacy'),
                json_extract(?1,'$.taxon_geoprivacy'),json_extract(?1,'$.obscured'),
                json_extract(?1,'$.geojson.coordinates[1]'),json_extract(?1,'$.geojson.coordinates[0]'),
                json_extract(?1,'$.taxon.id'),json_extract(?1,'$.taxon.name'),
                json_extract(?1,'$.user.login'),json_extract(?1,'$.positional_accuracy'),
                json_type(?1,'$.id'),json_type(?1,'$.taxon.id'),json_type(?1,'$.obscured'),
                json_type(?1,'$.geojson.coordinates[1]'),json_type(?1,'$.geojson.coordinates[0]'),json_type(?1,'$.positional_accuracy'),json_type(?1,'$.time_observed_at'),json_type(?1,'$.taxon.name'),json_type(?1,'$.user.login'),json_extract(?1,'$.created_at'),json_extract(?1,'$.updated_at'),json_type(?1,'$.created_at'),json_type(?1,'$.updated_at'),json_type(?1,'$.license_code'),json_type(?1,'$.geoprivacy'),json_type(?1,'$.taxon_geoprivacy')
                )SQL", row);
            (void)fields.next();
            const auto license = fields.text(2);
            if (fields.text(12) != "integer" || sqlite3_column_int64(fields.value,0) <= 0)
                throw std::invalid_argument("invalid observation identity");
            const auto privacyValid = [&](int valueColumn, int typeColumn) {
                const auto type = fields.text(typeColumn), value = fields.text(valueColumn);
                return type == "null" || (type == "text" &&
                    (value == "open" || value == "obscured" || value == "private"));
            };
            const auto licenseType = fields.text(25);
            const bool recognizedLicense = licenseType == "null" || (licenseType == "text" &&
                (license == "cc0" || license == "cc-by" || license == "cc-by-sa" ||
                 license == "cc-by-nc" || license == "cc-by-nd" || license == "cc-by-nc-sa" || license == "cc-by-nc-nd"));
            if (!recognizedLicense || !privacyValid(3,26) || !privacyValid(4,27) ||
                (fields.text(14) != "false" && fields.text(14) != "true"))
                throw std::invalid_argument("malformed observation permissions");
            if ((license != "cc0" && license != "cc-by") || fields.text(14) == "true" ||
                fields.text(3) == "obscured" || fields.text(3) == "private" ||
                fields.text(4) == "obscured" || fields.text(4) == "private") {
                // Only identity/permission metadata survives: never retain private coordinates.
                result.explicitlyRevokedIds.push_back(fields.text(0));
                continue;
            }
            if (fields.text(13) != "integer" || sqlite3_column_int64(fields.value,8) <= 0)
                throw std::invalid_argument("invalid taxon identity");
            for (const auto col : {15,16})
                if (fields.text(col) != "real" && fields.text(col) != "integer") throw std::invalid_argument("missing coordinates");
            for (const auto col : {18,19,20,23,24})
                if (fields.text(col) != "text") throw std::invalid_argument("invalid sighting text");
            if (!fields.text(17).empty() && fields.text(17) != "null" && fields.text(17) != "integer" && fields.text(17) != "real")
                throw std::invalid_argument("invalid uncertainty");
            const auto observed = fields.text(1);
            if (parseUtc(observed) > parseUtc(nowUtc)) throw std::invalid_argument("future sighting");
            const auto created = fields.text(21), updated = fields.text(22);
            if (parseUtc(created) > parseUtc(nowUtc) || parseUtc(updated) > parseUtc(nowUtc) ||
                parseUtc(updated) < parseUtc(created)) throw std::invalid_argument("invalid publication timestamps");
            const auto id = fields.text(0), name = fields.text(9), observer = fields.text(10);
            if (name.empty() || observer.empty() || name.size() > 512 || observer.size() > 512)
                throw std::invalid_argument("missing attribution or taxon");
            const auto q = detail::quoteJson;
            const auto latitude = fields.text(6), longitude = fields.text(7);
            const auto uncertainty = fields.text(11).empty() ? "null" : fields.text(11);
            auto normalized = "{\"schema_version\":1,\"event_id\":" + q("inaturalist:" + id) +
                ",\"source_id\":\"inaturalist\",\"provider_event_id\":" + q(id) +
                ",\"observed_at_utc\":" + q(formatUtc(parseUtc(observed))) +
                ",\"provider_published_at_utc\":" + q(formatUtc(parseUtc(created))) +
                ",\"provider_record_revision\":" + q(formatUtc(parseUtc(updated))) +
                ",\"event_kind\":\"human_observation\",\"location_kind\":\"reported_location\",\"latitude\":" + latitude +
                ",\"longitude\":" + longitude + ",\"uncertainty_m\":" + uncertainty +
                ",\"coordinate_order\":\"latitude,longitude\",\"species_id\":" + q("inaturalist:taxon:" + fields.text(8)) +
                ",\"scientific_name\":" + q(name) +
                ",\"provenance_mode\":\"live_source\",\"permissions_status\":" + q("observation_" + license + "_at_fetch; media_not_imported") +
                ",\"attribution\":" + q("iNaturalist observer " + observer + "; " + license) +
                ",\"source_url\":" + q("https://www.inaturalist.org/observations/" + id) +
                ",\"quality_flags\":[\"reported_sighting_not_individual_track\",\"community_identification_may_change\",\"privacy_and_license_revalidation_best_effort\"],\"first_seen_at_utc\":" + q(nowUtc) +
                ",\"ingested_at_utc\":" + q(nowUtc) + "}";
            detail::validateStoredEvent(db.get(), normalized);
            result.originalTimestampEvidence.emplace("inaturalist:" + id,
                "{\"time_observed_at\":" + q(observed) + ",\"created_at\":" + q(created) +
                ",\"updated_at\":" + q(updated) + "}");
            result.normalizedRecords.push_back(std::move(normalized));
        } catch (const std::invalid_argument&) { ++result.invalidRecords; }
    }
    return result;
}
}
