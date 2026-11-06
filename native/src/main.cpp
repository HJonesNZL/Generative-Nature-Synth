#include "atlas_build_identity.hpp"
#include "json_text.hpp"
#include <algorithm>
#include <atlas/presets.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/storage.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/wav.hpp>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

using atlas::detail::quoteJson;

int main(int argc, char **argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument(
                "usage: atlas-native sessions|replay|render|ui-host --db PATH "
                "[--session latest --mode observed|arrival --speed 60 --limit "
                "5]");
        if (std::string_view(argv[1]) == "version") {
            if (argc != 2)
                throw std::invalid_argument("version takes no options");
            std::cout
                << "{\"engine\":\"animal-atlas-native\",\"source_sha256\":"
                << quoteJson(atlas::nativeSourceSha256) << "}\n";
            return 0;
        }
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc || !std::string_view(argv[i]).starts_with("--"))
                throw std::invalid_argument("options require values");
            if (!options.emplace(argv[i], argv[i + 1]).second)
                throw std::invalid_argument("duplicate option");
        }
        for (const auto &[key, value] : options) {
            (void)value;
            if (key != "--db" && key != "--session" && key != "--mode" &&
                key != "--speed" && key != "--limit" && key != "--output" &&
                key != "--sample-rate" && key != "--block-frames" &&
                key != "--scene" && key != "--root-midi" &&
                key != "--attack-ms" && key != "--decay-ms" &&
                key != "--gain-db")
                throw std::invalid_argument("unknown option");
        }
        const auto get = [&](const std::string &key,
                             const std::string &fallback) {
            const auto found = options.find(key);
            return found == options.end() ? fallback : found->second;
        };
        if (std::string_view(argv[1]) == "ui-host") {
            atlas::UiHost host(get("--db", ".state/atlas.sqlite3"));
            std::string line;
            line.reserve(16385);
            char c{};
            while (std::cin.get(c)) {
                if (c == '\n') {
                    std::cout << host.request(line) << '\n' << std::flush;
                    line.clear();
                } else if (line.size() < 16385)
                    line += c;
            }
            return 0;
        }
        if (std::string_view(argv[1]) == "scene-render") {
            if (options.size() != 2 || !options.contains("--scene") ||
                !options.contains("--output"))
                throw std::invalid_argument(
                    "scene-render requires only --scene PATH --output WAV");
            const auto scene = atlas::loadSoundscape(get("--scene", ""));
            const auto &capture = scene.capture();
            const atlas::AudioPlan plan(scene.timeline(), capture.audio,
                                        capture.profiles, true);
            const auto recipe =
                "{\"recipe_version\":2,\"scene_hash\":" +
                quoteJson(scene.contentHash()) + ",\"engine_source_sha256\":" +
                quoteJson(atlas::nativeSourceSha256) +
                ",\"synthesis_revision\":" +
                std::to_string(capture.synthesisRevision) +
                ",\"mapping_revision\":" +
                std::to_string(capture.mappingRevision) +
                ",\"authoritative_recipe\":\"self-contained-atlas-scene\"}";
            atlas::renderWave(plan, get("--output", ""), recipe);
            std::cout << "{\"sceneHash\":" << quoteJson(scene.contentHash())
                      << ",\"frames\":" << plan.totalFrames() << "}\n";
            return 0;
        }
        if (std::string_view(argv[1]) == "audition-presets") {
            if (options.size() != 1 || !options.contains("--output"))
                throw std::invalid_argument(
                    "audition-presets requires --output NEW_DIRECTORY");
            const std::filesystem::path directory(get("--output", ""));
            if (!std::filesystem::create_directory(directory))
                throw std::invalid_argument(
                    "Audition output must be a new directory");
            std::ofstream manifest(directory / "manifest.json");
            if (!manifest)
                throw std::runtime_error("Cannot write audition manifest");
            manifest << "{\"syntheticTestSequence\":true,\"listeningApproved\":"
                        "false,\"engineSourceSha256\":"
                     << quoteJson(atlas::nativeSourceSha256)
                     << ",\"sampleRate\":48000,\"masterDb\":-18,\"items\":[";
            unsigned index{};
            for (const auto &preset : atlas::speciesPresets()) {
                std::vector<atlas::Event> events;
                for (std::int64_t note = 0; note < 3; ++note) {
                    atlas::Event event;
                    event.id = "audition:" + std::to_string(note);
                    event.sourceId = "synthetic-audition";
                    event.kind = "acoustic_species_detection";
                    event.locationKind = "station_detection";
                    event.stationId = "synthetic:station";
                    event.speciesId = "audition:voice";
                    event.observedAt = note * 900000;
                    event.firstSeenAt = event.observedAt;
                    event.revisionSequence = note + 1;
                    events.push_back(std::move(event));
                }
                const atlas::Timeline timeline(std::move(events),
                                               atlas::TimeBasis::observed);
                const std::array<atlas::VoiceProfile, 2> profiles{
                    {{"", {}}, {"audition:voice", preset.settings}}};
                atlas::AudioSettings settings;
                settings.masterDb = -18;
                const atlas::AudioPlan plan(timeline, settings, profiles, true);
                const auto filename = std::to_string(++index) + ".wav";
                const auto recipe =
                    "{\"synthetic_test_sequence\":true,\"preset_id\":" +
                    quoteJson(preset.id) + ",\"engine_source_sha256\":" +
                    quoteJson(atlas::nativeSourceSha256) + "}";
                atlas::renderWave(plan, directory / filename, recipe);
                if (index > 1)
                    manifest << ',';
                manifest << "{\"file\":" << quoteJson(filename)
                         << ",\"id\":" << quoteJson(preset.id)
                         << ",\"name\":" << quoteJson(preset.name)
                         << ",\"speciesId\":" << quoteJson(preset.speciesId)
                         << ",\"scientificName\":"
                         << quoteJson(preset.scientificName)
                         << ",\"description\":" << quoteJson(preset.description)
                         << ",\"evidenceStatus\":"
                         << quoteJson(preset.evidenceStatus) << "}";
            }
            manifest << "]}\n";
            manifest.close();
            if (!manifest)
                throw std::runtime_error("Audition manifest write failed");
            std::cout << "{\"rendered\":" << index
                      << ",\"listeningApproved\":false}\n";
            return 0;
        }
        atlas::ReadOnlyLibrary library(get("--db", ".state/atlas.sqlite3"));
        if (std::string_view(argv[1]) == "sessions") {
            std::cout << "[";
            bool first = true;
            for (const auto &session : library.sessions()) {
                if (!first)
                    std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quoteJson(session.id)
                          << ",\"name\":" << quoteJson(session.name)
                          << ",\"state\":" << quoteJson(session.state)
                          << ",\"event_count\":" << session.eventCount
                          << ",\"capture_truncated\":"
                          << (session.truncated ? "true" : "false") << '}';
            }
            std::cout << "]\n";
        } else if (std::string_view(argv[1]) == "replay" ||
                   std::string_view(argv[1]) == "render") {
            const auto mode = get("--mode", "observed");
            if (mode != "observed" && mode != "arrival")
                throw std::invalid_argument("invalid time basis");
            const auto speed = atlas::Speed::parse(get("--speed", "1"));
            atlas::SavedSession saved;
            const auto timeline =
                library.load(get("--session", "latest"),
                             mode == "observed" ? atlas::TimeBasis::observed
                                                : atlas::TimeBasis::arrival,
                             speed, &saved);
            if (std::string_view(argv[1]) == "render") {
                const auto number = [&](const std::string &key,
                                        const std::string &fallback) {
                    const auto text = get(key, fallback);
                    double value{};
                    std::istringstream input(text);
                    input.imbue(std::locale::classic());
                    if (!(input >> std::noskipws >> value) ||
                        input.peek() != std::char_traits<char>::eof() ||
                        !std::isfinite(value))
                        throw std::invalid_argument(
                            "invalid finite numeric render option");
                    return value;
                };
                const auto integerOption = [&](const std::string &key,
                                               const std::string &fallback,
                                               double maximum) {
                    const auto value = number(key, fallback);
                    if (value < 0 || value > maximum ||
                        std::floor(value) != value)
                        throw std::invalid_argument(
                            "invalid integer render option");
                    return static_cast<unsigned>(value);
                };
                atlas::AudioSettings settings;
                settings.sampleRate =
                    integerOption("--sample-rate", "48000", 192000);
                settings.rootMidi = integerOption("--root-midi", "60", 72);
                settings.attackSeconds = number("--attack-ms", "8") / 1000;
                settings.decaySeconds = number("--decay-ms", "650") / 1000;
                settings.masterDb = number("--gain-db", "0");
                const auto blockFrames =
                    integerOption("--block-frames", "512", 4096);
                const atlas::AudioPlan plan(timeline, settings);
                std::ostringstream recipe;
                recipe << std::setprecision(17)
                       << "{\"recipe_version\":1,\"engine\":\"atlas-struck-"
                          "tone-v1\",\"session_id\":"
                       << quoteJson(saved.id) << ",\"engine_source_sha256\":"
                       << quoteJson(atlas::nativeSourceSha256)
                       << ",\"session_name\":" << quoteJson(saved.name)
                       << ",\"source_content_sha256\":"
                       << quoteJson(saved.contentHash)
                       << ",\"metadata_integrity\":"
                       << quoteJson(saved.metadataIntegrity)
                       << ",\"mode\":" << quoteJson(mode)
                       << ",\"speed_numerator\":"
                       << quoteJson(std::to_string(speed.numerator))
                       << ",\"speed_denominator\":"
                       << quoteJson(std::to_string(speed.denominator))
                       << ",\"input_events\":" << timeline.events().size()
                       << ",\"admitted\":" << plan.actions().size()
                       << ",\"suppressed_density\":"
                       << timeline.events().size() - plan.actions().size()
                       << ",\"admitted_missing_species\":"
                       << plan.missingSpecies() << ",\"capture_truncated\":"
                       << (saved.truncated ? "true" : "false")
                       << ",\"admission\":\"first-4-per-scaled-10ms-v1\","
                          "\"pitch_mapping\":\"fnv1a32-species-pentatonic-v1\","
                          "\"stereo\":\"centred-dual-mono\""
                       << ",\"sample_rate\":" << settings.sampleRate
                       << ",\"root_midi\":" << settings.rootMidi
                       << ",\"attack_seconds\":" << settings.attackSeconds
                       << ",\"decay_seconds\":" << settings.decaySeconds
                       << ",\"gain_db\":" << settings.masterDb << ",\"frames\":"
                       << quoteJson(std::to_string(plan.totalFrames()))
                       << ",\"synthetic_audio\":true,\"dither\":false}";
                const auto result = atlas::renderWave(
                    plan, get("--output", ""), recipe.str(), blockFrames);
                std::cout << std::setprecision(17)
                          << "{\"recipe\":" << recipe.str()
                          << ",\"triggered\":" << result.triggered
                          << ",\"stolen\":" << result.stolen
                          << ",\"faults\":" << result.faults
                          << ",\"peak\":" << result.peak << "}\n";
                return 0;
            }
            const auto limitText = get("--limit", "5");
            unsigned limit{};
            const auto parsed = std::from_chars(
                limitText.data(), limitText.data() + limitText.size(), limit);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != limitText.data() + limitText.size() ||
                limit > 50000)
                throw std::invalid_argument("limit must be 0..50000");
            std::cout << "{\"engine\":\"portable-cpp\",\"session_id\":"
                      << quoteJson(saved.id) << ",\"mode\":" << quoteJson(mode)
                      << ",\"event_count\":" << timeline.events().size()
                      << ",\"duration_us\":" << timeline.durationUs()
                      << ",\"peak_events_in_one_second\":"
                      << timeline.peakEventsPerSecond()
                      << ",\"capture_truncated\":"
                      << (saved.truncated ? "true" : "false")
                      << ",\"observed_window_policy\":"
                      << quoteJson(saved.observedWindowPolicy)
                      << ",\"empty_material_meaning\":\"no selected records; "
                         "not evidence of ecological silence\""
                      << ",\"integrity\":"
                      << quoteJson("sha256-event-content-verified; " +
                                   saved.metadataIntegrity)
                      << ",\"events\":[";
            const auto count =
                std::min<std::size_t>(limit, timeline.schedule().size());
            for (std::size_t i = 0; i < count; ++i) {
                if (i)
                    std::cout << ',';
                const auto &item = timeline.schedule()[i];
                std::cout << "{\"index\":" << i << ",\"due_us\":" << item.dueUs
                          << ",\"original_seq\":" << item.revisionSequence
                          << ",\"event\":"
                          << timeline.events()[item.eventIndex].normalizedJson
                          << '}';
            }
            std::cout << "]}\n";
        } else
            throw std::invalid_argument("unknown command");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Animal Atlas native: " << error.what() << '\n';
        return 1;
    }
}
