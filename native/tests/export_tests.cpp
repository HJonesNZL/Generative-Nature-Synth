#include <atlas/exports.hpp>
#include <atlas/wav.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace atlas;
namespace fs = std::filesystem;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Temporary {
    fs::path directory =
        fs::canonical(fs::temp_directory_path()) /
        ("atlas-exports-tests-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { fs::create_directory(directory); }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
};
std::string id(unsigned value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(32) << value;
    return out.str();
}
std::string read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output << bytes;
    check(static_cast<bool>(output), "Fixture write failed");
}
void put32(std::string &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
        bytes.at(offset + byte) =
            static_cast<char>((value >> (8 * byte)) & 255);
}
const std::string recipe =
    R"({"recipe_version":3,"engine":"atlas-additive","scene_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","scene_schema_revision":3,"synthesis_revision":4,"mapping_revision":3,"time_basis":"observed","speed_numerator":1,"speed_denominator":1,"sample_rate":48000,"engine_source_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","channel_count":2,"pcm_bits":24,"tail_policy":"editable_voice_maximum","regional_tail_policy":"active_wet_12s_final_20ms_fade","authoritative_recipe":"self-contained-atlas-scene"})";
void replace(std::string &value, std::string_view from, std::string_view to) {
    const auto pos = value.find(from);
    check(pos != std::string::npos, "Fixture replacement absent");
    value.replace(pos, from.size(), to);
}
void tests() {
    Temporary temporary;
    const auto directory = temporary.directory;
    Timeline timeline({}, TimeBasis::observed, {},
                      std::pair<Microseconds, Microseconds>{0, 10000});
    AudioSettings settings;
    settings.sampleRate = 48000;
    AudioPlan plan(timeline, settings);
    const auto first = directory / (id(1) + ".wav");
    renderWave(plan, first, recipe);
    const auto original = read(first);
    auto named = recipe;
    named.insert(named.size() - 1, ",\"scene_name\":\"Tūī evening\"");
    const auto second = directory / (id(2) + ".wav");
    renderWave(plan, second, named);
    fs::last_write_time(first, fs::file_time_type::clock::now() -
                                   std::chrono::seconds(2));
    fs::last_write_time(second, fs::file_time_type::clock::now());
    auto history = listExports(directory);
    check(history.items.size() == 2 && !history.limited &&
              history.unavailableFiles == 0 && history.incompleteRenders == 0,
          "Valid exports were not recovered");
    check(history.items[0].artifactId == id(2) &&
              history.items[0].sceneName == "Tūī evening" &&
              history.items[1].sceneName == "Saved soundscape",
          "Export sorting or scene name fallback is wrong");
    const auto &artifact = history.items[1];
    check(artifact.bytes == original.size() &&
              artifact.frames == plan.totalFrames() &&
              artifact.sampleRate == 48000 &&
              artifact.durationUs == plan.totalFrames() * 1000000 / 48000 &&
              artifact.sceneHash == std::string(64, 'a') &&
              artifact.engineSourceSha256 == std::string(64, 'b'),
          "Export metadata differs from completed render");
    check(listExports(directory).items[1].frames == artifact.frames &&
              read(first) == original,
          "Read-only restart inventory changed material");
    history = listExports(directory, 1);
    check(history.items.size() == 1 && history.limited &&
              history.items[0].artifactId == id(2),
          "Result cap did not retain latest export");
    write(directory / "unrelated.wav", "not managed");
    write(directory / "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.wav", "not managed");
    fs::create_directory(directory / (id(3) + ".wav.rendering"));
    write(directory / (id(3) + ".wav.rendering") / "audio.wav",
          "partial output");
    fs::create_symlink(first, directory / (id(4) + ".wav"));
    write(directory / (id(5) + ".wav.rendering"), "wrong staging type");
    history = listExports(directory);
    check(history.items.size() == 2 && history.incompleteRenders == 1 &&
              history.unavailableFiles == 2,
          "Staging or symlink appeared as completed artifact");
    // Structural checks deliberately do not claim an audio checksum.
    auto changedPcm = original;
    check(changedPcm.size() > 80, "No PCM in fixture");
    changedPcm[80] ^= 1;
    write(directory / (id(6) + ".wav"), changedPcm);
    check(listExports(directory).items.size() == 3,
          "Inventory claimed to hash PCM");
    const auto malformedDirectory = directory / "malformed";
    fs::create_directory(malformedDirectory);
    unsigned badCount{};
    auto malformed = [&](auto mutate) {
        auto content = original;
        mutate(content);
        write(malformedDirectory / (id(++badCount) + ".wav"), content);
    };
    malformed([](auto &content) { content[0] = 'X'; });
    malformed([](auto &content) { put32(content, 4, 8); });
    malformed([](auto &content) { content[20] = 1; });
    malformed([](auto &content) { content[22] = 1; });
    malformed([](auto &content) { put32(content, 24, 1); });
    malformed([](auto &content) { put32(content, 28, 1); });
    malformed([](auto &content) { content[34] = 16; });
    malformed([](auto &content) { content[44] = 3; });
    malformed([](auto &content) { put32(content, 68, 999); });
    malformed([](auto &content) { put32(content, 76, 0xffffffffU); });
    malformed([](auto &content) { content.resize(79); });
    malformed([](auto &content) { content.resize(content.size() - 1); });
    malformed([](auto &content) { content += 'x'; });
    malformed([](auto &content) {
        const auto offset = content.find("LIST");
        check(offset != std::string::npos, "LIST missing");
        content[offset] = 'X';
    });
    malformed([](auto &content) {
        const auto offset = content.find("ICMT");
        check(offset != std::string::npos, "ICMT missing");
        put32(content, offset + 4, 0xffffffffU);
    });
    auto malformedRecipe = [&](auto mutate) {
        auto json = recipe;
        mutate(json);
        renderWave(plan, malformedDirectory / (id(++badCount) + ".wav"), json);
    };
    malformedRecipe([](auto &json) { json = "[1,2,3]"; });
    malformedRecipe([](auto &json) { json = "{broken"; });
    malformedRecipe([](auto &json) {
        replace(json, "\"recipe_version\":3", "\"recipe_version\":99");
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"sample_rate\":48000", "\"sample_rate\":44100");
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"channel_count\":2", "\"channel_count\":1");
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"speed_denominator\":1", "\"speed_denominator\":0");
    });
    malformedRecipe([](auto &json) {
        replace(json, std::string(64, 'b'), std::string(64, 'B'));
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"scene_schema_revision\":3",
                "\"scene_schema_revision\":2");
    });
    malformedRecipe([](auto &json) {
        json.insert(json.size() - 1, ",\"recipe_version\":3");
    });
    malformedRecipe([](auto &json) {
        json.insert(json.size() - 1, ",\"unrecognized\":true");
    });
    malformedRecipe([](auto &json) {
        json.insert(json.size() - 1, ",\"scene_name\":null");
    });
    malformedRecipe([](auto &json) {
        json.insert(json.size() - 1, ",\"scene_name\":\"\"");
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"pcm_bits\":24", "\"pcm_bits\":24.0");
    });
    malformedRecipe([](auto &json) {
        replace(json, "\"engine\":\"atlas-additive\"",
                "\"engine\":\"unrelated\"");
    });
    history = listExports(malformedDirectory);
    check(history.items.empty() && history.unavailableFiles == badCount &&
              !history.limited,
          "Malformed WAV or recipe accepted as completed export");
    const auto legacyDirectory = directory / "legacy";
    fs::create_directory(legacyDirectory);
    auto legacy = recipe;
    replace(legacy, "\"recipe_version\":3", "\"recipe_version\":2");
    replace(legacy, "\"scene_schema_revision\":3",
            "\"scene_schema_revision\":2");
    replace(legacy, "\"synthesis_revision\":4", "\"synthesis_revision\":3");
    replace(legacy, "\"mapping_revision\":3", "\"mapping_revision\":2");
    replace(legacy,
            ",\"regional_tail_policy\":\"active_wet_12s_final_20ms_fade\"", "");
    renderWave(plan, legacyDirectory / (id(1) + ".wav"), legacy);
    check(listExports(legacyDirectory).items.size() == 1,
          "Supported older soundscape export omitted");
    const auto scanDirectory = directory / "bounded";
    fs::create_directory(scanDirectory);
    for (unsigned i = 0; i < 2050; ++i)
        write(scanDirectory / ("unrelated-" + std::to_string(i)), "");
    check(listExports(scanDirectory).limited,
          "Directory scan exceeded bounded policy without flag");
    check(listExports(directory / "missing").items.empty(),
          "Fresh missing export directory is not empty");
    fs::create_directory_symlink(directory, directory / "linked");
    try {
        (void)listExports(directory / "linked");
        throw std::runtime_error("Symlink inventory directory accepted");
    } catch (const std::invalid_argument &) {
    }
    try {
        (void)listExports(directory, 0);
        throw std::runtime_error("Zero inventory limit accepted");
    } catch (const std::invalid_argument &) {
    }
    try {
        (void)listExports(directory, 101);
        throw std::runtime_error("Oversized inventory limit accepted");
    } catch (const std::invalid_argument &) {
    }
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "Export inventory tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
