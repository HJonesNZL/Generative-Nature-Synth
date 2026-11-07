#include "sqlite_statement.hpp"
#include <algorithm>
#include <array>
#include <atlas/exports.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sqlite3.h>

namespace atlas {
namespace {
namespace fs = std::filesystem;
class InvalidWave : public std::runtime_error {
  public:
    InvalidWave() : std::runtime_error("Invalid managed WAV") {}
};
void require(bool value) {
    if (!value)
        throw InvalidWave();
}
bool lowerHex(std::string_view value, std::size_t size) {
    return value.size() == size &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool managed(std::string_view filename, std::string_view suffix) {
    return filename.size() == 32 + suffix.size() &&
           filename.substr(32) == suffix &&
           lowerHex(filename.substr(0, 32), 32);
}
std::uint16_t u16(const unsigned char *bytes) {
    return static_cast<std::uint16_t>(bytes[0] | static_cast<unsigned>(bytes[1])
                                                     << 8);
}
std::uint32_t u32(const unsigned char *bytes) {
    return static_cast<std::uint32_t>(u16(bytes)) |
           static_cast<std::uint32_t>(u16(bytes + 2)) << 16;
}
bool tag(const unsigned char *bytes, std::string_view expected) {
    return std::string_view(reinterpret_cast<const char *>(bytes),
                            expected.size()) == expected;
}
class JsonDatabase {
  public:
    JsonDatabase() {
        if (sqlite3_open_v2(":memory:", &db_,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                SQLITE_OPEN_NOMUTEX,
                            nullptr) != SQLITE_OK) {
            if (db_)
                sqlite3_close(db_);
            throw std::runtime_error("Export recipe validator unavailable");
        }
        sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, 32768);
        sqlite3_limit(db_, SQLITE_LIMIT_SQL_LENGTH, 32768);
    }
    ~JsonDatabase() { sqlite3_close(db_); }
    sqlite3 *get() const { return db_; }

  private:
    sqlite3 *db_{};
};
void recipe(ExportArtifact &artifact, std::string_view json, sqlite3 *db) {
    detail::Statement valid(db, "SELECT json_valid(?1),json_type(?1)");
    valid.bindJson(json);
    require(valid.next() && valid.integer(0) == 1 && valid.text(1) == "object");
    static const std::set<std::string> integerKeys{
        "recipe_version",   "scene_schema_revision", "synthesis_revision",
        "mapping_revision", "speed_numerator",       "speed_denominator",
        "sample_rate",      "channel_count",         "pcm_bits"};
    static const std::set<std::string> stringKeys{"engine",
                                                  "scene_hash",
                                                  "engine_source_sha256",
                                                  "time_basis",
                                                  "tail_policy",
                                                  "regional_tail_policy",
                                                  "authoritative_recipe",
                                                  "scene_name"};
    std::map<std::string, std::int64_t> numbers;
    std::map<std::string, std::string> strings;
    detail::Statement values(db, "SELECT key,type,atom FROM json_each(?1)");
    values.bindJson(json);
    while (values.next()) {
        const auto key = values.text(0), type = values.text(1);
        if (integerKeys.contains(key)) {
            require(type == "integer" &&
                    numbers.emplace(key, values.integer(2)).second);
        } else {
            require(stringKeys.contains(key) && type == "text");
            require(strings.emplace(key, values.text(2)).second);
        }
    }
    const auto number = [&](const std::string &key) {
        require(numbers.contains(key));
        return numbers.at(key);
    };
    const auto text = [&](const std::string &key) -> const std::string & {
        require(strings.contains(key));
        return strings.at(key);
    };
    require(numbers.size() == integerKeys.size());
    const auto version = number("recipe_version"),
               schema = number("scene_schema_revision");
    const auto synthesis = number("synthesis_revision"),
               mapping = number("mapping_revision");
    require(
        (version == 2 && ((schema == 1 && synthesis == 2 && mapping == 1) ||
                          (schema == 2 && synthesis == 3 && mapping == 2))) ||
        (version == 3 && schema == 3 && synthesis == 4 && mapping == 3));
    require(text("engine") == "atlas-additive" &&
            text("authoritative_recipe") == "self-contained-atlas-scene" &&
            text("tail_policy") == "editable_voice_maximum");
    require(text("time_basis") == "observed" ||
            text("time_basis") == "arrival");
    const auto numerator = number("speed_numerator"),
               denominator = number("speed_denominator");
    require(numerator > 0 && numerator <= 1000000 && denominator > 0 &&
            denominator <= 1000000 && numerator * 100 >= denominator &&
            numerator <= denominator * 10000);
    require(number("sample_rate") == artifact.sampleRate &&
            number("channel_count") == 2 && number("pcm_bits") == 24);
    artifact.sceneHash = text("scene_hash");
    artifact.engineSourceSha256 = text("engine_source_sha256");
    require(lowerHex(artifact.sceneHash, 64) &&
            lowerHex(artifact.engineSourceSha256, 64));
    if (version == 3)
        require(text("regional_tail_policy") ==
                "active_wet_12s_final_20ms_fade");
    else
        require(!strings.contains("regional_tail_policy"));
    artifact.sceneName = strings.contains("scene_name") ? text("scene_name")
                                                        : "Saved soundscape";
    require(!artifact.sceneName.empty() && artifact.sceneName.size() <= 512 &&
            artifact.sceneName.find('\0') == std::string::npos);
}
ExportArtifact inspect(const fs::path &path, sqlite3 *db) {
    ExportArtifact artifact;
    artifact.artifactId = path.stem().string();
    artifact.bytes = fs::file_size(path);
    require(artifact.bytes >= 102 && artifact.bytes <= 0xffffffffULL + 8);
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 80> header{};
    require(static_cast<bool>(
        input.read(reinterpret_cast<char *>(header.data()),
                   static_cast<std::streamsize>(header.size()))));
    const auto *h = header.data();
    require(tag(h, "RIFF") &&
            static_cast<std::uint64_t>(u32(h + 4)) + 8 == artifact.bytes &&
            tag(h + 8, "WAVEfmt ") && u32(h + 16) == 40 &&
            u16(h + 20) == 0xfffe && u16(h + 22) == 2);
    artifact.sampleRate = u32(h + 24);
    require(artifact.sampleRate >= 22050 && artifact.sampleRate <= 192000 &&
            u32(h + 28) == artifact.sampleRate * 6 && u16(h + 32) == 6 &&
            u16(h + 34) == 24 && u16(h + 36) == 22 && u16(h + 38) == 24 &&
            u32(h + 40) == 3);
    constexpr std::array<unsigned char, 16> pcmGuid{
        1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    require(std::equal(pcmGuid.begin(), pcmGuid.end(), h + 44));
    require(tag(h + 60, "fact") && u32(h + 64) == 4 && tag(h + 72, "data"));
    const auto dataBytes = u32(h + 76);
    artifact.frames = dataBytes / 6;
    require(dataBytes % 6 == 0 && u32(h + 68) == artifact.frames &&
            100ULL + dataBytes <= artifact.bytes);
    artifact.durationUs = artifact.frames * 1000000 / artifact.sampleRate;
    input.seekg(static_cast<std::streamoff>(80ULL + dataBytes));
    std::array<unsigned char, 20> list{};
    require(static_cast<bool>(
        input.read(reinterpret_cast<char *>(list.data()),
                   static_cast<std::streamsize>(list.size()))));
    const auto *l = list.data();
    const auto commentBytes = u32(l + 16);
    require(tag(l, "LIST") && tag(l + 8, "INFOICMT") && commentBytes >= 3 &&
            commentBytes <= 16385 &&
            u32(l + 4) == 12 + commentBytes + commentBytes % 2 &&
            100ULL + dataBytes + commentBytes + commentBytes % 2 ==
                artifact.bytes);
    std::string json(commentBytes, '\0');
    require(static_cast<bool>(input.read(
                json.data(), static_cast<std::streamsize>(json.size()))) &&
            json.back() == '\0');
    json.pop_back();
    require(json.find('\0') == std::string::npos);
    if (commentBytes % 2)
        require(input.get() == 0);
    require(input.peek() == std::char_traits<char>::eof());
    recipe(artifact, json, db);
    return artifact;
}
} // namespace
ExportHistory listExports(const fs::path &directory, std::size_t limit) {
    if (directory.empty() || limit == 0 || limit > 100)
        throw std::invalid_argument(
            "Invalid export inventory directory or limit");
    const auto status = fs::symlink_status(directory);
    if (status.type() == fs::file_type::not_found)
        return {};
    if (!fs::is_directory(status))
        throw std::invalid_argument(
            "Export inventory requires a directory, not a symlink");
    struct Candidate {
        fs::path path;
        fs::file_time_type modified;
    };
    ExportHistory history;
    std::vector<Candidate> candidates;
    std::size_t scanned{};
    auto iterator = fs::directory_iterator(directory);
    for (; iterator != fs::directory_iterator{} && scanned < 2048;
         ++iterator, ++scanned) {
        const auto filename = iterator->path().filename().string();
        const bool final = managed(filename, ".wav"),
                   staging = managed(filename, ".wav.rendering");
        if (!final && !staging)
            continue;
        std::error_code error;
        const auto itemStatus = iterator->symlink_status(error);
        if (error || (final && !fs::is_regular_file(itemStatus)) ||
            (staging && !fs::is_directory(itemStatus))) {
            ++history.unavailableFiles;
            continue;
        }
        if (staging) {
            ++history.incompleteRenders;
            continue;
        }
        const auto modified = iterator->last_write_time(error);
        if (error) {
            ++history.unavailableFiles;
            continue;
        }
        candidates.push_back({iterator->path(), modified});
    }
    history.limited = iterator != fs::directory_iterator{};
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) {
                  return a.modified != b.modified
                             ? a.modified > b.modified
                             : a.path.filename() < b.path.filename();
              });
    JsonDatabase json;
    for (const auto &candidate : candidates) {
        try {
            // Recheck the leaf after enumeration; a replaced symlink must not
            // become a completed export.
            if (!fs::is_regular_file(fs::symlink_status(candidate.path))) {
                ++history.unavailableFiles;
                continue;
            }
            auto artifact = inspect(candidate.path, json.get());
            if (history.items.size() < limit)
                history.items.push_back(std::move(artifact));
            else
                history.limited = true;
        } catch (const std::runtime_error &) {
            ++history.unavailableFiles;
        }
    }
    return history;
}
} // namespace atlas
