// Isolated CSV contract preflight, not a production event importer.
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <locale>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t maxBytes = 4 * 1024 * 1024;
constexpr std::size_t maxRows = 10000;
constexpr std::size_t maxColumns = 64;
constexpr std::size_t maxCellBytes = 4096;
using Row = std::vector<std::string>;
using Table = std::vector<Row>;

bool utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            if (lead == 0 || (lead < 0x20 && lead != '\n' && lead != '\r' && lead != '\t')) return false;
            continue;
        }
        unsigned value{};
        unsigned count{};
        unsigned minimum{};
        if (lead >= 0xc2 && lead <= 0xdf) { value = lead & 31U; count = 1; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { value = lead & 15U; count = 2; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { value = lead & 7U; count = 3; minimum = 0x10000; }
        else return false;
        for (unsigned j = 0; j < count; ++j) {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0U) != 0x80U) return false;
            value = (value << 6U) | (next & 63U);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

Table readCsv(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) throw std::runtime_error("input_unavailable");
    std::string bytes;
    char character{};
    while (input.get(character)) {
        if (bytes.size() == maxBytes) throw std::runtime_error("file_capacity");
        bytes.push_back(character);
    }
    if (!input.eof()) throw std::runtime_error("input_read_error");
    if (!utf8(bytes)) throw std::runtime_error("invalid_utf8_or_control");
    Table table;
    Row row;
    std::string cell;
    bool quoted = false;
    bool closed = false;
    bool pending = false;
    const auto field = [&] {
        if (row.size() == maxColumns) throw std::runtime_error("column_capacity");
        row.push_back(cell);
        cell.clear();
        closed = false;
    };
    const auto record = [&] {
        field();
        if (table.size() == maxRows + 1) throw std::runtime_error("row_capacity");
        table.push_back(row);
        row.clear();
        pending = false;
    };
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        pending = true;
        if (quoted) {
            if (c == '"') {
                if (i + 1 < bytes.size() && bytes[i + 1] == '"') { cell.push_back('"'); ++i; }
                else { quoted = false; closed = true; }
            } else cell.push_back(c);
        } else if (c == ',') field();
        else if (c == '\n' || c == '\r') {
            if (c == '\r') {
                if (i + 1 == bytes.size() || bytes[i + 1] != '\n') throw std::runtime_error("bare_record_cr");
                ++i;
            }
            record();
        } else if (c == '"' && cell.empty() && !closed) quoted = true;
        else {
            if (c == '"' || closed) throw std::runtime_error("invalid_csv_quote");
            cell.push_back(c);
        }
        if (cell.size() > maxCellBytes) throw std::runtime_error("cell_capacity");
    }
    if (quoted) throw std::runtime_error("unterminated_csv_quote");
    if (pending) record();
    if (table.empty()) throw std::runtime_error("empty_csv");
    std::set<std::string> headers;
    for (const auto& header : table.front()) {
        if (header.empty() || !headers.insert(header).second) throw std::runtime_error("invalid_headers");
    }
    for (const auto& values : table) {
        if (values.size() != table.front().size()) throw std::runtime_error("row_width");
    }
    return table;
}

std::map<std::string, std::size_t> columns(const Row& header, const Row& required) {
    std::map<std::string, std::size_t> result;
    for (std::size_t i = 0; i < header.size(); ++i) result.emplace(header[i], i);
    for (const auto& name : required) {
        if (!result.contains(name)) throw std::runtime_error("missing_required_column");
    }
    return result;
}

bool timestamp(std::string_view value) {
    if (value.size() != 23) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char expected = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : i == 19 ? '.' : '\0';
        if (expected ? value[i] != expected : value[i] < '0' || value[i] > '9') return false;
    }
    const auto number = [&](std::size_t at, std::size_t count) {
        int result{};
        const auto part = value.substr(at, count);
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result);
        return parsed.ec == std::errc{} ? result : -1;
    };
    const int year = number(0, 4);
    const std::chrono::year_month_day day{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(number(5, 2))}, std::chrono::day{static_cast<unsigned>(number(8, 2))}};
    return year >= 1 && day.ok() && number(11, 2) < 24 && number(14, 2) < 60 && number(17, 2) < 60;
}

bool coordinate(std::string_view value, double bound) {
    double number{};
    std::istringstream input{std::string(value)};
    input.imbue(std::locale::classic());
    input >> std::noskipws >> number;
    return !value.empty() && !input.fail() && input.eof() && std::isfinite(number) && std::abs(number) <= bound;
}

std::string json(std::string_view value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result + '"';
}

void preflight(const Table& events, const Table& package) {
    const Row metadataNames{"package-doi", "study-name", "license-uri", "citation", "coordinate-units", "download-access", "privacy-review"};
    const auto meta = columns(package.front(), metadataNames);
    if (package.size() != 2 || package.front().size() != metadataNames.size()) throw std::runtime_error("one_package_declaration_required");
    const auto metadata = [&](const std::string& name) -> const std::string& { return package[1][meta.at(name)]; };
    const Row required{"event-id", "timestamp", "location-long", "location-lat", "individual-local-identifier", "tag-local-identifier", "sensor-type", "study-name", "visible", "manually-marked-outlier"};
    const auto index = columns(events.front(), required);
    std::map<std::string, std::size_t> idCounts;
    for (std::size_t r = 1; r < events.size(); ++r) ++idCounts[events[r][index.at("event-id")]];
    std::vector<std::size_t> candidates;
    std::vector<std::pair<std::size_t, Row>> exclusions;
    for (std::size_t r = 1; r < events.size(); ++r) {
        const auto& row = events[r];
        const auto get = [&](const std::string& name) -> const std::string& { return row[index.at(name)]; };
        Row reasons;
        const auto reject = [&](bool condition, const char* reason) { if (condition) reasons.emplace_back(reason); };
        reject(metadata("package-doi").empty() || metadata("license-uri").empty() || metadata("citation").empty(), "missing_package_provenance");
        reject(metadata("download-access") != "public-download", "private_or_unverified_download");
        reject(metadata("privacy-review") != "public-exact-reviewed", "privacy_not_reviewed_or_generalized");
        reject(metadata("coordinate-units") != "decimal-degrees-WGS84", "unverified_coordinate_units");
        reject(get("study-name").empty() || get("study-name") != metadata("study-name"), "study_mismatch");
        reject(get("event-id").empty() || get("individual-local-identifier").empty() || get("tag-local-identifier").empty(), "missing_identity");
        if (!get("event-id").empty()) reject(idCounts.at(get("event-id")) > 1, "duplicate_event_id");
        reject(!timestamp(get("timestamp")), "timestamp_not_exact_utc_millisecond_profile");
        reject(!coordinate(get("location-long"), 180) || !coordinate(get("location-lat"), 90), "invalid_coordinates");
        reject(get("sensor-type") != "gps", "unsupported_sensor_semantics");
        reject(get("visible") != "true" && get("visible") != "TRUE", "invisible_or_unknown_visibility");
        reject(get("manually-marked-outlier") != "false" && get("manually-marked-outlier") != "FALSE", "outlier_or_unknown_flag");
        if (reasons.empty()) candidates.push_back(r);
        else exclusions.emplace_back(r, reasons);
    }
    std::cout << "{\"profile\":\"movebank-csv-preflight-v1\",\"import_allowed\":false,\"blockers\":[\"per-study-rights-review-required\",\"deployment-reference-join-not-implemented\",\"canonical-normalization-not-implemented\"],\"declarations_unverified\":{";
    bool first = true;
    for (const auto& name : metadataNames) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << json(name) << ':' << json(metadata(name));
    }
    std::cout << "},\"input_rows\":" << events.size() - 1 << ",\"technical_candidate_rows\":[";
    for (std::size_t i = 0; i < candidates.size(); ++i) { if (i) std::cout << ','; std::cout << candidates[i]; }
    std::cout << "],\"exclusions\":[";
    for (std::size_t i = 0; i < exclusions.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"record\":" << exclusions[i].first << ",\"reasons\":[";
        for (std::size_t j = 0; j < exclusions[i].second.size(); ++j) { if (j) std::cout << ','; std::cout << json(exclusions[i].second[j]); }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage_preflight_EVENTS_csv_PACKAGE_csv");
        const auto events = readCsv(argv[1]);
        const auto package = readCsv(argv[2]);
        preflight(events, package);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
