#include <algorithm>
#include <array>
#include <atlas/playback.hpp>
#include <atlas/voices.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <thread>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
thread_local bool inCallback{};
std::atomic<unsigned> callbackAllocations{}, callbackFrees{};
void *allocate(std::size_t bytes,
               std::size_t alignment = alignof(std::max_align_t)) {
    if (inCallback)
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
    if (pointer && inCallback)
        ++callbackFrees;
    std::free(pointer);
}
void releaseAligned(void *pointer, std::align_val_t alignment) noexcept {
#if defined(_MSC_VER)
    if (static_cast<std::size_t>(alignment) > alignof(std::max_align_t)) {
        if (pointer && inCallback)
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
struct TemporarySidecar {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("atlas-voice-audio-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporarySidecar() { std::filesystem::create_directories(directory); }
    ~TemporarySidecar() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    std::filesystem::path path() const { return directory / "voices.sqlite3"; }
};
atlas::Event event(int id, atlas::Microseconds time, std::string species) {
    atlas::Event result;
    result.id = "voice:" + std::to_string(id);
    result.sourceId = "voice";
    result.kind = "acoustic_species_detection";
    result.locationKind = "station_detection";
    result.stationId = "voice:station";
    if (!species.empty())
        result.speciesId = std::move(species);
    result.revisionSequence = id;
    result.observedAt = result.firstSeenAt = time;
    return result;
}
atlas::Timeline material(std::vector<atlas::Event> events) {
    return atlas::Timeline(std::move(events), atlas::TimeBasis::observed);
}
// Closed form independent of the renderer's recursive envelope/phase state.
double oracle(std::uint64_t age, unsigned baseMidi, std::uint32_t rate,
              const atlas::VoiceSettings &settings, double masterDb = 0) {
    const auto n = static_cast<double>(age);
    const auto attack = std::ceil(settings.attackSeconds * rate);
    const auto decay = std::ceil(settings.decaySeconds * rate);
    const auto releaseFrames = std::ceil(.020 * rate);
    if (n >= attack + decay + releaseFrames)
        return 0;
    const auto a =
        n < attack ? (1 - std::cos(std::numbers::pi * n / attack)) / 2 : 1;
    const auto d = std::pow(1000.0, -std::max(0.0, n - attack) / decay);
    const auto r = n < attack + decay
                       ? 1
                       : (1 + std::cos(std::numbers::pi * (n - attack - decay) /
                                       releaseFrames)) /
                             2;
    const auto frequency = 440 * std::pow(2.0, (static_cast<double>(baseMidi) +
                                                settings.tuningSemitones - 69) /
                                                   12);
    const auto phase = 2 * std::numbers::pi * frequency * n / rate;
    return .05 * a * d * r *
           (std::sin(phase) + .25 * std::sin(2 * phase) +
            .08 * std::sin(3 * phase)) /
           1.33 * std::pow(10.0, (settings.gainDb + masterDb) / 20);
}
std::vector<float> render(const atlas::AudioPlan &plan,
                          std::size_t block = 257) {
    atlas::AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "play");
    std::vector<float> result(static_cast<std::size_t>(plan.totalFrames()) * 2);
    for (std::size_t i = 0; i < result.size();) {
        const auto count = std::min(block * 2, result.size() - i);
        check(renderer.process(std::span<float>(result).subspan(i, count)),
              "render");
        i += count;
    }
    check(renderer.statistics().faults == 0, "render fault");
    return result;
}
class ManualDriver final : public atlas::OutputDriver {
  public:
    bool running{};
    unsigned starts{}, stops{};
    std::string start(atlas::PlaybackRuntime &runtime,
                      std::uint32_t rate) override {
        ++starts;
        running = true;
        runtime.deviceStarted(rate, 64, 2);
        return {};
    }
    void stop() noexcept override {
        ++stops;
        running = false;
    }
};
struct Fixture {
    ManualDriver *driver{};
    atlas::PlaybackRuntime runtime;
    std::array<float, 4096> left{}, right{};
    std::array<float *, 2> outputs{left.data(), right.data()};
    Fixture() : Fixture(std::make_unique<ManualDriver>()) {}
    explicit Fixture(std::unique_ptr<ManualDriver> value)
        : driver(value.get()), runtime(std::move(value)) {}
    void pump(int frames = 64) {
        inCallback = true;
        runtime.process(outputs.data(), 2, frames);
        inCallback = false;
    }
    template <class Function>
    atlas::PlaybackResult acknowledged(Function function) {
        auto pending = std::async(std::launch::async, std::move(function));
        while (pending.wait_for(std::chrono::milliseconds(0)) !=
               std::future_status::ready) {
            pump();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return pending.get();
    }
};
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
        "profile controls follow independent pitch envelope and gain equations",
        [] {
            const auto timeline = material({event(1, 0, "species:a")});
            for (const auto rate : {22050u, 48000u, 192000u}) {
                for (const auto &settings :
                     {atlas::VoiceSettings{},
                      atlas::VoiceSettings{-12, .002, .100, -60},
                      atlas::VoiceSettings{12, .100, 3, 0},
                      atlas::VoiceSettings{7, .023, 1.6, -9}}) {
                    const std::array<atlas::VoiceProfile, 2> profiles{
                        {{"", {}}, {"species:a", settings}}};
                    atlas::AudioSettings global;
                    global.sampleRate = rate;
                    const atlas::AudioPlan plan(timeline, global, profiles);
                    const auto samples = render(plan);
                    check(plan.actions().front().voiceSlot == 1,
                          "species slot");
                    for (std::size_t n = 0; n < plan.totalFrames(); ++n) {
                        check(
                            std::abs(samples[n * 2] -
                                     oracle(n, plan.actions().front().midiNote,
                                            rate, settings)) < 2e-7,
                            "closed-form PCM mismatch");
                        check(samples[n * 2] == samples[n * 2 + 1] &&
                                  std::abs(samples[n * 2]) <= .05f,
                              "level or stereo bound");
                    }
                    check(render(plan, 1) == samples,
                          "block partition changed profile PCM");
                }
            }
        });
    test("default profiles preserve legacy PCM and deterministic species "
         "pitches",
         [] {
             const auto timeline = material(
                 {event(1, 0, "species:a"), event(2, 200000, "species:b"),
                  event(3, 400000, ""), event(4, 600000, "unknown")});
             const std::array<atlas::VoiceProfile, 3> profiles{
                 {{"", {}}, {"species:a", {}}, {"species:b", {}}}};
             const atlas::AudioPlan legacy(timeline),
                 profiled(timeline, {}, profiles),
                 editable(timeline, {}, profiles, true);
             check(profiled.totalFrames() == legacy.totalFrames(),
                   "fixed default tail differs");
             const auto expected = render(legacy);
             check(render(profiled) == expected,
                   "default profile changed exact legacy PCM");
             const auto extended = render(editable);
             check(
                 std::equal(expected.begin(), expected.end(), extended.begin()),
                 "editable default changed legacy PCM");
             check(std::all_of(extended.begin() +
                                   static_cast<std::ptrdiff_t>(expected.size()),
                               extended.end(),
                               [](float value) { return value == 0; }),
                   "reserved tail is not silent");
             for (std::size_t i = 0; i < legacy.actions().size(); ++i)
                 check(legacy.actions()[i].midiNote ==
                           editable.actions()[i].midiNote,
                       "species hash pitch changed");
             check(editable.voiceSlot("species:a") == 1 &&
                       editable.voiceSlot("species:b") == 2 &&
                       editable.voiceSlot("unknown") == 0,
                   "worker slot lookup");
         });
    test(
        "actual store defaults and saved normalized roundtrips preserve legacy "
        "PCM",
        [] {
            TemporarySidecar temporary;
            const std::array<atlas::VoiceSpecies, 2> species{
                {{"species:a", "A"}, {"species:b", "B"}}};
            {
                atlas::VoiceStore store(temporary.path(), species);
                for (const auto &voice : store.voices()) {
                    // Exercise the same normalized boundary that parameter
                    // gestures and saved presets use, including the logarithmic
                    // default coordinates.
                    for (std::size_t i = 0; i < atlas::voiceParameterIds.size();
                         ++i) {
                        store.setParameter(voice.id,
                                           atlas::voiceParameterIds[i],
                                           voice.normalizedValues[i],
                                           atlas::VoiceGesture::begin);
                        store.setParameter(voice.id,
                                           atlas::voiceParameterIds[i],
                                           voice.normalizedValues[i],
                                           atlas::VoiceGesture::end);
                    }
                    store.save(voice.id, 0);
                }
            }
            atlas::VoiceStore reopened(temporary.path(), species);
            const auto defaults = reopened.getVoice("default").settings;
            for (const auto &voice : reopened.voices())
                check(voice.settings.tuningSemitones == 0 &&
                          voice.settings.attackSeconds == .008 &&
                          voice.settings.decaySeconds == .650 &&
                          voice.settings.gainDb == 0,
                      "normalized default did not retain exact physical "
                      "identity");
            // Cover every supported integer rate: a one-ulp inversion error
            // must not add or remove a frame at an otherwise exact sample
            // boundary.
            for (std::uint32_t rate = 22050; rate <= 192000; ++rate) {
                check(std::ceil(defaults.attackSeconds * rate) ==
                          std::ceil(.008 * rate),
                      "stored default attack acquired a frame");
                check(std::ceil(defaults.decaySeconds * rate) ==
                          std::ceil(.650 * rate),
                      "stored default T60 acquired a frame");
            }
            const std::array<atlas::VoiceProfile, 3> profiles{
                {{"", defaults},
                 {"species:a", reopened.getVoice("species:species:a").settings},
                 {"species:b",
                  reopened.getVoice("species:species:b").settings}}};
            const auto timeline =
                material({event(1, 0, "species:a"),
                          event(2, 100000, "species:b"), event(3, 200000, "")});
            for (const auto rate : {22050u, 44100u, 48000u, 96000u, 192000u}) {
                atlas::AudioSettings global;
                global.sampleRate = rate;
                const atlas::AudioPlan legacy(timeline, global),
                    fromStore(timeline, global, profiles);
                check(fromStore.totalFrames() == legacy.totalFrames(),
                      "store default changed legacy duration");
                check(
                    render(fromStore) == render(legacy),
                    "actual normalized store default changed exact legacy PCM");
            }
        });
    test(
        "normalized endpoints adjacent values and random settings compile into "
        "bounded audio updates",
        [] {
            TemporarySidecar temporary;
            atlas::VoiceStore store(temporary.path());
            const std::array<atlas::VoiceProfile, 1> profiles{
                {{"", store.getVoice("default").settings}}};
            const auto timeline = material({event(1, 0, "")});
            const auto physical = [](const atlas::VoiceSettings &settings,
                                     std::size_t parameter) {
                switch (parameter) {
                case 0:
                    return static_cast<double>(settings.tuningSemitones);
                case 1:
                    return settings.attackSeconds;
                case 2:
                    return settings.decaySeconds;
                case 3:
                    return settings.gainDb;
                case 4:
                    return settings.brightness;
                case 5:
                    return settings.roughness;
                case 6:
                    return settings.pulseHz;
                case 7:
                    return settings.sweepSemitones;
                default:
                    throw std::runtime_error(
                        "unknown physical parameter fixture");
                }
            };
            std::uint64_t seed = 0x7a341c649e1235d1ULL;
            for (const auto rate :
                 {22050u, 44100u, 48000u, 88200u, 96000u, 192000u}) {
                atlas::AudioSettings global;
                global.sampleRate = rate;
                const atlas::AudioPlan plan(timeline, global, profiles, true);
                atlas::AudioRenderer renderer(plan);
                for (std::size_t parameter = 0;
                     parameter < atlas::voiceParameterIds.size(); ++parameter) {
                    const auto id = atlas::voiceParameterIds[parameter];
                    const auto &spec = atlas::voiceParameterSpecs[parameter];
                    store.setParameter("default", id, .5,
                                       atlas::VoiceGesture::begin);
                    for (unsigned i = 0; i < 3000; ++i) {
                        seed = seed * 6364136223846793005ULL +
                               1442695040888963407ULL;
                        const auto value =
                            i == 0   ? 0.0
                            : i == 1 ? 1.0
                            : i == 2 ? std::nextafter(0.0, 1.0)
                            : i == 3
                                ? std::nextafter(1.0, 0.0)
                                : static_cast<double>(seed >> 11) * 0x1p-53;
                        const auto voice = store.setParameter(
                            "default", id, value, atlas::VoiceGesture::change);
                        check(atlas::validVoiceSettings(voice.settings),
                              "normalized setting escaped engine ranges");
                        const auto actual = physical(voice.settings, parameter);
                        check(actual >= spec.min && actual <= spec.max,
                              "physical control range");
                        if (i == 0 || i == 1)
                            check(actual == (i == 0 ? spec.min : spec.max),
                                  "physical endpoint is not exact");
                        atlas::VoiceCoefficients coefficients;
                        check(
                            plan.prepareVoiceUpdate(0, voice.settings,
                                                    coefficients),
                            "valid store setting rejected during compilation");
                        inCallback = true;
                        const auto accepted =
                            renderer.updateVoice(0, coefficients);
                        inCallback = false;
                        check(accepted,
                              "valid compiled setting rejected on callback");
                    }
                    store.setParameter("default", id, .5,
                                       atlas::VoiceGesture::end);
                }
            }
        });
    test("edits affect new notes of one species and retain captured ringing "
         "voices",
         [] {
             const auto timeline = material({event(1, 0, "species:a"),
                                             event(2, 200000, "species:b"),
                                             event(3, 1000000, "species:a")});
             const atlas::VoiceSettings before{0, .008, .650, 0},
                 after{12, .100, 3, -12};
             const std::array<atlas::VoiceProfile, 3> profiles{
                 {{"", {}}, {"species:a", before}, {"species:b", {}}}};
             const atlas::AudioPlan plan(timeline, {}, profiles, true);
             atlas::AudioRenderer renderer(plan);
             atlas::VoiceCoefficients update;
             check(plan.prepareVoiceUpdate(1, after, update),
                   "prepare numeric update");
             std::vector<float> samples(
                 static_cast<std::size_t>(plan.totalFrames()) * 2);
             check(renderer.play(renderer.generation()), "play");
             check(renderer.process(std::span<float>(samples).first(2000)),
                   "first 1000 frames");
             const auto generation = renderer.generation();
             inCallback = true;
             const auto accepted = renderer.updateVoice(1, update);
             inCallback = false;
             check(accepted && renderer.generation() == generation &&
                       renderer.position() == 1000,
                   "edit changed transport");
             for (std::size_t i = 2000; i < samples.size();) {
                 const auto count =
                     std::min<std::size_t>(514, samples.size() - i);
                 inCallback = true;
                 const auto ok = renderer.process(
                     std::span<float>(samples).subspan(i, count));
                 inCallback = false;
                 check(ok, "edited render");
                 i += count;
             }
             for (std::uint64_t n = 0; n < plan.totalFrames(); ++n) {
                 double expected{};
                 for (const auto &action : plan.actions()) {
                     if (action.frame <= n)
                         expected +=
                             oracle(n - action.frame, action.midiNote, 48000,
                                    action.eventIndex == 2 ? after : before);
                 }
                 check(std::abs(samples[static_cast<std::size_t>(n) * 2] -
                                expected) < 2e-7,
                       "ringing or other-species state changed");
             }
             const auto last = plan.actions().back().frame;
             check(plan.totalFrames() == last + 149760,
                   "editable plan did not reserve maximum allowed tail");
             double latePeak{};
             for (auto n = last + 130000; n < last + 140000; ++n)
                 latePeak = std::max(
                     latePeak, std::abs(static_cast<double>(
                                   samples[static_cast<std::size_t>(n) * 2])));
             check(latePeak > 1e-6 &&
                       renderer.state() == atlas::AudioState::finished,
                   "long edited decay truncated");
         });
    test("bounded bank and invalid parameter boundaries are explicit", [] {
        const auto timeline = material({event(1, 0, "species:512")});
        std::vector<atlas::VoiceProfile> profiles{{"", {}}};
        for (unsigned i = 1; i <= 512; ++i)
            profiles.push_back({"species:" + std::to_string(i), {}});
        const atlas::AudioPlan plan(timeline, {}, profiles, true);
        check(plan.voiceSlot("species:512") == 512 &&
                  plan.actions().front().voiceSlot == 512,
              "last bank slot unreachable");
        atlas::VoiceCoefficients coefficients;
        check(plan.prepareVoiceUpdate(512, {12, .100, 3, 0}, coefficients),
              "last bank update rejected");
        check(!plan.prepareVoiceUpdate(513, {}, coefficients),
              "out of bank update accepted");
        for (int i = 0; i < 10; ++i) {
            atlas::VoiceSettings invalid;
            switch (i) {
            case 0:
                invalid.tuningSemitones = std::numeric_limits<int>::max();
                break;
            case 1:
                invalid.tuningSemitones = -13;
                break;
            case 2:
                invalid.attackSeconds = .0019;
                break;
            case 3:
                invalid.attackSeconds = .1001;
                break;
            case 4:
                invalid.decaySeconds = .099;
                break;
            case 5:
                invalid.decaySeconds = 3.001;
                break;
            case 6:
                invalid.gainDb = -60.01;
                break;
            case 7:
                invalid.gainDb = .01;
                break;
            case 8:
                invalid.attackSeconds =
                    std::numeric_limits<double>::quiet_NaN();
                break;
            default:
                invalid.decaySeconds = std::numeric_limits<double>::infinity();
            }
            check(!atlas::validVoiceSettings(invalid) &&
                      !plan.prepareVoiceUpdate(1, invalid, coefficients),
                  "invalid parameter accepted");
        }
        const atlas::AudioPlan immutable(timeline, {}, profiles);
        check(!immutable.prepareVoiceUpdate(1, {}, coefficients),
              "immutable plan accepted edit");
        atlas::AudioSettings highRoot;
        highRoot.rootMidi = 72;
        const atlas::AudioPlan high(timeline, highRoot, profiles, true);
        check(!high.prepareVoiceUpdate(1, {1, .008, .650, 0}, coefficients),
              "transposed root outside 48..72 accepted");
        profiles.push_back({"too-many", {}});
        bool rejected{};
        try {
            const atlas::AudioPlan oversized(timeline, {}, profiles, true);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "oversized bank accepted");
        for (const auto &invalid :
             std::vector<std::vector<atlas::VoiceProfile>>{
                 {{"missing-default", {}}},
                 {{"", {}}, {"", {}}},
                 {{"", {}}, {"a", {}}, {"a", {}}}}) {
            rejected = false;
            try {
                const atlas::AudioPlan malformed(timeline, {}, invalid, true);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "malformed bank accepted");
        }
    });
    test("runtime acknowledges edits without changing device preparation or "
         "transport",
         [] {
             Fixture fixture;
             check(fixture.runtime.updateVoice(0, {}).error ==
                       atlas::PlaybackError::notPrepared,
                   "unprepared edit");
             const auto timeline = material(
                 {event(1, 0, "species:a"), event(2, 3000000, "species:a")});
             const std::array<atlas::VoiceProfile, 2> profiles{
                 {{"", {}}, {"species:a", {}}}};
             check(fixture.runtime
                       .prepare(timeline, {48000, 60, .008, .650, -18},
                                profiles, true)
                       .ok,
                   "prepare");
             check(fixture.runtime.updateVoice(1, {-12, .002, 3, -6}).ok,
                   "quiescent edit rejected");
             check(fixture.runtime.enable(true).ok, "enable");
             auto snapshot = fixture.runtime.snapshot();
             check(fixture
                       .acknowledged([&] {
                           return fixture.runtime.command(
                               {atlas::PlaybackCommandKind::play,
                                snapshot.preparationId, snapshot.generation,
                                0});
                       })
                       .ok,
                   "play ack");
             snapshot = fixture.runtime.snapshot();
             const auto starts = fixture.driver->starts,
                        stops = fixture.driver->stops;
             const auto allocations = callbackAllocations.load(),
                        frees = callbackFrees.load();
             for (int i = 0; i < 30; ++i) {
                 fixture.runtime.heartbeat();
                 const auto result = fixture.acknowledged([&] {
                     return fixture.runtime.updateVoice(
                         1, {i % 2 ? 12 : -12, .002, i % 2 ? 3 : .100,
                             i % 2 ? -60.0 : 0.0});
                 });
                 check(result.ok &&
                           result.snapshot.preparationId ==
                               snapshot.preparationId &&
                           result.snapshot.generation == snapshot.generation &&
                           result.snapshot.enabled &&
                           result.snapshot.state == atlas::AudioState::playing,
                       "edit acknowledgement or transport changed");
             }
             check(fixture.driver->starts == starts &&
                       fixture.driver->stops == stops,
                   "knob edit restarted output");
             check(callbackAllocations == allocations && callbackFrees == frees,
                   "callback update allocated or destroyed ownership");
             check(fixture.runtime.updateVoice(2, {}).error ==
                       atlas::PlaybackError::invalidVoiceSettings,
                   "unknown slot accepted");
             check(fixture.runtime.updateVoice(1, {13, .008, .650, 0}).error ==
                       atlas::PlaybackError::invalidVoiceSettings,
                   "invalid live setting accepted");
             check(fixture.runtime.enable(false).ok, "disable");
         });
    check(callbackAllocations == 0 && callbackFrees == 0,
          "callback allocation/deallocation probe");
    return failures ? 1 : 0;
}
