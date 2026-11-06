#include <atlas/storage.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/wav.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <locale>
#include "json_text.hpp"
#include "atlas_build_identity.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>

using atlas::detail::quoteJson;

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("usage: atlas-native sessions|replay|render|ui-host --db PATH [--session latest --mode observed|arrival --speed 60 --limit 5]");
        if (std::string_view(argv[1]) == "version") {
            if (argc != 2) throw std::invalid_argument("version takes no options");
            std::cout << "{\"engine\":\"animal-atlas-native\",\"source_sha256\":" << quoteJson(atlas::nativeSourceSha256) << "}\n";
            return 0;
        }
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc || !std::string_view(argv[i]).starts_with("--")) throw std::invalid_argument("options require values");
            if (!options.emplace(argv[i], argv[i + 1]).second) throw std::invalid_argument("duplicate option");
        }
        for (const auto& [key, value] : options) {
            (void)value;
            if (key != "--db" && key != "--session" && key != "--mode" && key != "--speed" && key != "--limit" && key != "--output" && key != "--sample-rate" && key != "--block-frames" &&
                key != "--root-midi" && key != "--attack-ms" && key != "--decay-ms" && key != "--gain-db")
                throw std::invalid_argument("unknown option");
        }
        const auto get = [&](const std::string& key, const std::string& fallback) { const auto found = options.find(key); return found == options.end() ? fallback : found->second; };
        if (std::string_view(argv[1]) == "ui-host") {
            atlas::UiHost host(get("--db", ".state/atlas.sqlite3"));
            std::string line;
            line.reserve(16385);
            char c{};
            while (std::cin.get(c)) {
                if (c=='\n') {
                    std::cout << host.request(line) << '\n' << std::flush;
                    line.clear();
                } else if (line.size()<16385) line+=c;
            }
            return 0;
        }
        atlas::ReadOnlyLibrary library(get("--db", ".state/atlas.sqlite3"));
        if (std::string_view(argv[1]) == "sessions") {
            std::cout << "[";
            bool first = true;
            for (const auto& session : library.sessions()) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quoteJson(session.id) << ",\"name\":" << quoteJson(session.name)
                          << ",\"state\":" << quoteJson(session.state) << ",\"event_count\":" << session.eventCount
                          << ",\"capture_truncated\":" << (session.truncated ? "true" : "false") << '}';
            }
            std::cout << "]\n";
        } else if (std::string_view(argv[1]) == "replay" || std::string_view(argv[1]) == "render") {
            const auto mode = get("--mode", "observed");
            if (mode != "observed" && mode != "arrival") throw std::invalid_argument("invalid time basis");
            const auto speed = atlas::Speed::parse(get("--speed", "1"));
            atlas::SavedSession saved;
            const auto timeline = library.load(get("--session", "latest"), mode == "observed" ? atlas::TimeBasis::observed : atlas::TimeBasis::arrival, speed, &saved);
            if (std::string_view(argv[1]) == "render") {
                const auto number = [&](const std::string& key, const std::string& fallback) {
                    const auto text = get(key, fallback);
                    double value{};
                    std::istringstream input(text);
                    input.imbue(std::locale::classic());
                    if (!(input >> std::noskipws >> value) || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value))
                        throw std::invalid_argument("invalid finite numeric render option");
                    return value;
                };
                const auto integerOption = [&](const std::string& key, const std::string& fallback, double maximum) {
                    const auto value = number(key, fallback);
                    if (value < 0 || value > maximum || std::floor(value) != value) throw std::invalid_argument("invalid integer render option");
                    return static_cast<unsigned>(value);
                };
                atlas::AudioSettings settings;
                settings.sampleRate = integerOption("--sample-rate", "48000", 192000);
                settings.rootMidi = integerOption("--root-midi", "60", 72);
                settings.attackSeconds = number("--attack-ms", "8") / 1000;
                settings.decaySeconds = number("--decay-ms", "650") / 1000;
                settings.masterDb = number("--gain-db", "0");
                const auto blockFrames = integerOption("--block-frames", "512", 4096);
                const atlas::AudioPlan plan(timeline, settings);
                std::ostringstream recipe;
                recipe << std::setprecision(17) << "{\"recipe_version\":1,\"engine\":\"atlas-struck-tone-v1\",\"session_id\":" << quoteJson(saved.id)
                       << ",\"engine_source_sha256\":" << quoteJson(atlas::nativeSourceSha256)
                       << ",\"session_name\":" << quoteJson(saved.name) << ",\"source_content_sha256\":" << quoteJson(saved.contentHash)
                       << ",\"metadata_integrity\":" << quoteJson(saved.metadataIntegrity) << ",\"mode\":" << quoteJson(mode)
                       << ",\"speed_numerator\":" << quoteJson(std::to_string(speed.numerator)) << ",\"speed_denominator\":" << quoteJson(std::to_string(speed.denominator))
                       << ",\"input_events\":" << timeline.events().size() << ",\"admitted\":" << plan.actions().size()
                       << ",\"suppressed_density\":" << timeline.events().size() - plan.actions().size()
                       << ",\"admitted_missing_species\":" << plan.missingSpecies() << ",\"capture_truncated\":" << (saved.truncated ? "true" : "false")
                       << ",\"admission\":\"first-4-per-scaled-10ms-v1\",\"pitch_mapping\":\"fnv1a32-species-pentatonic-v1\",\"stereo\":\"centred-dual-mono\""
                       << ",\"sample_rate\":" << settings.sampleRate << ",\"root_midi\":" << settings.rootMidi
                       << ",\"attack_seconds\":" << settings.attackSeconds << ",\"decay_seconds\":" << settings.decaySeconds
                       << ",\"gain_db\":" << settings.masterDb << ",\"frames\":" << quoteJson(std::to_string(plan.totalFrames()))
                       << ",\"synthetic_audio\":true,\"dither\":false}";
                const auto result = atlas::renderWave(plan, get("--output", ""), recipe.str(), blockFrames);
                std::cout << std::setprecision(17) << "{\"recipe\":" << recipe.str() << ",\"triggered\":" << result.triggered
                          << ",\"stolen\":" << result.stolen << ",\"faults\":" << result.faults << ",\"peak\":" << result.peak << "}\n";
                return 0;
            }
            const auto limitText = get("--limit", "5");
            unsigned limit{};
            const auto parsed = std::from_chars(limitText.data(), limitText.data() + limitText.size(), limit);
            if (parsed.ec != std::errc{} || parsed.ptr != limitText.data() + limitText.size() || limit > 50000)
                throw std::invalid_argument("limit must be 0..50000");
            std::cout << "{\"engine\":\"portable-cpp\",\"session_id\":" << quoteJson(saved.id)
                      << ",\"mode\":" << quoteJson(mode) << ",\"event_count\":" << timeline.events().size()
                      << ",\"duration_us\":" << timeline.durationUs() << ",\"peak_events_in_one_second\":" << timeline.peakEventsPerSecond()
                      << ",\"capture_truncated\":" << (saved.truncated ? "true" : "false")
                      << ",\"observed_window_policy\":" << quoteJson(saved.observedWindowPolicy)
                      << ",\"empty_material_meaning\":\"no selected records; not evidence of ecological silence\""
                      << ",\"integrity\":" << quoteJson("sha256-event-content-verified; " + saved.metadataIntegrity) << ",\"events\":[";
            const auto count = std::min<std::size_t>(limit, timeline.schedule().size());
            for (std::size_t i = 0; i < count; ++i) {
                if (i) std::cout << ',';
                const auto& item = timeline.schedule()[i];
                std::cout << "{\"index\":" << i << ",\"due_us\":" << item.dueUs
                          << ",\"original_seq\":" << item.revisionSequence
                          << ",\"event\":" << timeline.events()[item.eventIndex].normalizedJson << '}';
            }
            std::cout << "]}\n";
        } else throw std::invalid_argument("unknown command");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animal Atlas native: " << error.what() << '\n';
        return 1;
    }
}
