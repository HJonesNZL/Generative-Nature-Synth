#include <algorithm>
#include <array>
#include <atlas/audio.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char **argv) {
    std::cout << "Synthetic scalar process benchmark; no audio device, GUI, "
                 "DAW, or hardware qualification.\n";
#ifndef NDEBUG
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cerr << "Run a Release build for performance evidence.\n";
    return 1;
#else
    const bool characterMode =
        argc == 2 && std::string_view(argv[1]) == "--characters";
    const bool compositionMode =
        argc == 2 && std::string_view(argv[1]) == "--composition";
    const bool spatialMode =
        argc == 2 && std::string_view(argv[1]) == "--spatial";
    bool spatialComposition{};
    if (argc > 1 && !characterMode && !compositionMode && !spatialMode) {
        std::cerr << "Usage: atlas-audio-bench "
                     "[--characters|--composition|--spatial]\n";
        return 2;
    }
    std::vector<atlas::Event> events;
    for (int group = 0; group < 3000; ++group) {
        for (int j = 0; j < 4; ++j) {
            atlas::Event event;
            event.id = "bench:" + std::to_string(group * 4 + j);
            event.sourceId = "synthetic";
            event.kind = "acoustic_species_detection";
            event.locationKind = "station_detection";
            event.stationId = "bench:station";
            event.revisionSequence = group * 4 + j + 1;
            if (characterMode || spatialMode) {
                constexpr std::array offsets{0, 10000, 29999, 30000};
                event.observedAt = (group / 4) * 40000 +
                                   offsets[static_cast<std::size_t>(group % 4)];
                event.speciesId = "tone:1";
            } else
                event.observedAt = group * 10000;
            if (compositionMode || spatialMode)
                event.speciesId = "birdweather:species:2561";
            if (spatialMode)
                event.position =
                    atlas::Position{0, -157.5 + 45 * ((group * 4 + j) % 8)};
            event.firstSeenAt = event.observedAt;
            events.push_back(std::move(event));
        }
    }
    const atlas::Timeline timeline(std::move(events),
                                   atlas::TimeBasis::observed);
    std::array<float, 8192> output{};
    const auto run = [&](std::string_view name, atlas::VoiceSettings voice,
                         std::uint32_t rate, unsigned block) {
        atlas::AudioSettings settings;
        settings.sampleRate = rate;
        if (compositionMode || (spatialMode && spatialComposition)) {
            settings.composition.enabled = true;
            settings.composition.density = atlas::CompositionDensity::active;
            settings.composition.variation = .7;
            settings.composition.seed = 812;
        }
        if (spatialMode) {
            settings.spatial.enabled = true;
            settings.spatial.amount = 1;
            for (unsigned region = 0; region < 8; ++region) {
                atlas::RegionSettings value;
                value.id = "bench" + std::to_string(region);
                value.name = value.id;
                value.west = -180 + 45.0 * region;
                value.east = value.west + 45;
                value.pan = -1 + 2.0 * region / 7;
                value.send = 1;
                value.feedback = .5;
                value.delayMs = 500;
                value.roomSeconds = 2.5;
                value.echoMix = .5;
                settings.spatial.regions.push_back(value);
            }
        }
        const std::array<atlas::VoiceProfile, 2> profiles{
            {{"", {}},
             {(compositionMode || spatialMode) ? "birdweather:species:2561"
                                               : "tone:1",
              voice}}};
        const auto selected =
            (characterMode || compositionMode || spatialMode)
                ? std::span<const atlas::VoiceProfile>(profiles)
                : std::span<const atlas::VoiceProfile>{};
        const atlas::AudioPlan plan(timeline, settings, selected);
        atlas::AudioRenderer renderer(plan);
        if (!renderer.play(renderer.generation()))
            return false;
        std::vector<double> times;
        times.reserve(static_cast<std::size_t>(plan.totalFrames() / block + 1));
        const auto buffer = std::span<float>(output).first(block * 2);
        for (int i = 0; i < 16; ++i)
            if (!renderer.process(buffer))
                return false;
        if (!renderer.panic(renderer.generation()) ||
            !renderer.play(renderer.generation()))
            return false;
        const auto warmed = renderer.statistics();
        std::size_t misses{};
        const double deadline = 1e6 * block / rate;
        while (renderer.state() != atlas::AudioState::finished) {
            const auto start = std::chrono::steady_clock::now();
            if (!renderer.process(buffer))
                return false;
            const auto end = std::chrono::steady_clock::now();
            const auto us =
                std::chrono::duration<double, std::micro>(end - start).count();
            times.push_back(us);
            if (us > deadline)
                ++misses;
        }
        if (renderer.statistics().faults)
            return false;
        std::sort(times.begin(), times.end());
        const auto percentile = [&](double q) {
            return times[static_cast<std::size_t>(
                             std::ceil(q * static_cast<double>(times.size()))) -
                         1];
        };
        if (characterMode || compositionMode || spatialMode)
            std::cout << name << ',';
        std::cout << rate << ',' << block << ',' << times.size() << ','
                  << percentile(.50) << ',' << percentile(.95) << ','
                  << percentile(.99) << ',' << times.back() << ',' << deadline
                  << ',' << misses;
        if (characterMode || compositionMode || spatialMode)
            std::cout << ','
                      << renderer.statistics().triggered - warmed.triggered
                      << ',' << renderer.statistics().stolen - warmed.stolen
                      << ',' << renderer.statistics().peak;
        std::cout << '\n';
        return true;
    };
    if (spatialMode) {
        std::cout << "Eight configured buses with max feedback/decay/delay and "
                     "send1, "
                     "12000 source events cycling all regions, amount1; 12s "
                     "bounded tail.\n";
        std::cout << "character,rate,block,calls,p50_us,p95_us,p99_us,max_us,"
                     "deadline_us,misses,triggered,stolen,peak\n";
        constexpr std::array names{"struck", "whistle", "reed",
                                   "bell",   "air",     "pulse"};
        for (const bool composed : {false, true}) {
            spatialComposition = composed;
            for (unsigned character = 0; character < names.size();
                 ++character) {
                atlas::VoiceSettings voice;
                voice.character = static_cast<atlas::VoiceCharacter>(character);
                voice.attackSeconds = .100;
                voice.decaySeconds = 3;
                voice.brightness = .9;
                voice.roughness = .8;
                voice.pulseHz = 13;
                voice.sweepSemitones = -7;
                const auto name =
                    std::string(composed ? "composed-" : "direct-") +
                    names[character];
                for (const auto block : {64u, 512u})
                    if (!run(name, voice, 48000, block))
                        return 3;
                if (character == 5)
                    for (const auto block : {32u, 512u})
                        if (!run(name, voice, 192000, block))
                            return 3;
            }
        }
    } else if (compositionMode) {
        std::cout
            << "Composition active-density Tui motif, seed812, variation.7, "
               "12000 source events over30s; all six modulation characters.\n";
        std::cout << "character,rate,block,calls,p50_us,p95_us,p99_us,max_us,"
                     "deadline_us,"
                     "misses,triggered,stolen,peak\n";
        constexpr std::array names{"struck", "whistle", "reed",
                                   "bell",   "air",     "pulse"};
        for (unsigned character = 0; character < 6; ++character) {
            atlas::VoiceSettings voice;
            voice.character = static_cast<atlas::VoiceCharacter>(character);
            voice.decaySeconds = 3;
            voice.roughness = .8;
            voice.pulseHz = 13;
            voice.sweepSemitones = -7;
            for (const auto block : {64u, 256u, 512u})
                if (!run(names[character], voice, 48000, block))
                    return 3;
        }
    } else if (!characterMode) {
        std::cout << "rate,block,calls,p50_us,p95_us,p99_us,max_us,deadline_us,"
                     "misses\n";
        for (const auto rate : {22050u, 48000u, 96000u, 192000u})
            for (const auto block : {32u, 64u, 128u, 256u, 512u, 1024u, 4096u})
                if (!run("legacy", {}, rate, block))
                    return 3;
    } else {
        std::cout << "Character workload includes 8 active + 8 simultaneous "
                     "retiring states at adjacent-window frame collisions; "
                     "all nonlegacy cases use brightness .9, roughness .8, "
                     "pulse13Hz, sweep-7st, attack100ms, decay3s.\n";
        std::cout << "character,rate,block,calls,p50_us,p95_us,p99_us,max_us,"
                     "deadline_us,misses,triggered,stolen,peak\n";
        constexpr std::array names{
            "legacy", "struck-modulated", "whistle", "reed",
            "bell",   "air-multisine",    "pulse"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            atlas::VoiceSettings voice;
            if (index) {
                voice.character = static_cast<atlas::VoiceCharacter>(index - 1);
                voice.attackSeconds = .100;
                voice.decaySeconds = 3;
                voice.brightness = .9;
                voice.roughness = .8;
                voice.pulseHz = 13;
                voice.sweepSemitones = -7;
            }
            for (const auto rate : {48000u, 192000u})
                for (const auto block : {32u, 128u, 512u})
                    if (!run(names[index], voice, rate, block))
                        return 3;
        }
    }
    return 0;
#endif
}
