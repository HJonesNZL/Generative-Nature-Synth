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
    if (argc > 1 && !characterMode) {
        std::cerr << "Usage: atlas-audio-bench [--characters]\n";
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
            if (characterMode) {
                constexpr std::array offsets{0, 10000, 29999, 30000};
                event.observedAt = (group / 4) * 40000 +
                                   offsets[static_cast<std::size_t>(group % 4)];
                event.speciesId = "tone:1";
            } else
                event.observedAt = group * 10000;
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
        const std::array<atlas::VoiceProfile, 2> profiles{
            {{"", {}}, {"tone:1", voice}}};
        const auto selected =
            characterMode ? std::span<const atlas::VoiceProfile>(profiles)
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
        if (characterMode)
            std::cout << name << ',';
        std::cout << rate << ',' << block << ',' << times.size() << ','
                  << percentile(.50) << ',' << percentile(.95) << ','
                  << percentile(.99) << ',' << times.back() << ',' << deadline
                  << ',' << misses;
        if (characterMode)
            std::cout << ',' << renderer.statistics().triggered << ','
                      << renderer.statistics().stolen << ','
                      << renderer.statistics().peak;
        std::cout << '\n';
        return true;
    };
    if (!characterMode) {
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
