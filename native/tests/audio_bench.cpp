#include <atlas/audio.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    std::cout << "Synthetic scalar process benchmark; no audio device, GUI, DAW, or hardware qualification.\n";
#ifndef NDEBUG
    std::cerr << "Run a Release build for performance evidence.\n";
    return 1;
#else
    std::vector<atlas::Event> events;
    for (int window = 0; window < 3000; ++window) for (int j = 0; j < 4; ++j) {
        atlas::Event e; e.id = "bench:" + std::to_string(window * 4 + j); e.sourceId = "synthetic";
        e.kind = "acoustic_species_detection"; e.locationKind = "station_detection"; e.stationId = "bench:station";
        e.revisionSequence = window * 4 + j + 1; e.observedAt = window * 10000; e.firstSeenAt = e.observedAt;
        events.push_back(std::move(e));
    }
    const atlas::Timeline timeline(std::move(events), atlas::TimeBasis::observed);
    std::array<float, 8192> output{};
    std::cout << "rate,block,calls,p50_us,p95_us,p99_us,max_us,deadline_us,misses\n";
    for (auto rate : {22050u, 48000u, 96000u, 192000u}) for (auto block : {32u, 64u, 128u, 256u, 512u, 1024u, 4096u}) {
        atlas::AudioSettings s; s.sampleRate = rate; const atlas::AudioPlan p(timeline, s); atlas::AudioRenderer r(p);
        if (!r.play(r.generation())) return 2;
        std::vector<double> times; times.reserve(static_cast<std::size_t>(p.totalFrames() / block + 1));
        const auto buffer = std::span<float>(output).first(block * 2);
        // Warm active oscillators before collecting the full dense synthetic timeline.
        for (int i = 0; i < 16; ++i) if (!r.process(buffer)) return 2;
        if (!r.panic(r.generation()) || !r.play(r.generation())) return 2;
        std::size_t misses{}; const double deadline = 1e6 * block / rate;
        while (r.state() != atlas::AudioState::finished) {
            const auto start = std::chrono::steady_clock::now();
            if (!r.process(buffer)) return 2;
            const auto end = std::chrono::steady_clock::now();
            const auto us = std::chrono::duration<double, std::micro>(end - start).count();
            times.push_back(us); if (us > deadline) ++misses;
        }
        if (r.statistics().faults) return 3;
        std::sort(times.begin(), times.end());
        const auto percentile = [&](double q) { return times[static_cast<std::size_t>(std::ceil(q * times.size())) - 1]; };
        std::cout << rate << ',' << block << ',' << times.size() << ',' << percentile(.50) << ',' << percentile(.95) << ',' << percentile(.99) << ',' << times.back() << ',' << deadline << ',' << misses << '\n';
    }
    return 0;
#endif
}
