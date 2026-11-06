#include <atlas/audio.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
std::atomic<unsigned long> allocations{}, frees{};
void* allocate(std::size_t n, std::size_t alignment = alignof(std::max_align_t)) {
    ++allocations;
    void* p{};
    if (alignment > alignof(std::max_align_t)) {
        const auto size = n ? n : 1;
        if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1)) throw std::bad_alloc{};
#if defined(_MSC_VER)
        p = _aligned_malloc(size, alignment);
#else
        p = std::aligned_alloc(alignment, ((size + alignment - 1) / alignment) * alignment);
#endif
    } else p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc{};
    return p;
}
void release(void* p) noexcept { if (p) ++frees; std::free(p); }
void releaseAligned(void* p, std::align_val_t alignment) noexcept {
#if defined(_MSC_VER)
    if (static_cast<std::size_t>(alignment) > alignof(std::max_align_t)) {
        if (p) ++frees;
        _aligned_free(p);
        return;
    }
#else
    static_cast<void>(alignment);
#endif
    release(p);
}
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t a) noexcept { releaseAligned(p, a); }
void operator delete[](void* p, std::align_val_t a) noexcept { releaseAligned(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { releaseAligned(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { releaseAligned(p, a); }

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
atlas::Event event(std::int64_t id, atlas::Microseconds time) {
    atlas::Event e;
    e.id = "test:" + std::to_string(id); e.sourceId = "test";
    e.kind = "acoustic_species_detection"; e.locationKind = "station_detection";
    e.stationId = "test:station"; e.revisionSequence = id;
    e.observedAt = time; e.firstSeenAt = time;
    return e;
}
atlas::Timeline timeline(std::vector<atlas::Event> events) {
    return atlas::Timeline(std::move(events), atlas::TimeBasis::observed);
}
std::vector<float> render(const atlas::AudioPlan& plan, std::size_t block) {
    atlas::AudioRenderer r(plan); check(r.play(r.generation()), "play");
    std::vector<float> out(static_cast<std::size_t>(plan.totalFrames()) * 2);
    for (std::size_t i = 0; i < out.size();) {
        const auto count = std::min(block * 2, out.size() - i);
        check(r.process(std::span<float>(out).subspan(i, count)), "process"); i += count;
    }
    check(r.state() == atlas::AudioState::finished, "natural finish");
    check(r.statistics().faults == 0, "render faults");
    return out;
}
double oracle(std::uint64_t age, const atlas::AudioSettings& s) {
    const double pi = std::numbers::pi;
    const auto a = std::ceil(s.attackSeconds * s.sampleRate);
    const auto d = std::ceil(s.decaySeconds * s.sampleRate);
    const auto r = std::ceil(.020 * s.sampleRate);
    const auto n = static_cast<double>(age);
    if (n >= a + d + r) return 0;
    const double attack = n < a ? (1 - std::cos(pi * n / a)) / 2 : 1;
    const double decay = std::exp(-std::log(1000.) * std::max(0., n - a) / d);
    const double fade = n < a + d ? 1 : (1 + std::cos(pi * (n - a - d) / r)) / 2;
    const double frequency = 440 * std::pow(2., (static_cast<double>(s.rootMidi) - 69) / 12);
    const double phase = 2 * pi * frequency * n / s.sampleRate;
    return .05 * attack * decay * fade * (std::sin(phase) + .25 * std::sin(2 * phase) + .08 * std::sin(3 * phase)) / 1.33 * std::pow(10., s.masterDb / 20);
}
void advance(atlas::AudioRenderer& r, std::uint64_t n) {
    std::array<float, 8192> scratch{};
    while (n) { const auto count = std::min<std::uint64_t>(n, 4096); check(r.process(std::span<float>(scratch).first(static_cast<std::size_t>(count) * 2)), "advance"); n -= count; }
}
}
int main() {
    unsigned failed{};
    const auto test = [&](const char* name, auto body) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    test("direct equation normal and parameter/rate extremes", [] {
        for (auto rate : {22050u, 48000u, 192000u}) for (bool extreme : {false, true}) {
            atlas::AudioSettings s; s.sampleRate = rate;
            if (extreme) { s.rootMidi = 72; s.attackSeconds = .100; s.decaySeconds = 3; s.masterDb = -60; }
            else { s.rootMidi = 48; s.attackSeconds = .002; s.decaySeconds = .100; }
            const atlas::AudioPlan p(timeline({event(1, 0)}), s); const auto out = render(p, 257);
            for (std::size_t n = 0; n < p.totalFrames(); ++n) {
                check(std::abs(out[n * 2] - oracle(n, s)) < 2e-7, "equation mismatch");
                check(out[n * 2] == out[n * 2 + 1], "dual mono");
            }
        }
        const atlas::AudioPlan p(timeline({event(1, 0)})); const auto out = render(p, 64);
        for (std::size_t n = 0; n < p.totalFrames(); ++n) check(std::abs(out[n * 2] - oracle(n, p.settings())) < 2e-7, "default equation");
    });
    test("absolute admission and upward timing", [] {
        std::vector<atlas::Event> es;
        for (int i = 0; i < 6; ++i) es.push_back(event(i + 1, i));
        es.push_back(event(7, 9999)); es.push_back(event(8, 10000)); es.push_back(event(9, 10001));
        atlas::AudioSettings s; s.sampleRate = 22050;
        const atlas::AudioPlan p(timeline(es), s);
        check(p.actions().size() == 6 && p.decisions().size() == 9, "window cap");
        const std::array<std::uint64_t, 6> expected{0, 1, 1, 1, 221, 221};
        for (std::size_t i = 0; i < expected.size(); ++i) check(p.actions()[i].frame == expected[i], "ceil timing");
        check(p.decisions()[4].reason == atlas::Admission::density && p.decisions()[6].reason == atlas::Admission::density, "suppression ledger");
    });
    test("overload partition invariance and retiring capacity", [] {
        std::vector<atlas::Event> es;
        for (int window = 0; window < 30; ++window) for (int i = 0; i < 12; ++i) es.push_back(event(window * 12 + i + 1, window * 10000 + i));
        const atlas::AudioPlan p(timeline(es));
        const auto reference = render(p, 1);
        for (auto block : {17u, 64u, 257u, 4096u}) check(render(p, block) == reference, "block changes samples");
        atlas::AudioRenderer r(p); check(r.play(r.generation()), "play"); advance(r, p.totalFrames());
        check(r.statistics().triggered == 120 && r.statistics().stolen == 112 && r.statistics().faults == 0, "voice capacity or steals");
        for (float v : reference) check(std::isfinite(v) && std::abs(v) <= .8f, "numerical bound");
    });
    test("oldest steals with eight simultaneous retiring states", [] {
        std::vector<atlas::Event> es;
        for (const auto time : {0, 10000, 29999, 30000}) for (int j = 0; j < 4; ++j)
            es.push_back(event(static_cast<std::int64_t>(es.size() + 1), time));
        const atlas::AudioPlan p(timeline(es));
        const auto out = render(p, 37);
        // Both adjacent windows round upward to frame 1440. Oldest groups at
        // frames 0 and 480 retire together; new groups both begin at 1440.
        for (std::uint64_t frame = 1440; frame < 1680; ++frame) {
            const double fade = (1 + std::cos(std::numbers::pi * static_cast<double>(frame - 1440) / 240)) / 2;
            const double expected = 4 * (oracle(frame, p.settings()) + oracle(frame - 480, p.settings())) * fade
                                  + 8 * oracle(frame - 1440, p.settings());
            check(std::abs(out[static_cast<std::size_t>(frame) * 2] - expected) < 2e-7, "oldest retirement samples");
        }
        atlas::AudioRenderer r(p); check(r.play(r.generation()), "play"); advance(r, 1441);
        check(r.statistics().triggered == 16 && r.statistics().stolen == 8 && r.statistics().faults == 0, "eight retirement slots");
    });
    test("pause resume stop panic seek and stale controls", [] {
        const atlas::AudioPlan p(timeline({event(1, 0), event(2, 200000)})); atlas::AudioRenderer r(p);
        const auto g = r.generation(); check(r.play(g), "play"); advance(r, 1000);
        check(!r.seek(0, g), "playing seek accepted"); check(r.pause(g), "pause"); advance(r, 240);
        check(r.state() == atlas::AudioState::paused, "pause endpoint"); const auto frozen = r.position(); advance(r, 1024); check(r.position() == frozen, "paused timeline moves");
        check(r.play(g), "resume"); advance(r, 240); check(r.position() == frozen + 240, "resume timeline");
        check(r.stop(g), "stop"); advance(r, 240); check(r.state() == atlas::AudioState::ready && r.position() == 0, "stop endpoint");
        check(!r.play(g) && !r.pause(g) && !r.stop(g) && !r.panic(g) && !r.seek(0, g), "stale generation accepted");
        check(r.seek(100, r.generation()), "seek"); check(r.state() == atlas::AudioState::paused && r.position() == 100, "seek state");
        check(r.stop(r.generation()), "paused stop"); check(r.position() == 0 && r.state() == atlas::AudioState::ready, "paused stop endpoint");
        check(r.play(r.generation()), "replay"); advance(r, 1000); check(r.panic(r.generation()), "panic");
        std::array<float, 128> out; out.fill(1); check(r.process(out), "panic process"); check(std::all_of(out.begin(), out.end(), [](float v) { return v == 0; }), "panic silence");
    });
    test("normal-level analytical transport ramps and repeated retarget", [] {
        const atlas::AudioPlan p(timeline({event(1, 0)})); atlas::AudioRenderer r(p);
        check(r.play(r.generation()), "play"); advance(r, 1000);
        const auto ramp = [&](double from, double to, unsigned frames) {
            const auto first = r.position();
            std::array<float, 480> out{};
            check(r.process(std::span<float>(out).first(frames * 2)), "ramp process");
            for (unsigned i = 0; i < frames; ++i) {
                const double progress = static_cast<double>(i + 1) / 240;
                const double gain = from + (to - from) * (1 - std::cos(std::numbers::pi * progress)) / 2;
                const double expected = oracle(first + i, p.settings()) * gain;
                check(std::abs(out[i * 2] - expected) < 2e-7 && out[i * 2] == out[i * 2 + 1], "transport ramp samples");
            }
            return from + (to - from) * (1 - std::cos(std::numbers::pi * static_cast<double>(frames) / 240)) / 2;
        };
        check(r.pause(r.generation()), "pause"); ramp(1, 0, 240);
        const auto frozen = r.position(); advance(r, 100); check(r.position() == frozen, "pause freeze");
        check(r.play(r.generation()), "resume"); const double resumed = ramp(0, 1, 80);
        check(r.pause(r.generation()), "retarget resume to pause"); const double pausing = ramp(resumed, 0, 60);
        check(r.stop(r.generation()), "retarget pause to stop"); const double draining = ramp(pausing, 0, 90);
        check(r.stop(r.generation()), "repeat stop while draining"); ramp(draining, 0, 240);
        check(r.state() == atlas::AudioState::ready && r.position() == 0, "repeated stop endpoint");
    });
    test("dense stealing callback allocates and destroys nothing", [] {
        std::vector<atlas::Event> es;
        for (int window = 0; window < 40; ++window) for (int i = 0; i < 12; ++i)
            es.push_back(event(window * 12 + i + 1, window * 10000 + i));
        const atlas::AudioPlan p(timeline(es)); atlas::AudioRenderer r(p);
        std::array<float, 514> out{};
        const auto a = allocations.load(), f = frees.load();
        bool ok = r.play(r.generation());
        while (r.state() != atlas::AudioState::finished) ok = r.process(out) && ok;
        const auto afterA = allocations.load(), afterF = frees.load();
        check(ok && r.statistics().triggered == 160 && r.statistics().stolen == 152 && r.statistics().faults == 0, "dense stealing workload");
        check(a == afterA && f == afterF, "dense callback allocation or destruction");
    });
    test("near-end stop regression", [] {
        const atlas::AudioPlan p(timeline({event(1, 0)})); atlas::AudioRenderer r(p); check(r.play(r.generation()), "play"); advance(r, p.totalFrames() - 1);
        check(r.stop(r.generation()), "stop"); advance(r, 240);
        check(r.state() == atlas::AudioState::ready && r.position() == 0, "stop near plan end must return ready at frame zero");
    });
    test("zero invalid buffers and no callback allocation or destruction", [] {
        const atlas::AudioPlan p(timeline({event(1, 0)})); atlas::AudioRenderer r(p);
        std::array<float, 8194> out; out.fill(.123f);
        const auto a = allocations.load(), f = frees.load();
        bool ok = r.process({}) && r.play(r.generation()); const auto pos = r.position();
        ok = ok && !r.process(std::span<float>(out).first(3)) && !r.process(out) && r.position() == pos;
        for (float v : out) ok = ok && v == .123f;
        ok = ok && r.process(std::span<float>(out).first(8192));
        ok = ok && r.pause(r.generation()); advance(r, 240);
        ok = ok && r.play(r.generation()) && r.stop(r.generation()); advance(r, 240);
        ok = ok && r.seek(1, r.generation()) && r.panic(r.generation());
        const auto afterA = allocations.load(), afterF = frees.load();
        check(ok, "control/buffer semantics"); check(a == afterA && f == afterF, "callback/control allocated or freed");
    });
    test("empty windows and seek clear prior voices", [] {
        const atlas::Timeline t({event(1, 500000)}, atlas::TimeBasis::observed, {}, std::pair<atlas::Microseconds, atlas::Microseconds>{0, 1000000});
        const atlas::AudioPlan p(t); const auto out = render(p, 4096);
        check(p.totalFrames() >= 48000, "requested empty timeline truncated");
        for (std::size_t frame = 0; frame <= 24000; ++frame) check(out[frame * 2] == 0, "sound before onset");
        atlas::AudioRenderer r(p); check(r.play(r.generation()), "play"); advance(r, 25000);
        check(r.pause(r.generation()), "pause"); advance(r, 240);
        check(r.seek(30000, r.generation()), "seek into note tail"); check(r.play(r.generation()), "resume seek");
        std::array<float, 8192> scratch{}; check(r.process(scratch), "seek process");
        check(std::all_of(scratch.begin(), scratch.end(), [](float v) { return v == 0; }), "seek reconstructed earlier note");
        check(r.panic(r.generation()), "panic"); check(!r.seek(p.totalFrames() + 1, r.generation()), "out-of-range seek");
        check(r.seek(p.totalFrames(), r.generation()) && r.state() == atlas::AudioState::finished, "seek end");
        const atlas::Timeline empty({}, atlas::TimeBasis::observed, {}, std::pair<atlas::Microseconds, atlas::Microseconds>{0, 100000});
        const atlas::AudioPlan ep(empty); const auto silent = render(ep, 13);
        check(ep.totalFrames() == 4800 && ep.actions().empty(), "empty duration");
        check(std::all_of(silent.begin(), silent.end(), [](float v) { return v == 0; }), "empty plan sound");
    });
    test("invalid settings rejected", [] {
        const auto t = timeline({event(1, 0)});
        for (int k = 0; k < 10; ++k) {
            atlas::AudioSettings s;
            switch (k) { case 0: s.sampleRate = 22049; break; case 1: s.sampleRate = 192001; break; case 2: s.rootMidi = 47; break; case 3: s.rootMidi = 73; break; case 4: s.attackSeconds = 0; break; case 5: s.decaySeconds = 3.01; break; case 6: s.masterDb = .01; break; case 7: s.masterDb = -61; break; case 8: s.attackSeconds = std::numeric_limits<double>::quiet_NaN(); break; default: s.decaySeconds = std::numeric_limits<double>::infinity(); }
            bool rejected{}; try { const atlas::AudioPlan p(t, s); } catch (const std::invalid_argument&) { rejected = true; } check(rejected, "invalid settings accepted");
        }
    });
    return failed ? 1 : 0;
}
