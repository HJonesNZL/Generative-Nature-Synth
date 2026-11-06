#include <algorithm>
#include <atlas/audio.hpp>
#include <atlas/presets.hpp>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <set>
#include <sstream>
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
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
atlas::Event event(std::int64_t id, atlas::Microseconds time,
                   std::string species = "birdweather:species:2298") {
    atlas::Event e;
    e.id = "test:" + std::to_string(id);
    e.sourceId = "test";
    e.kind = "acoustic_species_detection";
    e.locationKind = "station_detection";
    e.stationId = "test:station";
    e.speciesId = std::move(species);
    e.revisionSequence = id;
    e.observedAt = e.firstSeenAt = time;
    return e;
}
atlas::Timeline timeline(std::vector<atlas::Event> events,
                         atlas::Microseconds end = 2000000) {
    return atlas::Timeline(
        std::move(events), atlas::TimeBasis::observed, {},
        std::pair<atlas::Microseconds, atlas::Microseconds>{0, end});
}
atlas::AudioSettings settings() {
    atlas::AudioSettings result;
    result.composition.enabled = true;
    result.composition.tempoBpm = 120;
    result.composition.variation = 0;
    result.composition.density = atlas::CompositionDensity::active;
    return result;
}
std::vector<float> render(const atlas::AudioPlan &plan, std::size_t block) {
    atlas::AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "play");
    std::vector<float> output(static_cast<std::size_t>(plan.totalFrames()) * 2);
    for (std::size_t offset = 0; offset < output.size();) {
        const auto count = std::min(block * 2, output.size() - offset);
        check(renderer.process(std::span<float>(output).subspan(offset, count)),
              "render");
        offset += count;
    }
    check(renderer.state() == atlas::AudioState::finished &&
              renderer.statistics().faults == 0,
          "render endpoint or faults");
    return output;
}
bool sameActions(const atlas::AudioPlan &a, const atlas::AudioPlan &b) {
    if (a.totalFrames() != b.totalFrames() ||
        a.actions().size() != b.actions().size())
        return false;
    for (std::size_t i = 0; i < a.actions().size(); ++i) {
        const auto &x = a.actions()[i];
        const auto &y = b.actions()[i];
        if (x.frame != y.frame || x.midiNote != y.midiNote ||
            x.phaseStep != y.phaseStep || x.eventIndex != y.eventIndex ||
            x.velocity != y.velocity || x.gateFrames != y.gateFrames ||
            x.phaseSeed != y.phaseSeed || x.voiceSlot != y.voiceSlot)
            return false;
    }
    return true;
}
double gatedOracle(std::uint64_t age, const atlas::AudioSettings &s) {
    const double n = static_cast<double>(age);
    const double a = std::ceil(s.attackSeconds * s.sampleRate);
    const double decayFrames = std::ceil(s.decaySeconds * s.sampleRate);
    const double releaseFrames = std::ceil(.020 * s.sampleRate);
    const double gateFrames = std::ceil(.218750 * s.sampleRate);
    if (n >= gateFrames + releaseFrames)
        return 0;
    const double attack =
        n < a ? (1 - std::cos(std::numbers::pi * n / a)) / 2 : 1;
    const double decay =
        std::exp(-std::log(1000.0) * std::max(0.0, n - a) / decayFrames);
    const double release =
        n < gateFrames ? 1
                       : (1 + std::cos(std::numbers::pi * (n - gateFrames) /
                                       releaseFrames)) /
                             2;
    const double frequency = 440 * std::exp2((48.0 - 69) / 12);
    const double phase = 2 * std::numbers::pi * frequency * n / s.sampleRate;
    return .05 * .78 * attack * decay * release *
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
    test("scale membership safely normalizes signed note and rejects malformed "
         "controls",
         [] {
             atlas::CompositionSettings s;
             s.tonicPitchClass = 11;
             check(atlas::compositionScaleContains(s, -13) &&
                       atlas::compositionScaleContains(s, 11) &&
                       atlas::compositionScaleContains(
                           s, std::numeric_limits<int>::min()) ==
                           atlas::compositionScaleContains(
                               s, std::numeric_limits<int>::min() % 12 + 24),
                   "signed scale pitch-class normalization");
             s.tonicPitchClass = std::numeric_limits<unsigned>::max();
             check(!atlas::compositionScaleContains(s, -11),
                   "invalid tonic accepted");
         });
    test("absolute phrase oracle, event provenance and independent gated PCM",
         [] {
             const auto t = timeline({event(7, 1001)});
             auto s = settings();
             for (const auto rate : {22050u, 48000u, 192000u}) {
                 s.sampleRate = rate;
                 const atlas::AudioPlan plan(t, s);
                 check(plan.actions().size() == 2 &&
                           plan.decisions().size() == 1,
                       "paired motif");
                 const auto &first = plan.actions()[0];
                 const auto &second = plan.actions()[1];
                 check(first.frame == (125000ULL * rate + 999999) / 1000000 &&
                           second.frame ==
                               (437500ULL * rate + 999999) / 1000000,
                       "absolute paired times");
                 check(first.midiNote == 48 && second.midiNote == 45,
                       "authored scale contour");
                 check(first.velocity == .78 && second.velocity == .78 * .87,
                       "authored unequal accents");
                 check(first.eventIndex == 0 && second.eventIndex == 0 &&
                           t.events()[0].observedAt == 1001 &&
                           t.schedule()[0].dueUs == 1001,
                       "source event/time altered");
                 const auto output = render(plan, 257);
                 for (std::uint64_t n = 0; n < second.frame; ++n) {
                     const auto expected =
                         n < first.frame ? 0 : gatedOracle(n - first.frame, s);
                     check(std::abs(output[static_cast<std::size_t>(n) * 2] -
                                    expected) < 2e-7,
                           "velocity/gate equation mismatch");
                 }
             }
         });
    test("tempo, quantization and density have absolute musical effects", [] {
        const auto t = timeline({event(1, 1001)});
        auto s = settings();
        const atlas::AudioPlan p120(t, s);
        s.composition.tempoBpm = 60;
        const atlas::AudioPlan p60(t, s);
        check(p60.actions()[0].frame == 2 * p120.actions()[0].frame &&
                  p60.actions()[1].frame == 2 * p120.actions()[1].frame,
              "tempo did not scale score times");
        s.composition.quantize = false;
        const atlas::AudioPlan unquantized(t, s);
        check(unquantized.actions()[0].frame == 49,
              "unquantized event ceil time");
        const auto dense =
            timeline({event(1, 0, "birdweather:species:1276"),
                      event(2, 30000, "birdweather:species:1276"),
                      event(3, 60000, "birdweather:species:1276"),
                      event(4, 90000, "birdweather:species:1276")});
        s = settings();
        s.composition.quantize = false;
        for (const auto density : {atlas::CompositionDensity::sparse,
                                   atlas::CompositionDensity::balanced,
                                   atlas::CompositionDensity::active}) {
            s.composition.density = density;
            const atlas::AudioPlan plan(dense, s);
            const auto expected =
                density == atlas::CompositionDensity::sparse     ? 1u
                : density == atlas::CompositionDensity::balanced ? 4u
                                                                 : 16u;
            check(plan.actions().size() == expected &&
                      plan.decisions().size() == 4,
                  "event or motif density limit");
        }
    });
    test(
        "all exact candidates have distinct bounded motifs and honest fallback",
        [] {
            auto s = settings();
            std::set<std::string> signatures;
            unsigned named{};
            for (const auto &preset : atlas::speciesPresets()) {
                const atlas::AudioPlan plan(
                    timeline({event(1, 0, std::string(preset.speciesId))}), s);
                check(!plan.actions().empty() && plan.actions().size() <= 4,
                      "per-event note bound");
                std::ostringstream signature;
                for (const auto &note : plan.actions()) {
                    check(note.eventIndex == 0 && note.midiNote >= 36 &&
                              note.midiNote <= 76 && note.velocity > 0 &&
                              note.velocity <= 1 && note.gateFrames > 0,
                          "motif provenance/pitch/dynamics bounds");
                    signature << note.frame << ':' << note.midiNote << ':'
                              << note.gateFrames << ':' << note.velocity << ';';
                }
                if (preset.evidenceStatus == "unresolved_taxon") {
                    const atlas::AudioPlan unknown(
                        timeline({event(1, 0, "unknown:taxon")}), s);
                    check(plan.actions().size() == 1 &&
                              unknown.actions().size() == 1 &&
                              plan.actions()[0].midiNote ==
                                  unknown.actions()[0].midiNote &&
                              plan.actions()[0].gateFrames ==
                                  unknown.actions()[0].gateFrames &&
                              plan.actions()[0].velocity ==
                                  unknown.actions()[0].velocity,
                          "unresolved bat gained invented animal phrase");
                } else {
                    ++named;
                    check(signatures.insert(signature.str()).second,
                          "named taxa collapsed to same phrase");
                }
            }
            check(named == 32 && signatures.size() == 32,
                  "exact named catalogue coverage");
            const atlas::AudioPlan foreground(
                timeline({event(1, 0, "birdweather:species:1276")}), s);
            const atlas::AudioPlan background(
                timeline({event(1, 0, "birdweather:species:4452")}), s);
            check(background.actions()[0].velocity <
                          foreground.actions()[0].velocity &&
                      background.actions()[0].midiNote >
                          foreground.actions()[0].midiNote,
                  "authored foreground/background orchestration");
        });
    test("every scale/tonic keeps default and edited voices in key", [] {
        std::vector<atlas::VoiceProfile> profiles{{"", {}}};
        std::vector<atlas::Event> events;
        for (const auto &preset : atlas::speciesPresets()) {
            profiles.push_back(
                {std::string(preset.speciesId), preset.settings});
            events.push_back(
                event(static_cast<std::int64_t>(events.size() + 1),
                      static_cast<atlas::Microseconds>(events.size()) * 2000000,
                      std::string(preset.speciesId)));
        }
        const auto t = timeline(std::move(events), 70000000);
        auto s = settings();
        for (const auto scale : {atlas::CompositionScale::majorPentatonic,
                                 atlas::CompositionScale::minorPentatonic,
                                 atlas::CompositionScale::major,
                                 atlas::CompositionScale::naturalMinor}) {
            s.composition.scale = scale;
            for (unsigned tonic = 0; tonic < 12; ++tonic) {
                s.composition.tonicPitchClass = tonic;
                const atlas::AudioPlan plan(t, s, profiles, true);
                atlas::AudioRenderer renderer(plan);
                for (const auto &note : plan.actions()) {
                    check(atlas::compositionScaleContains(
                              s.composition, static_cast<int>(note.midiNote)),
                          "base action outside scale");
                    atlas::VoiceCoefficients coefficients;
                    check(plan.prepareVoiceUpdate(
                              note.voiceSlot, profiles[note.voiceSlot].settings,
                              coefficients),
                          "prepare candidate pitch table");
                    const auto projected =
                        static_cast<double>(note.midiNote) +
                        12 *
                            std::log2(
                                coefficients
                                    .scalePitchMultipliers[note.midiNote % 12]);
                    const auto nearest =
                        static_cast<int>(std::llround(projected));
                    check(
                        std::abs(projected - nearest) < 1e-10 &&
                            nearest <= 88 &&
                            atlas::compositionScaleContains(s.composition,
                                                            nearest),
                        "candidate transpose broke scene key or pitch maximum");
                    check(renderer.updateVoice(note.voiceSlot, coefficients),
                          "candidate coefficient rejection");
                }
                for (int tuning = -12; tuning <= 12; ++tuning) {
                    auto voice = profiles[1].settings;
                    voice.tuningSemitones = tuning;
                    atlas::VoiceCoefficients coefficients;
                    check(plan.prepareVoiceUpdate(1, voice, coefficients) &&
                              renderer.updateVoice(1, coefficients),
                          "scale-constrained live tuning rejected");
                    for (const auto &note : plan.actions()) {
                        if (note.voiceSlot != 1)
                            continue;
                        const auto projected = static_cast<int>(std::llround(
                            static_cast<double>(note.midiNote) +
                            12 * std::log2(coefficients.scalePitchMultipliers
                                               [note.midiNote % 12])));
                        check(projected <= 88 &&
                                  atlas::compositionScaleContains(s.composition,
                                                                  projected),
                              "live tuning broke scene key");
                    }
                }
            }
        }
    });
    test("seeds ordering and block partitions are reproducible", [] {
        std::vector<atlas::Event> events;
        for (int i = 0; i < 20; ++i)
            events.push_back(
                event(i + 1, i * 20000, "birdweather:species:420"));
        const auto t = timeline(events);
        std::reverse(events.begin(), events.end());
        const auto reversed = timeline(std::move(events));
        auto s = settings();
        s.composition.variation = 1;
        const atlas::AudioPlan reference(t, s), repeated(t, s),
            reordered(reversed, s);
        check(sameActions(reference, repeated) &&
                  sameActions(reference, reordered),
              "same source/seed produced different score or ordering");
        s.composition.seed = 123456;
        const atlas::AudioPlan varied(t, s);
        check(!sameActions(reference, varied) &&
                  render(reference, 257) != render(varied, 257),
              "variation seed has no musical effect");
        const auto output = render(reference, 1);
        for (const auto block : {17u, 64u, 257u, 4096u})
            check(render(reference, block) == output,
                  "composition depends on host partition");
        std::array<unsigned, 20> count{};
        std::uint64_t previous{};
        for (const auto &note : reference.actions()) {
            check(note.frame >= previous && note.eventIndex < count.size(),
                  "numeric ordering/index");
            previous = note.frame;
            ++count[note.eventIndex];
        }
        check(std::all_of(count.begin(), count.end(),
                          [](auto value) { return value <= 4; }),
              "per-event maximum exceeded");
    });
    test(
        "bounded dense callback, onset snapshots and malformed coefficient "
        "defence",
        [] {
            auto s = settings();
            s.composition.variation = 1;
            std::vector<atlas::Event> events;
            for (int i = 0; i < 100; ++i)
                events.push_back(
                    event(i + 1, i * 10000, "birdweather:species:2561"));
            const auto *preset =
                atlas::speciesPreset("candidate:birdweather:species:2561:v1");
            check(preset != nullptr, "test preset");
            const std::array<atlas::VoiceProfile, 2> profiles{
                {{"", {}}, {std::string(preset->speciesId), preset->settings}}};
            const atlas::AudioPlan plan(timeline(std::move(events)), s,
                                        profiles, true);
            atlas::VoiceCoefficients coefficients;
            auto edited = profiles[1].settings;
            edited.tuningSemitones = 7;
            edited.decaySeconds = 3;
            check(plan.prepareVoiceUpdate(1, edited, coefficients),
                  "prepare live tuning");
            atlas::AudioRenderer renderer(plan);
            auto malformed = coefficients;
            malformed.scalePitchMultipliers[0] =
                std::numeric_limits<double>::quiet_NaN();
            check(!renderer.updateVoice(1, malformed),
                  "NaN scale table accepted");
            malformed = coefficients;
            malformed.scalePitchMultipliers[11] = 3;
            check(!renderer.updateVoice(1, malformed),
                  "unbounded scale multiplier accepted");
            malformed = coefficients;
            malformed.scaleQuantized = false;
            check(!renderer.updateVoice(1, malformed),
                  "scale mode mismatch accepted");
            std::array<float, 514> output{};
            const auto beforeAllocations = allocations.load(),
                       beforeFrees = frees.load();
            bool ok = renderer.play(renderer.generation());
            ok = renderer.process(output) && ok;
            ok = renderer.updateVoice(1, coefficients) && ok;
            while (renderer.state() != atlas::AudioState::finished)
                ok = renderer.process(output) && ok;
            const auto afterAllocations = allocations.load(),
                       afterFrees = frees.load();
            check(ok && beforeAllocations == afterAllocations &&
                      beforeFrees == afterFrees,
                  "composition callback/update allocated or freed");
            check(renderer.statistics().faults == 0 &&
                      renderer.statistics().peak <= .8 &&
                      renderer.statistics().triggered == plan.actions().size(),
                  "bounded dense rendering");
            const atlas::AudioPlan snapshotPlan(
                timeline({event(1, 0, "birdweather:species:2561")}), s,
                profiles, true);
            atlas::AudioRenderer old(snapshotPlan), changed(snapshotPlan);
            check(old.play(old.generation()) &&
                      changed.play(changed.generation()),
                  "snapshot play");
            std::array<float, 200> a{}, b{};
            check(old.process(a) && changed.process(b) && a == b,
                  "snapshot start");
            check(changed.updateVoice(1, coefficients), "snapshot update");
            check(old.process(a) && changed.process(b) && a == b,
                  "running voice changed after tuning edit");
        });
    test(
        "full source capacity, overload decisions, tails and settings "
        "boundaries",
        [] {
            std::vector<atlas::Event> events;
            events.reserve(50000);
            for (int i = 0; i < 50000; ++i)
                events.push_back(event(i + 1, 0, "birdweather:species:102"));
            auto s = settings();
            const atlas::AudioPlan full(timeline(std::move(events)), s);
            check(full.decisions().size() == 50000 &&
                      full.actions().size() <= 16 &&
                      full.actions().size() <= atlas::compositionActionCapacity,
                  "full source/action capacity");
            const auto admitted = std::count_if(
                full.decisions().begin(), full.decisions().end(),
                [](const auto &decision) {
                    return decision.reason == atlas::Admission::accepted;
                });
            check(admitted == 4,
                  "overload ledger lost admitted source identities");
            s.composition.quantize = false;
            const auto edge =
                timeline({event(1, 1799730000, "unknown")}, 1799730001);
            const atlas::AudioPlan fits(edge, s);
            check(fits.totalFrames() == 1800ULL * 48000,
                  "inclusive tail boundary");
            bool rejected{};
            try {
                const atlas::AudioPlan exceeds(
                    timeline({event(1, 1799730001, "unknown")}, 1799730002), s);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "tail beyond 30 minute limit accepted");
            rejected = false;
            try {
                const std::array<atlas::VoiceProfile, 1> profiles{{{"", {}}}};
                const atlas::AudioPlan editable(edge, s, profiles, true);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "editable worst-case voice tail unbounded");
            for (unsigned k = 0; k < 7; ++k) {
                auto invalid = settings();
                switch (k) {
                case 0:
                    invalid.composition.tonicPitchClass = 12;
                    break;
                case 1:
                    invalid.composition.tempoBpm = 39;
                    break;
                case 2:
                    invalid.composition.tempoBpm = 181;
                    break;
                case 3:
                    invalid.composition.variation =
                        std::numeric_limits<double>::quiet_NaN();
                    break;
                case 4:
                    invalid.composition.variation = 1.01;
                    break;
                case 5:
                    invalid.composition.scale =
                        static_cast<atlas::CompositionScale>(99);
                    break;
                default:
                    invalid.composition.density =
                        static_cast<atlas::CompositionDensity>(99);
                    break;
                }
                rejected = false;
                try {
                    const atlas::AudioPlan plan(timeline({event(1, 0)}),
                                                invalid);
                } catch (const std::invalid_argument &) {
                    rejected = true;
                }
                check(rejected, "malformed composition settings accepted");
            }
        });
    test("disabled composition settings preserve legacy PCM exactly", [] {
        const auto t =
            timeline({event(1, 0), event(2, 123456), event(3, 300000)});
        atlas::AudioSettings legacy;
        const atlas::AudioPlan a(t, legacy);
        legacy.composition.scale = atlas::CompositionScale::naturalMinor;
        legacy.composition.tonicPitchClass = 11;
        legacy.composition.tempoBpm = 180;
        legacy.composition.seed = 0xffffffffu;
        legacy.composition.variation = 1;
        legacy.composition.quantize = false;
        const atlas::AudioPlan b(t, legacy);
        check(sameActions(a, b) && render(a, 257) == render(b, 64),
              "disabled score changed legacy output");
    });
    return failures ? 1 : 0;
}
