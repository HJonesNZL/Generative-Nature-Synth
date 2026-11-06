#include <algorithm>
#include <array>
#include <atlas/composition.hpp>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace atlas {
namespace {
constexpr Microseconds minuteUs = 60000000;
constexpr Microseconds maximumDurationUs = 1800LL * 1000000;
struct Motif {
    std::string_view species;
    std::array<int, 4> degrees;
    std::array<unsigned, 4> ticks, gates;
    std::array<double, 4> accents;
    unsigned count;
    int registerSemitones;
    double roleGain;
};
// Original musical designs, not transcriptions or biological measurements.
// Families share synthesis bodies; these short score gestures remain distinct.
constexpr std::array<Motif, 32> motifs{{
    {"birdweather:species:2298",
     {0, -1},
     {0, 10},
     {7, 11},
     {1, .87},
     2,
     -12,
     .78},
    {"birdweather:species:102",
     {0, 0, -1, 0},
     {0, 3, 6, 10},
     {2, 2, 2, 3},
     {1, .8, .85, .7},
     4,
     2,
     .9},
    {"birdweather:species:420",
     {0, 1, 3, 2},
     {0, 4, 7, 12},
     {3, 2, 4, 5},
     {.75, .9, 1, .7},
     4,
     7,
     .64},
    {"birdweather:species:4456",
     {0, 1, 0, 2},
     {0, 2, 5, 7},
     {1, 1, 1, 2},
     {1, .7, .9, .65},
     4,
     9,
     .62},
    {"birdweather:species:1276",
     {0, 2, 1, 4},
     {0, 8, 20, 32},
     {7, 10, 9, 16},
     {1, .9, .8, .9},
     4,
     0,
     .95},
    {"birdweather:species:4452",
     {0, -1, -2, -1},
     {0, 3, 7, 13},
     {2, 3, 5, 8},
     {.7, .85, 1, .65},
     4,
     11,
     .55},
    {"birdweather:species:2561",
     {0, 3, -1, 2},
     {0, 6, 9, 22},
     {5, 2, 4, 12},
     {1, .75, .9, .85},
     4,
     0,
     1},
    {"birdweather:species:2195",
     {0, 0, 2},
     {0, 3, 11},
     {2, 2, 6},
     {.8, .7, 1},
     3,
     5,
     .7},
    {"birdweather:species:4251",
     {0, -2, 1},
     {0, 7, 19},
     {5, 7, 4},
     {1, .9, .65},
     3,
     -5,
     .95},
    {"birdweather:species:4459",
     {2, 1, 0, -1},
     {0, 3, 8, 16},
     {2, 3, 5, 7},
     {1, .9, .8, .65},
     4,
     4,
     .7},
    {"birdweather:species:591",
     {0, 1, 2, -1},
     {0, 8, 16, 28},
     {5, 5, 6, 9},
     {.8, .9, 1, .75},
     4,
     4,
     .85},
    {"birdweather:species:1",
     {0, 1, 0},
     {0, 3, 9},
     {2, 2, 3},
     {1, .65, .8},
     3,
     3,
     .65},
    {"birdweather:species:35",
     {0, 3, 1, -1},
     {0, 9, 21, 34},
     {8, 10, 9, 12},
     {.9, 1, .8, .65},
     4,
     0,
     .9},
    {"birdweather:species:89",
     {0, 1, 2, 1},
     {0, 3, 5, 9},
     {2, 2, 2, 4},
     {.75, .85, 1, .7},
     4,
     8,
     .7},
    {"birdweather:species:294",
     {1, 0, -1},
     {0, 5, 22},
     {3, 15, 5},
     {.6, 1, .65},
     3,
     3,
     .78},
    {"birdweather:species:25",
     {1, 1, 0, -2},
     {0, 5, 8, 10},
     {3, 2, 1, 6},
     {.7, .75, .8, 1},
     4,
     5,
     .8},
    {"birdweather:species:61",
     {0, 2, 0, 2},
     {0, 5, 16, 21},
     {4, 7, 4, 9},
     {1, .8, .9, .75},
     4,
     0,
     .86},
    {"birdweather:species:534",
     {0, 1, 0, -2},
     {0, 7, 14, 29},
     {5, 5, 6, 8},
     {1, .9, .95, .6},
     4,
     -1,
     .9},
    {"birdweather:species:44",
     {0, 1, 0, 2},
     {0, 4, 9, 15},
     {3, 3, 4, 7},
     {.7, .75, .65, .8},
     4,
     9,
     .52},
    {"birdweather:species:2952",
     {0, 1, 0, -1},
     {0, 6, 10, 12},
     {3, 2, 1, 2},
     {1, .9, .85, .7},
     4,
     4,
     .88},
    {"birdweather:species:100", {0, 2}, {0, 24}, {15, 18}, {1, .8}, 2, -9, .78},
    {"birdweather:species:106", {0, -1}, {0, 5}, {2, 4}, {1, .7}, 2, -5, .72},
    {"birdweather:species:12",
     {0, 1, 0},
     {0, 12, 28},
     {10, 12, 6},
     {.8, 1, .7},
     3,
     -7,
     .64},
    {"birdweather:species:46",
     {1, -1, 0},
     {0, 12, 25},
     {10, 11, 9},
     {1, .8, .65},
     3,
     -4,
     .75},
    {"birdweather:species:53",
     {2, -1, 0, 0},
     {0, 10, 19, 23},
     {8, 6, 1, 1},
     {.8, 1, .6, .5},
     4,
     3,
     .86},
    {"birdweather:species:130",
     {0, 0, -1, -1},
     {0, 6, 12, 18},
     {4, 4, 3, 3},
     {1, .8, .6, .45},
     4,
     -6,
     .78},
    {"birdweather:species:1051",
     {0, 2},
     {0, 13},
     {10, 5},
     {.85, 1},
     2,
     -3,
     .88},
    {"birdweather:species:113",
     {0, 0, 0, 1},
     {0, 5, 10, 18},
     {3, 3, 3, 20},
     {.65, .7, .75, 1},
     4,
     5,
     .82},
    {"birdweather:species:64",
     {0, -1, -2},
     {0, 7, 13},
     {4, 3, 5},
     {1, .85, .55},
     3,
     -8,
     .9},
    {"birdweather:species:928",
     {0, 2, -1},
     {0, 20, 42},
     {14, 16, 13},
     {1, .9, .8},
     3,
     2,
     .95},
    {"birdweather:species:913",
     {0, 0, 1},
     {0, 2, 5},
     {1, 1, 3},
     {1, .7, .8},
     3,
     10,
     .6},
    {"nzsharks:species:great_white_shark", {0}, {0}, {48}, {1}, 1, -12, .58},
}};
constexpr Motif fallback{"", {0}, {0}, {8}, {1}, 1, 0, .7};
const Motif &motifFor(const Event &event) noexcept {
    if (event.speciesId) {
        const auto found =
            std::find_if(motifs.begin(), motifs.end(), [&](const Motif &m) {
                return m.species == *event.speciesId;
            });
        if (found != motifs.end())
            return *found;
    }
    return fallback;
}
std::uint32_t eventSeed(const Event &event, std::uint32_t seed) noexcept {
    auto hash = 2166136261u ^ seed;
    const auto append = [&](std::string_view value) {
        for (const auto byte : value) {
            hash ^= static_cast<unsigned char>(byte);
            hash *= 16777619u;
        }
        hash *= 16777619u;
    };
    append(event.sourceId);
    append(event.id);
    if (event.speciesId)
        append(*event.speciesId);
    const auto revision = static_cast<std::uint64_t>(event.revisionSequence);
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= static_cast<std::uint32_t>((revision >> (8 * i)) & 255u);
        hash *= 16777619u;
    }
    return hash ? hash : 0x6d2b79f5u;
}
std::uint32_t nextRandom(std::uint32_t &state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
int scaleNote(const CompositionSettings &settings, int target, int degree,
              int maximum) noexcept {
    int nearest = 36;
    int distance = 1000;
    for (int note = 36; note <= maximum; ++note) {
        if (compositionScaleContains(settings, note) &&
            std::abs(note - target) < distance) {
            nearest = note;
            distance = std::abs(note - target);
        }
    }
    while (degree > 0) {
        auto next = nearest + 1;
        while (next <= maximum && !compositionScaleContains(settings, next))
            ++next;
        if (next > maximum)
            break;
        nearest = next;
        --degree;
    }
    while (degree < 0) {
        auto next = nearest - 1;
        while (next >= 36 && !compositionScaleContains(settings, next))
            --next;
        if (next < 36)
            break;
        nearest = next;
        ++degree;
    }
    return nearest;
}
Microseconds ticksToUs(unsigned ticks, unsigned tempo) noexcept {
    const auto divisor = static_cast<Microseconds>(tempo) * 16;
    return (static_cast<Microseconds>(ticks) * minuteUs + divisor - 1) /
           divisor;
}
} // namespace

bool validCompositionSettings(const CompositionSettings &settings) noexcept {
    const auto scale = settings.scale;
    const auto density = settings.density;
    return (scale == CompositionScale::majorPentatonic ||
            scale == CompositionScale::minorPentatonic ||
            scale == CompositionScale::major ||
            scale == CompositionScale::naturalMinor) &&
           (density == CompositionDensity::sparse ||
            density == CompositionDensity::balanced ||
            density == CompositionDensity::active) &&
           settings.tonicPitchClass <= 11 && settings.tempoBpm >= 40 &&
           settings.tempoBpm <= 180 && std::isfinite(settings.variation) &&
           settings.variation >= 0 && settings.variation <= 1;
}
bool compositionScaleContains(const CompositionSettings &settings,
                              int midiNote) noexcept {
    if (settings.tonicPitchClass > 11)
        return false;
    const auto pitchClass = (((midiNote % 12) + 12) % 12 + 12 -
                             static_cast<int>(settings.tonicPitchClass)) %
                            12;
    unsigned mask{};
    switch (settings.scale) {
    case CompositionScale::majorPentatonic:
        mask = 0x295;
        break; // 0,2,4,7,9
    case CompositionScale::minorPentatonic:
        mask = 0x4a9;
        break; // 0,3,5,7,10
    case CompositionScale::major:
        mask = 0xab5;
        break; // 0,2,4,5,7,9,11
    case CompositionScale::naturalMinor:
        mask = 0x5ad;
        break; // 0,2,3,5,7,8,10
    default:
        return false;
    }
    return (mask & (1u << static_cast<unsigned>(pitchClass))) != 0;
}
CompiledComposition compileComposition(const Timeline &timeline,
                                       const CompositionSettings &settings,
                                       unsigned rootMidi) {
    if (!validCompositionSettings(settings) || rootMidi < 48 || rootMidi > 72)
        throw std::invalid_argument("invalid composition settings");
    if (timeline.durationUs() < 0 || timeline.durationUs() > maximumDurationUs)
        throw std::invalid_argument(
            "composition is limited to 30 minutes including tails");
    if (timeline.schedule().size() > compositionActionCapacity / 4)
        throw std::length_error("composition action capacity exceeded");
    CompiledComposition result;
    result.decisions.reserve(timeline.schedule().size());
    result.notes.reserve(timeline.schedule().size() * 4);
    const auto density = static_cast<unsigned>(settings.density);
    const auto eventLimit = 1u << density;
    const auto noteLimit = 1u << density;
    Microseconds beatWindow = -1;
    unsigned eventsInBeat{};
    for (const auto &scheduled : timeline.schedule()) {
        const auto decisionIndex = result.decisions.size();
        result.decisions.push_back({scheduled.eventIndex, false});
        auto due = scheduled.dueUs;
        if (settings.quantize) {
            const auto divisor =
                static_cast<Microseconds>(settings.tempoBpm) * 4;
            const auto quarterBeat = (due * divisor + minuteUs - 1) / minuteUs;
            due = (quarterBeat * minuteUs + divisor - 1) / divisor;
        }
        const auto beat = due * settings.tempoBpm / minuteUs;
        if (beat != beatWindow) {
            beatWindow = beat;
            eventsInBeat = 0;
        }
        if (eventsInBeat == eventLimit)
            continue;
        ++eventsInBeat;
        const auto &event = timeline.events()[scheduled.eventIndex];
        const auto &motif = motifFor(event);
        auto state = eventSeed(event, settings.seed);
        const auto count = std::min(motif.count, noteLimit);
        for (unsigned ordinal = 0; ordinal < count; ++ordinal) {
            const auto random = nextRandom(state);
            const auto changes =
                static_cast<double>(random) / 4294967296.0 < settings.variation;
            const auto degreeChange =
                changes ? static_cast<int>((random >> 16) % 3) - 1 : 0;
            const auto spacing = ordinal && changes ? (random >> 24) % 3 : 0;
            const auto noteDue = due + ticksToUs(motif.ticks[ordinal] + spacing,
                                                 settings.tempoBpm);
            if (noteDue > maximumDurationUs)
                throw std::invalid_argument(
                    "composition phrase exceeds 30 minute limit");
            const auto note = scaleNote(
                settings, static_cast<int>(rootMidi) + motif.registerSemitones,
                motif.degrees[ordinal] + degreeChange,
                static_cast<int>(std::min(76u, rootMidi + 16)));
            const auto attenuation =
                1 - settings.variation * .15 *
                        (static_cast<double>((random >> 8) & 255u) / 255.0);
            if (result.notes.size() == compositionActionCapacity)
                throw std::length_error("composition action capacity exceeded");
            result.notes.push_back(
                {noteDue, ticksToUs(motif.gates[ordinal], settings.tempoBpm),
                 scheduled.eventIndex, static_cast<unsigned>(note),
                 motif.roleGain * motif.accents[ordinal] * attenuation,
                 random});
        }
        // The final onset admission below can still omit all notes of a phrase.
        result.decisions[decisionIndex].admitted = true;
    }
    std::stable_sort(result.notes.begin(), result.notes.end(),
                     [](const auto &a, const auto &b) {
                         return std::tie(a.dueUs, a.eventIndex) <
                                std::tie(b.dueUs, b.eventIndex);
                     });
    std::vector<bool> emitted(timeline.events().size());
    Microseconds window = -1;
    unsigned admitted{};
    const auto end = std::remove_if(
        result.notes.begin(), result.notes.end(), [&](const auto &note) {
            const auto nextWindow = note.dueUs / 10000;
            if (nextWindow != window) {
                window = nextWindow;
                admitted = 0;
            }
            if (admitted == 4) {
                ++result.omittedNotes;
                return true;
            }
            ++admitted;
            emitted[note.eventIndex] = true;
            return false;
        });
    result.notes.erase(end, result.notes.end());
    for (auto &decision : result.decisions)
        decision.admitted = emitted[decision.eventIndex];
    return result;
}
} // namespace atlas
