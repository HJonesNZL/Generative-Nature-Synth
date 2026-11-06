#include "event_validation.hpp"
#include "sqlite_statement.hpp"
#include <atlas/core.hpp>
#include <cmath>
#include <map>
#include <set>

namespace atlas::detail {
namespace {
void text(std::string_view value, std::size_t maximum = 2048) {
    if (value.empty() || value.size() > maximum || value.find('\0') != std::string_view::npos)
        throw std::invalid_argument("invalid event text length or NUL");
    // SQLite decodes JSON escapes; reject invalid UTF-8 including unpaired surrogates.
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80) continue;
        unsigned count{}, code{}, minimum{};
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1fU; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0fU; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 0x07U; minimum = 0x10000; }
        else throw std::invalid_argument("invalid UTF-8 event text");
        for (unsigned n = 0; n < count; ++n) {
            if (i == value.size()) throw std::invalid_argument("incomplete UTF-8 event text");
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0U) != 0x80U) throw std::invalid_argument("invalid UTF-8 continuation");
            code = (code << 6U) | (next & 0x3fU);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            throw std::invalid_argument("invalid Unicode scalar");
    }
}
void number(const Statement& query, int typeColumn, int valueColumn) {
    const auto type = query.text(typeColumn);
    if ((type != "integer" && type != "real") || !std::isfinite(query.number(valueColumn)))
        throw std::invalid_argument("invalid numeric event field");
}
bool contains(std::string_view words, std::string_view word) {
    if (word.empty() || word.find('|') != std::string_view::npos) return false;
    return words.find("|" + std::string(word) + "|") != std::string_view::npos;
}
} // namespace

void validateStoredEvent(sqlite3* db, std::string_view json) {
    if (json.size() > 16384) throw std::invalid_argument("event exceeds capacity");
    constexpr auto required = "|schema_version|event_id|source_id|provider_event_id|observed_at_utc|event_kind|location_kind|latitude|longitude|coordinate_order|provenance_mode|first_seen_at_utc|ingested_at_utc|";
    constexpr auto strings = "|event_id|source_id|provider_event_id|provider_record_revision|observed_at_utc|provider_published_at_utc|event_kind|location_kind|coordinate_order|species_id|scientific_name|common_name|individual_id|deployment_id|station_id|confidence_meaning|provenance_mode|permissions_status|attribution|source_url|provider_subject_id|individual_display_name|first_seen_at_utc|ingested_at_utc|";
    Statement root(db, "SELECT json_type(?1)");
    root.bindJson(json);
    if (!root.next() || root.text(0) != "object") throw std::invalid_argument("event must be an object");
    Statement fields(db, "SELECT key,type,atom,value FROM json_each(?1)");
    fields.bindJson(json);
    std::set<std::string> seen;
    std::map<std::string, std::string> values;
    std::map<std::string, double> numbers;
    std::size_t requiredCount{};
    while (fields.next()) {
        const auto key = fields.text(0), type = fields.text(1);
        if (!seen.insert(key).second) throw std::invalid_argument("duplicate event field");
        const bool needed = contains(required, key);
        if (needed) ++requiredCount;
        if (contains(strings, key)) {
            if (type == "null" && !needed) continue;
            if (type != "text") throw std::invalid_argument("invalid event text type");
            auto value = fields.text(2);
            text(value, key.ends_with("_id") ? 512 : 2048);
            if (key.ends_with("_at_utc")) (void)parseUtc(value);
            values.emplace(key, std::move(value));
        } else if (key == "schema_version") {
            if (type != "integer" || fields.integer(2) != 1) throw std::invalid_argument("unsupported event schema");
        } else if (contains("|latitude|longitude|uncertainty_m|confidence|", key)) {
            if (type == "null") continue;
            number(fields, 1, 2);
            numbers.emplace(key, fields.number(2));
        } else if (key == "quality_flags" || key == "provider_scores") {
            const bool flags = key == "quality_flags";
            if (type != (flags ? "array" : "object")) throw std::invalid_argument("invalid event collection");
            Statement items(db, "SELECT key,type,atom FROM json_each(?1)");
            items.bindJson(fields.text(3));
            std::set<std::string> scoreKeys;
            std::size_t count{};
            while (items.next()) {
                if (++count > (flags ? 64U : 32U)) throw std::invalid_argument("event collection exceeds capacity");
                if (flags) {
                    if (items.text(1) != "text") throw std::invalid_argument("quality flag must be text");
                    text(items.text(2), 128);
                } else {
                    const auto scoreKey = items.text(0);
                    text(scoreKey, 128);
                    if (!scoreKeys.insert(scoreKey).second) throw std::invalid_argument("duplicate provider score");
                    if (items.text(1) != "null") number(items, 1, 2);
                }
            }
        } else throw std::invalid_argument("unknown event field");
    }
    if (requiredCount != 13) throw std::invalid_argument("missing event fields");
    const auto& source = values.at("source_id");
    if (source.size() > 128 || source.find(':') != std::string::npos ||
        values.at("event_id") != source + ":" + values.at("provider_event_id"))
        throw std::invalid_argument("event identity mismatch");
    for (const auto* key : {"species_id", "individual_id", "deployment_id", "station_id", "provider_subject_id"}) {
        const auto found = values.find(key);
        if (found == values.end()) continue;
        const auto& value = found->second;
        if (value.front() == ':' || value.back() == ':' || value.find(':') == std::string::npos || value.find("::") != std::string::npos)
            throw std::invalid_argument("identity requires a namespace");
    }
    const auto& kind = values.at("event_kind");
    const auto& location = values.at("location_kind");
    const bool pairing = ((kind == "gps_fix" || kind == "satellite_fix") && location == "individual_fix") ||
        (kind == "acoustic_receiver_detection" && location == "receiver_detection") ||
        (kind == "acoustic_species_detection" && location == "station_detection") ||
        (kind == "human_observation" && location == "reported_location");
    if (!pairing) throw std::invalid_argument("invalid event kind/location pairing");
    if (kind == "acoustic_species_detection" && (values.contains("individual_id") || values.contains("deployment_id")))
        throw std::invalid_argument("station detection cannot identify individual or deployment");
    if (values.at("coordinate_order") != "latitude,longitude" ||
        !contains("|live_source|imported|demo|", values.at("provenance_mode")))
        throw std::invalid_argument("invalid coordinate order or provenance");
    if (numbers.contains("latitude") != numbers.contains("longitude")) throw std::invalid_argument("incomplete coordinates");
    if (numbers.contains("latitude") && (std::abs(numbers.at("latitude")) > 90 || std::abs(numbers.at("longitude")) > 180))
        throw std::invalid_argument("coordinates outside range");
    if (numbers.contains("uncertainty_m") && numbers.at("uncertainty_m") < 0) throw std::invalid_argument("negative uncertainty");
    if (numbers.contains("confidence") && !values.contains("confidence_meaning")) throw std::invalid_argument("confidence requires meaning");
}
} // namespace atlas::detail
