#include <algorithm>
#include <atlas/audio.hpp>
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
void *allocate(std::size_t n,
               std::size_t alignment = alignof(std::max_align_t)) {
    ++allocations;
    void *p{};
    if (alignment > alignof(std::max_align_t)) {
        const auto size = n ? n : 1;
        if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
            throw std::bad_alloc{};
#if defined(_MSC_VER)
        p = _aligned_malloc(size, alignment);
#else
        p = std::aligned_alloc(alignment, ((size + alignment - 1) / alignment) *
                                              alignment);
#endif
    } else
        p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc{};
    return p;
}
void release(void *p) noexcept {
    if (p)
        ++frees;
    std::free(p);
}
void releaseAligned(void *p, std::align_val_t alignment) noexcept {
#if defined(_MSC_VER)
    if (static_cast<std::size_t>(alignment) > alignof(std::max_align_t)) {
        if (p)
            ++frees;
        _aligned_free(p);
        return;
    }
#else
    static_cast<void>(alignment);
#endif
    release(p);
}
} // namespace
void *operator new(std::size_t n) { return allocate(n); }
void *operator new[](std::size_t n) { return allocate(n); }
void *operator new(std::size_t n, std::align_val_t a) {
    return allocate(n, static_cast<std::size_t>(a));
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return allocate(n, static_cast<std::size_t>(a));
}
void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p, std::size_t) noexcept { release(p); }
void operator delete[](void *p, std::size_t) noexcept { release(p); }
void operator delete(void *p, std::align_val_t a) noexcept {
    releaseAligned(p, a);
}
void operator delete[](void *p, std::align_val_t a) noexcept {
    releaseAligned(p, a);
}
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept {
    releaseAligned(p, a);
}
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept {
    releaseAligned(p, a);
}

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double value, double expected, double tolerance,
          const char *message) {
    check(std::abs(value - expected) <= tolerance, message);
}
atlas::RegionSettings region(std::string id = "region") {
    atlas::RegionSettings r;
    r.id = std::move(id);
    r.name = "Authored region";
    return r;
}
atlas::SpatialSettings spatial() {
    atlas::SpatialSettings s;
    s.enabled = true;
    s.amount = 1;
    s.regions.push_back(region());
    return s;
}
atlas::Event event(std::int64_t id, atlas::Microseconds time,
                   std::optional<atlas::Position> position = atlas::Position{
                       0, 0}) {
    atlas::Event e;
    e.id = "test:" + std::to_string(id);
    e.sourceId = "test";
    e.kind = "acoustic_species_detection";
    e.locationKind = "station_detection";
    e.stationId = "reported:station";
    e.position = position;
    e.observedAt = e.firstSeenAt = time;
    e.revisionSequence = id;
    return e;
}
atlas::Timeline timeline(std::vector<atlas::Event> events,
                         atlas::Microseconds end = 1000000) {
    return atlas::Timeline(
        std::move(events), atlas::TimeBasis::observed, {},
        std::pair<atlas::Microseconds, atlas::Microseconds>{0, end});
}
std::vector<float> render(const atlas::AudioPlan &plan,
                          std::size_t block = 257) {
    atlas::AudioRenderer r(plan);
    check(r.play(r.generation()), "play");
    std::vector<float> output(static_cast<std::size_t>(plan.totalFrames()) * 2);
    for (std::size_t offset = 0; offset < output.size();) {
        const auto count = std::min(block * 2, output.size() - offset);
        check(r.process(std::span<float>(output).subspan(offset, count)),
              "render");
        offset += count;
    }
    check(r.state() == atlas::AudioState::finished &&
              r.statistics().faults == 0,
          "render endpoint/faults");
    return output;
}
void advance(atlas::AudioRenderer &renderer, std::uint64_t frames) {
    std::array<float, 8192> scratch{};
    while (frames) {
        const auto count = std::min<std::uint64_t>(frames, 4096);
        check(renderer.process(std::span<float>(scratch).first(
                  static_cast<std::size_t>(count) * 2)),
              "advance");
        frames -= count;
    }
}
long double binomial(unsigned n, unsigned k) {
    long double result = 1;
    for (unsigned i = 1; i <= k; ++i)
        result *= static_cast<long double>(n - k + i) / i;
    return result;
}
// Closed-form expansion of H(z)=(1-g)z^-D / (1-g*z^-D*(1-a)/(1-a*z^-1)).
// Each feedback traversal adds one delayed low-pass impulse convolution.
double combImpulse(unsigned n, unsigned delay, double feedback, double pole) {
    if (n < delay)
        return 0;
    long double sum = n == delay ? 1 : 0;
    for (unsigned traversals = 1; traversals < n / delay; ++traversals) {
        const auto k = n - (traversals + 1) * delay;
        sum += std::pow(static_cast<long double>(feedback * (1 - pole)),
                        traversals) *
               binomial(k + traversals - 1, traversals - 1) *
               std::pow(static_cast<long double>(pole), k);
    }
    return static_cast<double>((1 - feedback) * sum);
}
double voiceOracle(std::uint64_t age, const atlas::AudioSettings &s) {
    const auto attack = std::ceil(s.attackSeconds * s.sampleRate);
    const auto decay = std::ceil(s.decaySeconds * s.sampleRate);
    const auto release = std::ceil(.020 * s.sampleRate);
    const auto n = static_cast<double>(age);
    if (n >= attack + decay + release)
        return 0;
    const auto a =
        n < attack ? (1 - std::cos(std::numbers::pi * n / attack)) / 2 : 1;
    const auto d =
        std::exp(-std::log(1000.0) * std::max(0.0, n - attack) / decay);
    const auto r = n < attack + decay
                       ? 1
                       : (1 + std::cos(std::numbers::pi * (n - attack - decay) /
                                       release)) /
                             2;
    const auto frequency =
        440 * std::exp2((static_cast<double>(s.rootMidi) - 69) / 12);
    const auto phase = 2 * std::numbers::pi * frequency * n / s.sampleRate;
    return .05 * a * d * r *
           (std::sin(phase) + .25 * std::sin(2 * phase) +
            .08 * std::sin(3 * phase)) /
           1.33;
}
} // namespace
int main() {
    unsigned failures{};
    const auto test = [&](const char *name, auto body) {
        try {
            body();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test(
        "geographic boundaries dateline priority and actual position semantics",
        [] {
            auto s = spatial();
            auto &crossing = s.regions[0];
            crossing.south = -20;
            crossing.north = 20;
            crossing.west = 170;
            crossing.east = -170;
            crossing.pan = .7;
            crossing.positionPan = true;
            s.regions.push_back(region("second"));
            for (const auto longitude :
                 {170.0, 175.0, 180.0, -180.0, -175.0, -170.0}) {
                const auto route = atlas::prepareSpatialRoute(
                    s, atlas::Position{20, longitude});
                check(route.regionSlot == 1,
                      "dateline/inclusive/first-match route");
                const auto offset =
                    longitude >= 170 ? longitude - 170 : longitude + 190;
                const auto angle = (offset / 20) * std::numbers::pi / 2;
                near(route.leftGain, std::cos(angle), 1e-14,
                     "dateline position left pan");
                near(route.rightGain, std::sin(angle), 1e-14,
                     "dateline position right pan");
            }
            check(atlas::prepareSpatialRoute(s, atlas::Position{20.01, 175})
                              .regionSlot == 2 &&
                      atlas::prepareSpatialRoute(s, atlas::Position{0, 0})
                              .regionSlot == 2,
                  "outside region assigned to first rectangle");
            s.regions.pop_back();
            check(atlas::prepareSpatialRoute(s, atlas::Position{0, 0})
                              .regionSlot == 0 &&
                      atlas::prepareSpatialRoute(s, std::nullopt).regionSlot ==
                          0,
                  "missing/outside position fabricated a route");
            const auto unknown = atlas::prepareSpatialRoute(s, std::nullopt);
            near(unknown.leftGain, std::sqrt(.5), 1e-15, "unassigned centre");
            near(unknown.rightGain, std::sqrt(.5), 1e-15, "unassigned centre");
            s.regions[0] = region();
            s.regions[0].positionPan = true;
            const auto west =
                atlas::prepareSpatialRoute(s, atlas::Position{0, -180});
            const auto equivalent =
                atlas::prepareSpatialRoute(s, atlas::Position{0, 180});
            check(west.regionSlot == 1 &&
                      west.leftGain == equivalent.leftGain &&
                      west.rightGain == equivalent.rightGain &&
                      west.leftGain == 1 && west.rightGain == 0,
                  "full-world equivalent meridian convention");
            const auto report = event(1, 123, atlas::Position{0, 175});
            const auto t = timeline({report});
            atlas::AudioSettings audio;
            audio.spatial = s;
            const atlas::AudioPlan plan(t, audio);
            check(plan.actions()[0].eventIndex == 0 &&
                      t.events()[0].stationId == "reported:station" &&
                      t.events()[0].observedAt == 123 &&
                      t.events()[0].position->longitude == 175,
                  "spatial mapping changed source provenance");
        });
    test("strict settings and UTF8 validation including disabled drafts", [] {
        const auto valid = spatial();
        check(atlas::validSpatialSettings(valid), "valid defaults rejected");
        for (unsigned k = 0; k < 23; ++k) {
            auto s = valid;
            s.enabled = false;
            auto &r = s.regions[0];
            switch (k) {
            case 0:
                r.id = "bad id";
                break;
            case 1:
                r.id = std::string(65, 'x');
                break;
            case 2:
                r.name.clear();
                break;
            case 3:
                r.name = std::string("a\0b", 3);
                break;
            case 4:
                r.name = std::string("\xc0\xaf", 2);
                break;
            case 5:
                r.name = std::string("\xed\xa0\x80", 3);
                break;
            case 6:
                r.name = std::string("\xf4\x90\x80\x80", 4);
                break;
            case 7:
                r.name = std::string("\xc2\x80", 2);
                break;
            case 8:
                r.south = r.north;
                break;
            case 9:
                r.west = r.east;
                break;
            case 10:
                r.west = 180;
                r.east = -180;
                break;
            case 11:
                r.pan = 1.01;
                break;
            case 12:
                r.gainDb = .01;
                break;
            case 13:
                r.send = -1;
                break;
            case 14:
                r.echoMix = 1.1;
                break;
            case 15:
                r.delayMs = 19;
                break;
            case 16:
                r.feedback = .51;
                break;
            case 17:
                r.roomSeconds = 2.6;
                break;
            case 18:
                r.tone = std::numeric_limits<double>::quiet_NaN();
                break;
            case 19:
                s.amount = std::numeric_limits<double>::infinity();
                break;
            case 20:
                s.regions.push_back(r);
                break;
            case 21:
                r.name = std::string(129, 'x');
                break;
            default:
                r.north = 91;
                break;
            }
            check(!atlas::validSpatialSettings(s),
                  "invalid disabled spatial draft accepted");
        }
        auto unicode = valid;
        unicode.regions[0].name = "Ōtautahi 🌊";
        check(atlas::validSpatialSettings(unicode), "valid UTF8 name rejected");
        for (unsigned i = 1; i < 9; ++i)
            unicode.regions.push_back(region("region" + std::to_string(i)));
        check(!atlas::validSpatialSettings(unicode), "ninth region accepted");
    });
    test("echo impulse has absolute delay stereo spacing and geometric decay",
         [] {
             for (const auto rate : {22050u, 48000u, 192000u}) {
                 auto s = spatial();
                 s.regions[0].send = 1;
                 s.regions[0].echoMix = 1;
                 s.regions[0].delayMs = 20;
                 s.regions[0].feedback = .5;
                 const auto plan = atlas::prepareSpatialPlan(s, rate, true);
                 atlas::SpatialProcessor processor(plan);
                 const auto leftDelay = (20u * rate + 500u) / 1000u;
                 const auto rightDelay = (22u * rate + 500u) / 1000u;
                 std::array<double, atlas::spatialRegionCapacity> input{};
                 for (unsigned n = 0; n <= 4 * rightDelay; ++n) {
                     input[0] = n == 0 ? .5 : 0;
                     const auto output = processor.process(input);
                     const auto expected = [n](unsigned delay) {
                         return n >= delay && n % delay == 0
                                    ? .25 *
                                          std::pow(.5, static_cast<double>(
                                                           n / delay - 1)) *
                                          std::sqrt(.5)
                                    : 0;
                     };
                     near(output.left, expected(leftDelay), 1e-14,
                          "left echo timing/feedback or hidden direct sound");
                     near(output.right, expected(rightDelay), 1e-14,
                          "right echo timing/feedback or hidden direct sound");
                 }
                 check(processor.faults() == 0, "echo faults");
             }
         });
    test("damped-comb impulse agrees with independent transfer-function "
         "expansion",
         [] {
             for (const auto rate : {22050u, 48000u, 192000u}) {
                 auto s = spatial();
                 auto &r = s.regions[0];
                 r.send = 1;
                 r.echoMix = 0;
                 r.roomSeconds = 2.5;
                 r.tone = 0;
                 const auto plan = atlas::prepareSpatialPlan(s, rate, true);
                 atlas::SpatialProcessor processor(plan);
                 constexpr std::array<unsigned, 4> milliseconds{41, 59, 89,
                                                                127};
                 constexpr std::array<double, 4> left{.4, .3, .2, .1},
                     right{.1, .2, .3, .4};
                 const auto pole = std::exp(-2 * std::numbers::pi * 200 / rate);
                 std::array<double, atlas::spatialRegionCapacity> input{};
                 for (unsigned n = 0; n < rate * 15 / 100; ++n) {
                     input[0] = n == 0 ? .5 : 0;
                     const auto output = processor.process(input);
                     atlas::StereoSample expected;
                     for (unsigned j = 0; j < 4; ++j) {
                         const auto delay =
                             (milliseconds[j] * rate + 500) / 1000;
                         const auto feedback =
                             std::pow(10.0, -3.0 * delay / (rate * 2.5));
                         const auto impulse =
                             .5 * combImpulse(n, delay, feedback, pole) *
                             std::sqrt(.5);
                         expected.left += left[j] * impulse;
                         expected.right += right[j] * impulse;
                     }
                     near(output.left, expected.left, 1e-13,
                          "left room impulse expansion");
                     near(output.right, expected.right, 1e-13,
                          "right room impulse expansion");
                 }
             }
         });
    test("enabled dry panning gain mono and delayed engine output have "
         "absolute oracles",
         [] {
             auto s = atlas::AudioSettings{};
             s.decaySeconds = .1;
             s.spatial = spatial();
             auto &r = s.spatial.regions[0];
             r.pan = -1;
             r.gainDb = -6;
             s.spatial.amount = 0;
             const auto t = timeline({event(1, 0)}, 200000);
             const atlas::AudioPlan dry(t, s);
             check(!dry.spatialPlan().wetActive,
                   "amount zero allocated wet plan");
             const auto output = render(dry);
             const auto gain = std::pow(10.0, -6.0 / 20);
             for (std::size_t n = 0; n < output.size() / 2; ++n) {
                 near(output[n * 2], voiceOracle(n, s) * gain, 2e-7,
                      "hard-left/gain equation");
                 check(output[n * 2 + 1] == 0, "hard-left leaked right");
                 near((output[n * 2] + output[n * 2 + 1]) / 2,
                      voiceOracle(n, s) * gain / 2, 2e-7,
                      "mono average equation");
             }
             r.gainDb = 0;
             r.echoMix = 1;
             r.send = 1;
             r.feedback = 0;
             r.delayMs = 20;
             s.spatial.amount = 1;
             const atlas::AudioPlan wet(t, s);
             const auto echo = render(wet);
             for (std::size_t n = 0; n < 15000; ++n) {
                 const auto expected =
                     (voiceOracle(n, s) +
                      (n >= 960 ? voiceOracle(n - 960, s) : 0)) /
                     2;
                 near(echo[n * 2], expected, 2e-7,
                      "engine dry/send/wet/compensation equation");
                 check(echo[n * 2 + 1] == 0, "hard-left wet leaked right");
             }
             auto silent = region("silent");
             silent.send = 0;
             s.spatial.regions.push_back(silent);
             const atlas::AudioPlan twoConfigured(t, s);
             const auto averaged = render(twoConfigured);
             for (std::size_t n = 0; n < 15000; ++n) {
                 const auto expected =
                     (voiceOracle(n, s) +
                      (n >= 960 ? voiceOracle(n - 960, s) / 2 : 0)) /
                     2;
                 near(averaged[n * 2], expected, 2e-7,
                      "configured silent-send region omitted from wet average");
             }
             s.spatial.regions.pop_back();
             s.spatial.regions[0].pan = 0;
             s.spatial.regions[0].positionPan = true;
             const atlas::AudioPlan positionDryFixedWet(
                 timeline({event(1, 0, atlas::Position{0, -180})}, 200000), s);
             const auto separated = render(positionDryFixedWet);
             for (std::size_t n = 0; n < 15000; ++n) {
                 const auto delayed =
                     n >= 960 ? voiceOracle(n - 960, s) * std::sqrt(.5) : 0;
                 near(separated[n * 2], (voiceOracle(n, s) + delayed) / 2, 2e-7,
                      "position dry pan failed or wet moved with source "
                      "longitude");
                 const auto rightDelayed =
                     n >= 1056 ? voiceOracle(n - 1056, s) * std::sqrt(.5) : 0;
                 near(separated[n * 2 + 1], rightDelayed / 2, 2e-7,
                      "fixed wet pan followed dry longitude or echo-channel "
                      "delay changed");
             }
             auto missing = event(1, 0, std::nullopt);
             const atlas::AudioPlan unassigned(timeline({missing}, 200000), s);
             check(!unassigned.spatialPlan().wetActive,
                   "missing positions fed an effect");
             const auto centre = render(unassigned);
             near(centre[1000], voiceOracle(500, s) * std::sqrt(.5), 2e-7,
                  "unassigned equal-power dry");
         });
    test("voice retirement retains onset route after later region arrivals",
         [] {
             atlas::AudioSettings s;
             s.spatial = spatial();
             s.spatial.amount = 0;
             s.spatial.regions[0].west = -180;
             s.spatial.regions[0].east = 0;
             s.spatial.regions[0].pan = -1;
             auto right = region("right");
             right.west = 0;
             right.east = 180;
             right.pan = 1;
             s.spatial.regions.push_back(right);
             std::vector<atlas::Event> events;
             for (const auto when : {0, 10000, 20000})
                 for (int i = 0; i < 4; ++i)
                     events.push_back(event(
                         static_cast<std::int64_t>(events.size() + 1), when,
                         atlas::Position{0, when == 0 ? -90.0 : 90.0}));
             const atlas::AudioPlan plan(timeline(std::move(events)), s);
             const auto output = render(plan);
             for (unsigned frame = 960; frame < 1200; ++frame) {
                 const auto retirement =
                     (1 + std::cos(std::numbers::pi * (frame - 960) / 240)) / 2;
                 near(output[frame * 2], 4 * voiceOracle(frame, s) * retirement,
                      3e-7, "retiring left voice jumped to latest route");
                 near(output[frame * 2 + 1],
                      4 * (voiceOracle(frame - 480, s) +
                           voiceOracle(frame - 960, s)),
                      3e-7, "right voice route aggregation");
             }
         });
    test(
        "historical multi-region energy normalized and fixed memory bounded",
        [] {
            auto s = spatial();
            s.regions.clear();
            for (unsigned i = 0; i < 8; ++i) {
                auto r = region("region" + std::to_string(i));
                r.pan = static_cast<double>(i) * 2 / 7 - 1;
                r.send = 1;
                r.echoMix = .5;
                r.feedback = .5;
                r.delayMs = 500;
                r.roomSeconds = 2.5;
                s.regions.push_back(r);
            }
            const auto maximum = atlas::prepareSpatialPlan(s, 192000, true);
            atlas::SpatialProcessor maximumProcessor(maximum);
            check(maximumProcessor.allocatedSamples() ==
                          8ULL *
                              (96000 + 99264 + 7872 + 11328 + 17088 + 24384) &&
                      maximumProcessor.allocatedSamples() * sizeof(double) <=
                          16400000,
                  "eight-region delay-memory maximum");
            const auto plan = atlas::prepareSpatialPlan(s, 22050, true);
            atlas::SpatialProcessor processor(plan);
            std::array<double, atlas::spatialRegionCapacity> input{};
            for (unsigned n = 0; n < 44100; ++n) {
                input.fill(0);
                input[(n / 37) % 8] = (n / 1000) % 2 ? .8 : -.8;
                const auto wet = processor.process(input);
                check(std::isfinite(wet.left) && std::isfinite(wet.right) &&
                          std::abs(wet.left) <= .8 && std::abs(wet.right) <= .8,
                      "historical returns exceeded average-wet bound");
                const auto dry = input[(n / 37) % 8];
                const auto left = (dry + s.amount * wet.left) * plan.outputGain;
                const auto right =
                    (dry + s.amount * wet.right) * plan.outputGain;
                check(std::abs(left) <= .800000000001 &&
                          std::abs(right) <= .800000000001 &&
                          std::abs((left + right) / 2) <= .800000000001,
                      "complete stereo/mono bound");
            }
            check(processor.faults() == 0, "bounded multi-region state faults");
        });
    test("logical reset nonfinite recovery and tiny-state policy", [] {
        auto s = spatial();
        s.regions[0].send = 1;
        s.regions[0].echoMix = 1;
        s.regions[0].delayMs = 20;
        const auto plan = atlas::prepareSpatialPlan(s, 48000, true);
        atlas::SpatialProcessor processor(plan), fresh(plan);
        std::array<double, atlas::spatialRegionCapacity> input{};
        input[0] = .5;
        processor.process(input);
        input[0] = 0;
        for (unsigned n = 0; n < 2000; ++n)
            processor.process(input);
        processor.reset();
        for (unsigned n = 0; n < 5000; ++n) {
            input[0] = n == 23 ? .4 : 0;
            const auto a = processor.process(input), b = fresh.process(input);
            check(a.left == b.left && a.right == b.right,
                  "stale delay/filter memory after logical reset");
        }
        input[0] = std::numeric_limits<double>::quiet_NaN();
        const auto invalid = processor.process(input);
        check(invalid.left == 0 && invalid.right == 0 &&
                  processor.faults() == 1,
              "NaN input did not recover observably to silence");
        input[0] = std::numeric_limits<double>::infinity();
        processor.process(input);
        check(processor.faults() == 2, "Inf recovery not observable");
        input[0] = 1e-320;
        processor.process(input);
        input[0] = 0;
        for (unsigned n = 0; n < 5000; ++n) {
            const auto value = processor.process(input);
            check(value.left == 0 && value.right == 0,
                  "subnormal state was retained");
        }
    });
    test("all plan paths reserve effects after possible voice lifetime and "
         "enforce total limit",
         [] {
             atlas::AudioSettings s;
             s.spatial = spatial();
             const auto t = timeline({event(1, 0)}, 1);
             const atlas::AudioPlan legacyWet(t, s);
             const auto voiceEnd = static_cast<std::uint64_t>(
                 std::ceil(.008 * 48000) + std::ceil(.650 * 48000) + 960);
             check(legacyWet.spatialPlan().wetActive &&
                       legacyWet.totalFrames() == voiceEnd + 12ULL * 48000,
                   "legacy mapping skipped spatial compilation/tail");
             s.composition.enabled = true;
             s.composition.tempoBpm = 120;
             s.composition.variation = 0;
             const atlas::AudioPlan composed(t, s);
             check(composed.spatialPlan().wetActive &&
                       composed.totalFrames() == 12960 + 12ULL * 48000,
                   "composition early-return skipped spatial compilation/gate "
                   "tail");
             const std::array<atlas::VoiceProfile, 1> profiles{{{"", {}}}};
             const atlas::AudioPlan editable(t, s, profiles, true);
             check(editable.totalFrames() == 149760 + 12ULL * 48000,
                   "editable maximum voice/effect lifetime");
             s.composition.enabled = false;
             s.spatial.amount = 0;
             const atlas::AudioPlan noAmount(t, s);
             atlas::SpatialProcessor noAmountState(noAmount.spatialPlan());
             check(!noAmount.spatialPlan().wetActive &&
                       noAmountState.allocatedSamples() == 0 &&
                       noAmount.totalFrames() == voiceEnd,
                   "zero amount retained buffers/compensation/tail");
             s.spatial.amount = 1;
             s.spatial.regions[0].send = 0;
             const atlas::AudioPlan noSend(t, s);
             check(!noSend.spatialPlan().wetActive &&
                       noSend.totalFrames() == voiceEnd,
                   "silent-send configuration reserved effects");
             s.spatial.regions[0].send = 1;
             bool rejected{};
             try {
                 const atlas::AudioPlan tooLong(
                     timeline({event(1, 1790000000)}, 1790000001), s);
             } catch (const std::invalid_argument &) {
                 rejected = true;
             }
             check(rejected, "effects beyond 30 minutes accepted");
             s.spatial.enabled = false;
             const atlas::AudioPlan historical(
                 timeline({event(1, 1799900000)}, 1799900001), s);
             check(historical.totalFrames() > 1800ULL * 48000,
                   "disabled spatial changed historical duration semantics");
         });
    test("block parity pause seek stop panic and exact natural final zero", [] {
        atlas::AudioSettings s;
        s.sampleRate = 22050;
        s.spatial = spatial();
        s.spatial.regions[0].send = 1;
        s.spatial.regions[0].roomSeconds = 2.5;
        s.spatial.regions[0].tone = 0;
        const atlas::AudioPlan plan(
            timeline({event(1, 0), event(2, 200000)}, 250000), s);
        const auto reference = render(plan, 1);
        for (const auto block : {17u, 64u, 257u, 4096u})
            check(render(plan, block) == reference,
                  "spatial state depends on host block partition");
        check(reference[reference.size() - 2] == 0 && reference.back() == 0,
              "natural endpoint not exact zero");
        atlas::AudioRenderer paused(plan), uninterrupted(plan);
        check(paused.play(paused.generation()) &&
                  uninterrupted.play(uninterrupted.generation()),
              "pause play");
        advance(paused, 18000);
        advance(uninterrupted, 18000);
        check(paused.pause(paused.generation()), "pause");
        advance(paused, 111);
        advance(uninterrupted, 111);
        const auto position = paused.position();
        advance(paused, 4096);
        check(paused.position() == position, "paused score/DSP advanced");
        check(paused.play(paused.generation()), "resume");
        advance(paused, 111);
        advance(uninterrupted, 111);
        std::array<float, 514> a{}, b{};
        check(paused.process(a) && uninterrupted.process(b) && a == b,
              "pause did not freeze effect histories");
        check(paused.panic(paused.generation()) &&
                  paused.seek(30000, paused.generation()) &&
                  paused.play(paused.generation()),
              "panic/seek/play");
        check(paused.process(a) &&
                  std::all_of(a.begin(), a.end(),
                              [](float value) { return value == 0; }),
              "seek reconstructed pre-seek effects");
        check(paused.stop(paused.generation()), "stop tail");
        advance(paused, 111);
        check(paused.state() == atlas::AudioState::ready &&
                  paused.position() == 0,
              "stop did not clear tail");
    });
    test("nonzero late ambience follows the independently specified final "
         "linear fade",
         [] {
             atlas::AudioSettings s;
             s.sampleRate = 22050;
             s.spatial = spatial();
             s.spatial.regions[0].send = 1;
             s.spatial.regions[0].roomSeconds = 2.5;
             s.spatial.regions[0].tone = 0;
             const atlas::AudioPlan plan(
                 timeline({event(1, 0), event(2, 200000)}, 250000), s);
             const auto output = render(plan);
             atlas::SpatialProcessor wet(plan.spatialPlan());
             std::array<double, atlas::spatialRegionCapacity> input{};
             const auto fadeFrames = 441ULL;
             double latePeak{};
             for (std::uint64_t n = 0; n < plan.totalFrames(); ++n) {
                 input[0] = voiceOracle(n, s) +
                            (n >= 4410 ? voiceOracle(n - 4410, s) : 0);
                 const auto raw = wet.process(input);
                 const auto remaining = plan.totalFrames() - n;
                 if (remaining > fadeFrames)
                     continue;
                 const auto unfaded = raw.left / 2;
                 latePeak = std::max(latePeak, std::abs(unfaded));
                 const auto expected =
                     unfaded * static_cast<double>(remaining - 1) / fadeFrames;
                 near(output[static_cast<std::size_t>(n) * 2], expected,
                      std::max(1e-35, std::abs(expected) * .001),
                      "finite-horizon fade equation");
             }
             check(latePeak > 1e-30 && output.back() == 0,
                   "fade tested only already-silent output");
         });
    test("maximum-region callback and transport allocate and destroy nothing",
         [] {
             atlas::AudioSettings s;
             s.sampleRate = 192000;
             s.spatial = spatial();
             s.spatial.regions.clear();
             for (unsigned i = 0; i < 8; ++i) {
                 auto r = region("r" + std::to_string(i));
                 r.west = -180 + 45.0 * i;
                 r.east = r.west + 45;
                 r.delayMs = 500;
                 r.send = 1;
                 s.spatial.regions.push_back(r);
             }
             std::vector<atlas::Event> events;
             for (unsigned i = 0; i < 64; ++i)
                 events.push_back(
                     event(i + 1, i * 10000,
                           atlas::Position{0, -179.0 + (i % 8) * 45}));
             const atlas::AudioPlan plan(timeline(std::move(events), 650000),
                                         s);
             atlas::AudioRenderer renderer(plan);
             std::array<float, 514> output{};
             const auto beforeAllocations = allocations.load(),
                        beforeFrees = frees.load();
             bool ok = renderer.play(renderer.generation());
             for (unsigned i = 0; i < 600; ++i)
                 ok = renderer.process(output) && ok;
             ok = renderer.panic(renderer.generation()) && ok;
             ok = renderer.seek(1000000, renderer.generation()) && ok;
             ok = renderer.play(renderer.generation()) && ok;
             ok = renderer.process(output) && ok;
             const auto afterAllocations = allocations.load(),
                        afterFrees = frees.load();
             check(ok && beforeAllocations == afterAllocations &&
                       beforeFrees == afterFrees,
                   "spatial callback/reset allocated or freed");
             check(renderer.statistics().faults == 0 &&
                       renderer.statistics().peak <= .8,
                   "maximum-region callback fault/headroom");
         });
    test("spatial disabled retains legacy and composition PCM exactly", [] {
        atlas::AudioSettings s;
        const auto t = timeline({event(1, 0), event(2, 100000)});
        for (const auto composition : {false, true}) {
            s.composition.enabled = composition;
            s.spatial = {};
            const atlas::AudioPlan baseline(t, s);
            s.spatial = spatial();
            s.spatial.enabled = false;
            s.spatial.regions[0].pan = 1;
            s.spatial.regions[0].positionPan = true;
            s.spatial.regions[0].feedback = .5;
            const atlas::AudioPlan disabled(t, s);
            check(baseline.totalFrames() == disabled.totalFrames() &&
                      render(baseline, 257) == render(disabled, 64),
                  "disabled spatial changed historical PCM");
        }
    });
    return failures ? 1 : 0;
}
