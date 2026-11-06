#include <algorithm>
#include <array>
#include <atlas/audio.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
thread_local bool callback{};
std::size_t callbackAllocations{}, callbackFrees{};
void *allocate(std::size_t bytes,
               std::size_t alignment = alignof(std::max_align_t)) {
    if (callback)
        ++callbackAllocations;
    void *result{};
    if (alignment > alignof(std::max_align_t)) {
        const auto size = bytes ? bytes : 1;
        if (size > std::numeric_limits<std::size_t>::max() - alignment + 1)
            throw std::bad_alloc{};
#if defined(_MSC_VER)
        result = _aligned_malloc(size, alignment);
#else
        result = std::aligned_alloc(
            alignment, ((size + alignment - 1) / alignment) * alignment);
#endif
    } else
        result = std::malloc(bytes ? bytes : 1);
    if (!result)
        throw std::bad_alloc{};
    return result;
}
void release(void *pointer) noexcept {
    if (callback && pointer)
        ++callbackFrees;
    std::free(pointer);
}
void releaseAligned(void *pointer, std::align_val_t alignment) noexcept {
#if defined(_MSC_VER)
    if (static_cast<std::size_t>(alignment) > alignof(std::max_align_t)) {
        if (callback && pointer)
            ++callbackFrees;
        _aligned_free(pointer);
        return;
    }
#else
    static_cast<void>(alignment);
#endif
    release(pointer);
}
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
constexpr std::array characters{
    atlas::VoiceCharacter::struck, atlas::VoiceCharacter::whistle,
    atlas::VoiceCharacter::reed,   atlas::VoiceCharacter::bell,
    atlas::VoiceCharacter::air,    atlas::VoiceCharacter::pulse};
atlas::Event event(int id = 1, atlas::Microseconds due = 0,
                   std::string species = "tone:1") {
    atlas::Event result;
    result.id = "character:" + std::to_string(id);
    result.sourceId = "character";
    result.kind = "acoustic_species_detection";
    result.locationKind = "station_detection";
    result.stationId = "character:station";
    result.speciesId = std::move(species);
    result.observedAt = result.firstSeenAt = due;
    result.revisionSequence = id;
    return result;
}
atlas::AudioPlan planFor(const atlas::VoiceSettings &voice,
                         std::uint32_t rate = 48000, unsigned root = 60,
                         std::vector<atlas::Event> events = {event()}) {
    const auto species = *events.front().speciesId;
    const std::array<atlas::VoiceProfile, 2> profiles{
        {{"", {}}, {species, voice}}};
    atlas::AudioSettings settings;
    settings.sampleRate = rate;
    settings.rootMidi = root;
    return atlas::AudioPlan(
        atlas::Timeline(std::move(events), atlas::TimeBasis::observed),
        settings, profiles, true);
}
std::vector<float> render(const atlas::AudioPlan &plan,
                          std::size_t block = 257) {
    atlas::AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "play");
    std::vector<float> result(static_cast<std::size_t>(plan.totalFrames()) * 2);
    for (std::size_t position = 0; position < result.size();) {
        const auto count = std::min(block * 2, result.size() - position);
        check(
            renderer.process(std::span<float>(result).subspan(position, count)),
            "process");
        position += count;
    }
    check(renderer.state() == atlas::AudioState::finished &&
              renderer.statistics().faults == 0,
          "natural finish or numerical fault");
    return result;
}
// Closed-form reference: no recursive oscillator/envelope from the renderer.
long double oracle(std::uint64_t age, unsigned mappedMidi, std::uint32_t rate,
                   const atlas::VoiceSettings &settings,
                   const atlas::VoiceCoefficients &c, std::uint32_t seed) {
    const auto n = static_cast<long double>(age);
    const auto attack = std::ceil(settings.attackSeconds * rate);
    const auto decay = std::ceil(settings.decaySeconds * rate);
    const auto releaseFrames = std::ceil(.020 * rate);
    if (n >= attack + decay + releaseFrames)
        return 0;
    const auto pi = std::numbers::pi_v<long double>;
    const auto attackGain =
        n < attack ? (1 - std::cos(pi * n / attack)) / 2 : 1;
    const auto decayGain = std::pow(1000.L, -std::max(0.L, n - attack) / decay);
    const auto releaseGain =
        n < attack + decay
            ? 1
            : (1 + std::cos(pi * (n - attack - decay) / releaseFrames)) / 2;
    const auto frequency =
        440.L * std::exp2((static_cast<long double>(mappedMidi) +
                           settings.tuningSemitones - 69) /
                          12);
    long double phaseIntegral = n;
    if (c.sweepFrames) {
        const auto count =
            std::min(age, static_cast<std::uint64_t>(c.sweepFrames - 1));
        const auto logRatio =
            static_cast<long double>(settings.sweepSemitones) /
            (12 * static_cast<long double>(c.sweepFrames - 1)) * std::log(2.L);
        phaseIntegral =
            std::expm1(logRatio * static_cast<long double>(count)) /
                std::expm1(logRatio) +
            static_cast<long double>(age - count) *
                std::exp2(static_cast<long double>(settings.sweepSemitones) /
                          12);
    }
    const auto phase = 2 * pi * frequency * phaseIntegral / rate;
    long double tone{};
    if (c.legacyStruck) {
        tone = (std::sin(phase) + .25L * std::sin(2 * phase) +
                .08L * std::sin(3 * phase)) /
               1.33L;
    } else {
        for (std::size_t i = 0; i < c.partialCount; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            const auto initial = static_cast<long double>(seed) / 4294967296.L *
                                 2 * pi * c.phaseSpread;
            tone +=
                c.partials[i].amplitude *
                std::pow(static_cast<long double>(c.partials[i].decayFactor),
                         n) *
                std::sin(initial + c.partials[i].ratio * phase);
        }
    }
    const auto pulsePhase = 2 * pi * settings.pulseHz * n / rate;
    const auto pulse =
        settings.pulseHz == 0 ? 1 : std::pow((1 + std::cos(pulsePhase)) / 2, 2);
    const auto roughPhase = 2 * pi * (29 + 31 * settings.brightness) * n / rate;
    const auto rough = 1 - settings.roughness / 2 +
                       settings.roughness / 2 * std::cos(roughPhase);
    return .05L * tone * attackGain * decayGain * releaseGain * pulse * rough *
           std::pow(10.L, settings.gainDb / 20);
}
double distance(const std::vector<float> &a, const std::vector<float> &b) {
    check(a.size() == b.size(), "comparison shape");
    double energyA{}, energyB{}, cross{};
    for (std::size_t i = 0; i < a.size(); i += 2) {
        energyA += a[i] * static_cast<double>(a[i]);
        energyB += b[i] * static_cast<double>(b[i]);
        cross += a[i] * static_cast<double>(b[i]);
    }
    check(energyA > 0 && energyB > 0, "silent comparison");
    return 1 - cross / std::sqrt(energyA * energyB);
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

int main() {
    static_assert(std::is_trivially_copyable_v<atlas::VoiceCoefficients>);
    static_assert(std::is_trivially_copyable_v<atlas::MusicalAction>);
    unsigned failed{};
    const auto test = [&](const char *name, auto body) {
        try {
            body();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    test(
        "independent closed-form oscillator, modal decay and gesture oracle",
        [] {
            for (const auto character : characters) {
                for (const auto sweep : {-7.0, 0.0, 9.0}) {
                    atlas::VoiceSettings settings;
                    settings.character = character;
                    settings.attackSeconds = .013;
                    settings.decaySeconds = .37;
                    settings.gainDb = -6;
                    settings.brightness = .61;
                    settings.roughness = .73;
                    settings.pulseHz = 5.3;
                    settings.sweepSemitones = sweep;
                    const auto plan = planFor(settings);
                    atlas::VoiceCoefficients c;
                    check(plan.prepareVoiceUpdate(1, settings, c), "prepare");
                    const auto output = render(plan);
                    const auto &action = plan.actions().front();
                    for (std::uint64_t n = 0; n < plan.totalFrames(); n += 31) {
                        const auto expected =
                            oracle(n, action.midiNote, 48000, settings, c,
                                   action.phaseSeed);
                        check(std::abs(
                                  static_cast<long double>(
                                      output[static_cast<std::size_t>(n) * 2]) -
                                  expected) < 2e-7L,
                              "closed-form signal mismatch");
                    }
                }
            }
        });
    test("exact neutral legacy dispatch and independent legacy equation", [] {
        const atlas::VoiceSettings settings;
        const auto plan = planFor(settings);
        atlas::VoiceCoefficients c;
        check(plan.prepareVoiceUpdate(1, settings, c) && c.legacyStruck,
              "neutral struck must use historical branch");
        const auto output = render(plan);
        const auto &action = plan.actions().front();
        for (std::uint64_t n = 0; n < plan.totalFrames(); n += 11)
            check(std::abs(static_cast<long double>(
                               output[static_cast<std::size_t>(n) * 2]) -
                           oracle(n, action.midiNote, 48000, settings, c,
                                  action.phaseSeed)) < 2e-7L,
                  "legacy equation");
    });
    test("same-register spectra differ without changing old envelope controls",
         [] {
             std::vector<std::vector<float>> signals;
             for (const auto character : characters) {
                 atlas::VoiceSettings settings;
                 settings.character = character;
                 const auto plan = planFor(settings);
                 atlas::VoiceCoefficients c;
                 check(plan.prepareVoiceUpdate(1, settings, c),
                       "prepare contrast");
                 if (character == atlas::VoiceCharacter::reed)
                     for (std::size_t i = 0; i < c.partialCount; ++i)
                         check(std::fmod(c.partials[i].ratio, 2) == 1,
                               "reed odd harmonic structure");
                 if (character == atlas::VoiceCharacter::bell)
                     check(c.partials[1].ratio !=
                                   std::round(c.partials[1].ratio) &&
                               c.partials[1].decayFactor <
                                   c.partials[0].decayFactor,
                           "inharmonic independently decaying bell modes");
                 if (character == atlas::VoiceCharacter::air)
                     check(c.phaseSpread == 1 && c.partials.front().ratio < 1,
                           "diffuse multisine character");
                 signals.push_back(render(plan));
             }
             for (std::size_t i = 0; i < signals.size(); ++i)
                 for (std::size_t j = i + 1; j < signals.size(); ++j)
                     check(distance(signals[i], signals[j]) > .005,
                           "characters differ only negligibly after level "
                           "normalisation");
         });
    test("every macro changes observable behaviour and zero gestures remain "
         "neutral",
         [] {
             atlas::VoiceSettings settings;
             settings.character = atlas::VoiceCharacter::reed;
             const auto neutral = render(planFor(settings));
             for (int parameter = 0; parameter < 4; ++parameter) {
                 auto changed = settings;
                 if (parameter == 0)
                     changed.brightness = 0;
                 if (parameter == 1)
                     changed.roughness = 1;
                 if (parameter == 2)
                     changed.pulseHz = 8;
                 if (parameter == 3)
                     changed.sweepSemitones = -9;
                 check(distance(neutral, render(planFor(changed))) > .001,
                       "macro does not change the signal");
             }
         });
    test("stable event identity and revision seeds, replay and partition "
         "invariance",
         [] {
             atlas::VoiceSettings settings;
             settings.character = atlas::VoiceCharacter::air;
             settings.roughness = .4;
             settings.pulseHz = 7;
             settings.sweepSemitones = 5;
             const auto plan = planFor(settings);
             check(plan.actions().front().phaseSeed == 0x42cfc8dbu,
                   "known independent FNV fixture");
             const auto reference = render(plan, 1);
             for (const auto block : {17u, 64u, 257u, 4096u})
                 check(render(plan, block) == reference,
                       "block partition changes samples");
             auto changedRevision = event();
             changedRevision.revisionSequence = 101;
             const auto revisionPlan =
                 planFor(settings, 48000, 60, {changedRevision});
             check(revisionPlan.actions().front().phaseSeed !=
                           plan.actions().front().phaseSeed &&
                       render(revisionPlan) != reference,
                   "revision does not affect phase seed");
             atlas::AudioRenderer renderer(plan);
             std::array<float, 2048> first{}, replay{};
             check(renderer.play(renderer.generation()) &&
                       renderer.process(first),
                   "initial prefix");
             check(renderer.panic(renderer.generation()) &&
                       renderer.play(renderer.generation()) &&
                       renderer.process(replay) && first == replay,
                   "panic replay not deterministic");
         });
    test("coefficient frequency admission and all-rate endpoint numerical "
         "bounds",
         [] {
             for (const auto rate : {22050u, 22051u, 32000u, 44100u, 48000u,
                                     96000u, 123457u, 192000u}) {
                 for (const auto character : characters) {
                     for (const auto sweep : {-12.0, 12.0}) {
                         atlas::VoiceSettings settings;
                         settings.character = character;
                         settings.attackSeconds = sweep > 0 ? .100 : .002;
                         settings.decaySeconds = sweep > 0 ? 3 : .100;
                         settings.brightness = 1;
                         settings.roughness = 1;
                         settings.pulseHz = 16;
                         settings.sweepSemitones = sweep;
                         const auto plan = planFor(settings, rate, 72,
                                                   {event(1, 0, "tone:4")});
                         check(plan.actions().front().midiNote == 88,
                               "highest mapped MIDI fixture");
                         atlas::VoiceCoefficients c;
                         check(plan.prepareVoiceUpdate(1, settings, c),
                               "extreme preparation");
                         double sum{};
                         const auto highestFrequency =
                             440 * std::exp2((88.0 - 69) / 12) *
                             std::max(1.0, std::exp2(sweep / 12));
                         for (std::size_t i = 0; i < c.partialCount; ++i) {
                             const auto modulationHz =
                                 (c.roughnessPhaseStep + 2 * c.pulsePhaseStep) *
                                 rate / (2 * std::numbers::pi);
                             check(highestFrequency * c.partials[i].ratio +
                                           modulationHz <=
                                       .45 * rate + 1e-8,
                                   "oscillator plus AM exceeds admitted band");
                             sum += std::abs(c.partials[i].amplitude);
                         }
                         check(sum <= 1.000000000001, "weight bound");
                         const auto output = render(plan, 4096);
                         for (const auto value : output)
                             check(std::isfinite(value) &&
                                       std::abs(value) <= .050001f,
                                   "single-state bound");
                     }
                 }
             }
         });
    test("prepared updates captured only at the next onset", [] {
        atlas::VoiceSettings before;
        before.character = atlas::VoiceCharacter::whistle;
        auto after = before;
        after.character = atlas::VoiceCharacter::bell;
        after.roughness = .8;
        after.pulseHz = 9;
        after.sweepSemitones = -6;
        const auto plan =
            planFor(before, 48000, 60, {event(1), event(2, 200000)});
        atlas::VoiceCoefficients firstCoefficients, secondCoefficients;
        check(plan.prepareVoiceUpdate(1, before, firstCoefficients) &&
                  plan.prepareVoiceUpdate(1, after, secondCoefficients),
              "prepare onset update");
        const auto reference = render(plan);
        atlas::AudioRenderer renderer(plan);
        check(renderer.play(renderer.generation()), "play onset update");
        std::vector<float> output(reference.size());
        check(renderer.process(std::span<float>(output).first(2000)), "prefix");
        check(renderer.updateVoice(1, secondCoefficients), "update");
        for (std::size_t pos = 2000; pos < output.size();) {
            const auto count = std::min<std::size_t>(514, output.size() - pos);
            check(
                renderer.process(std::span<float>(output).subspan(pos, count)),
                "remainder");
            pos += count;
        }
        const auto onset = plan.actions()[1].frame;
        for (std::size_t n = 0; n < onset; ++n)
            check(output[n * 2] == reference[n * 2],
                  "active voice changed on update");
        check(output != reference, "next onset did not change");
        for (std::uint64_t n = onset; n < plan.totalFrames(); n += 71) {
            const auto expected =
                oracle(n, 60, 48000, before, firstCoefficients,
                       plan.actions()[0].phaseSeed) +
                oracle(n - onset, 60, 48000, after, secondCoefficients,
                       plan.actions()[1].phaseSeed);
            check(std::abs(static_cast<long double>(
                               output[static_cast<std::size_t>(n) * 2]) -
                           expected) < 2e-7L,
                  "onset snapshot mixture oracle");
        }
    });
    test("invalid new settings and hostile POD updates retain valid bank", [] {
        atlas::VoiceSettings settings;
        settings.character = atlas::VoiceCharacter::reed;
        const auto plan = planFor(settings);
        atlas::VoiceCoefficients valid;
        check(plan.prepareVoiceUpdate(1, settings, valid),
              "prepare valid update");
        for (int field = 0; field < 5; ++field) {
            for (const auto value :
                 {-std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(), -99.0, 99.0}) {
                auto invalid = settings;
                if (field == 0)
                    invalid.character = static_cast<atlas::VoiceCharacter>(99);
                if (field == 1)
                    invalid.brightness = value;
                if (field == 2)
                    invalid.roughness = value;
                if (field == 3)
                    invalid.pulseHz = value;
                if (field == 4)
                    invalid.sweepSemitones = value;
                atlas::VoiceCoefficients destination;
                check(!atlas::validVoiceSettings(invalid) &&
                          !plan.prepareVoiceUpdate(1, invalid, destination),
                      "invalid setting accepted");
            }
        }
        atlas::AudioRenderer renderer(plan);
        for (int field = 0; field < 9; ++field) {
            auto bad = valid;
            if (field == 0)
                bad.partialCount = 9;
            if (field == 1)
                bad.partials[0].ratio = 1000;
            if (field == 2)
                bad.partials[0].amplitude = 2;
            if (field == 3)
                bad.partials[0].decayFactor = 2;
            if (field == 4)
                bad.sweepFactor = 2;
            if (field == 5)
                bad.phaseSpread = std::numeric_limits<double>::quiet_NaN();
            if (field == 6)
                bad.pulsePhaseStep = 1;
            if (field == 7)
                bad.roughnessDepth = 2;
            if (field == 8)
                bad.sweepEnd = 3;
            check(!renderer.updateVoice(1, bad),
                  "hostile coefficient snapshot accepted");
        }
        const auto reference = render(plan);
        check(renderer.play(renderer.generation()),
              "play after invalid update");
        std::array<float, 128> output{};
        check(renderer.process(output), "render after rejected update");
        check(std::equal(output.begin(), output.end(), reference.begin()),
              "rejected update changed bank");
    });
    test("transposed note bounds and root-specific POD pitch rejection", [] {
        for (const auto transpose : {-12, 12}) {
            atlas::VoiceSettings settings;
            settings.character = atlas::VoiceCharacter::reed;
            settings.tuningSemitones = transpose;
            settings.sweepSemitones = 12;
            settings.pulseHz = 16;
            const auto plan =
                planFor(settings, 22050, 60,
                        {event(1, 0, transpose < 0 ? "tone:1" : "tone:4")});
            check(static_cast<int>(plan.actions().front().midiNote) +
                          transpose ==
                      (transpose < 0 ? 48 : 88),
                  "effective transposed note boundary");
            atlas::VoiceCoefficients c;
            check(plan.prepareVoiceUpdate(1, settings, c),
                  "transposed preparation");
            atlas::AudioRenderer renderer(plan);
            check(renderer.updateVoice(1, c), "valid transposed POD rejected");
            render(plan);
        }
        const auto high = planFor({}, 22050, 72);
        atlas::VoiceCoefficients c;
        check(high.prepareVoiceUpdate(1, {}, c), "root boundary preparation");
        c.pitchMultiplier = 2;
        atlas::AudioRenderer renderer(high);
        check(!renderer.updateVoice(1, c),
              "POD update bypassed effective root ceiling");
    });
    test("all-character dense stealing allocates nothing and obeys shared peak "
         "bound",
         [] {
             for (const auto character : characters) {
                 atlas::VoiceSettings settings;
                 settings.character = character;
                 settings.brightness = 1;
                 settings.roughness = 1;
                 settings.pulseHz = 16;
                 settings.sweepSemitones = -12;
                 std::vector<atlas::Event> events;
                 for (int window = 0; window < 30; ++window)
                     for (int n = 0; n < 4; ++n)
                         events.push_back(
                             event(window * 4 + n + 1, window * 10000));
                 const auto plan =
                     planFor(settings, 48000, 60, std::move(events));
                 atlas::AudioRenderer renderer(plan);
                 std::array<float, 514> output{};
                 const auto allocations = callbackAllocations,
                            frees = callbackFrees;
                 callback = true;
                 bool ok = renderer.play(renderer.generation());
                 while (renderer.state() != atlas::AudioState::finished) {
                     ok = renderer.process(output) && ok;
                     for (const auto value : output)
                         ok = ok && std::isfinite(value) &&
                              std::abs(value) <= .800001f;
                 }
                 callback = false;
                 check(ok && renderer.statistics().triggered == 120 &&
                           renderer.statistics().stolen == 112 &&
                           renderer.statistics().faults == 0,
                       "dense render failed");
                 check(allocations == callbackAllocations &&
                           frees == callbackFrees,
                       "audio callback allocated or freed memory");
             }
         });
    return failed ? 1 : 0;
}
