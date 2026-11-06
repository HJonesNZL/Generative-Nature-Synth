#include <algorithm>
#include <array>
#include <atlas/presets.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
const atlas::SpeciesPreset &find(std::string_view speciesId) {
    const auto all = atlas::speciesPresets();
    const auto found =
        std::find_if(all.begin(), all.end(), [speciesId](const auto &preset) {
            return preset.speciesId == speciesId;
        });
    check(found != all.end(), "expected exact provider ID missing");
    return *found;
}
std::vector<float> commonNote(atlas::VoiceSettings settings) {
    // Isolate colour/gesture from transposition, provider-note mapping and
    // gain.
    settings.tuningSemitones = 0;
    settings.gainDb = 0;
    atlas::Event event;
    event.id = "preset-test:1";
    event.sourceId = "preset-test";
    event.kind = "acoustic_species_detection";
    event.locationKind = "station_detection";
    event.stationId = "preset-test:station";
    event.speciesId = "tone:1";
    event.revisionSequence = 1;
    const std::array<atlas::VoiceProfile, 2> profiles{
        {{"", {}}, {"tone:1", settings}}};
    const atlas::AudioPlan plan(
        atlas::Timeline({event}, atlas::TimeBasis::observed), {}, profiles);
    atlas::AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "play candidate");
    // Compare a shared half-second window. Different lifetimes yield silence.
    std::vector<float> samples(48000);
    for (std::size_t i = 0; i < samples.size();) {
        const auto count = std::min<std::size_t>(514, samples.size() - i);
        check(renderer.process(std::span<float>(samples).subspan(i, count)),
              "render candidate");
        i += count;
    }
    check(renderer.statistics().faults == 0, "candidate numerical fault");
    return samples;
}
double distance(const std::vector<float> &a, const std::vector<float> &b) {
    double aa{}, bb{}, ab{};
    for (std::size_t i = 0; i < a.size(); i += 2) {
        aa += a[i] * static_cast<double>(a[i]);
        bb += b[i] * static_cast<double>(b[i]);
        ab += a[i] * static_cast<double>(b[i]);
    }
    check(aa > 0 && bb > 0, "silent candidate");
    return 1 - ab / std::sqrt(aa * bb);
}
} // namespace
int main() {
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
    test("immutable catalog metadata, exact IDs and unique valid settings", [] {
        const auto entries = atlas::speciesPresets();
        check(entries.size() == 33, "32 named taxa plus unresolved bat entry");
        std::array<unsigned, 6> characters{};
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto &preset = entries[i];
            check(!preset.id.empty() && !preset.speciesId.empty() &&
                      !preset.name.empty() && !preset.scientificName.empty() &&
                      !preset.description.empty() &&
                      !preset.evidenceStatus.empty() &&
                      !preset.researchPath.empty(),
                  "missing reviewable metadata");
            check(preset.evidenceStatus == "research_candidate" ||
                      preset.evidenceStatus == "ecological_metaphor" ||
                      preset.evidenceStatus == "unresolved_taxon",
                  "catalog evidence status outside public enum");
            check(preset.id ==
                      "candidate:" + std::string(preset.speciesId) + ":v1",
                  "stable provider-bound candidate identity");
            check(atlas::speciesPreset(preset.id) == &preset &&
                      atlas::speciesPreset(preset.speciesId) == nullptr,
                  "lookup must use exact candidate ID, not fuzzy species "
                  "matching");
            check(atlas::validVoiceSettings(preset.settings),
                  "invalid candidate settings");
            check(preset.settings.gainDb >= -6 && preset.settings.gainDb <= 0,
                  "candidate gain compounds existing conservative master/voice "
                  "scale");
            ++characters[static_cast<std::size_t>(preset.settings.character)];
            for (std::size_t j = 0; j < i; ++j)
                check(preset.id != entries[j].id &&
                          preset.speciesId != entries[j].speciesId,
                      "duplicate candidate/species identity");
        }
        check(atlas::speciesPreset("") == nullptr &&
                  atlas::speciesPreset("tui") == nullptr &&
                  atlas::speciesPreset(
                      "candidate:birdweather:species:2298:v2") == nullptr,
              "unknown or future version lookup must fail");
        check(std::all_of(characters.begin(), characters.end(),
                          [](unsigned n) { return n > 0; }),
              "catalog does not exercise shared character diversity");
    });
    test("exact species coverage includes explicit legacy and unresolved "
         "taxonomy",
         [] {
             constexpr std::array expectedBirdWeatherIds{
                 1,    35,  2298, 102,  89,   420, 294, 25,   4456, 1276, 4452,
                 61,   534, 44,   2561, 2952, 100, 106, 2195, 12,   46,   53,
                 4251, 130, 4459, 1051, 113,  64,  928, 913,  591,  17651};
             for (const auto id : expectedBirdWeatherIds)
                 find("birdweather:species:" + std::to_string(id));
             check(find("birdweather:species:591").scientificName ==
                       "Chrysococcyx lucidus",
                   "supplied cuckoo taxon must not be silently renamed");
             check(find("birdweather:species:913").scientificName ==
                           "Acanthis cabaret" &&
                       find("birdweather:species:913")
                               .description.find("legacy") !=
                           std::string_view::npos,
                   "cabaret legacy identity must remain explicit");
             const auto &bat = find("birdweather:species:17651");
             check(
                 bat.evidenceStatus.find("unresolved") !=
                         std::string_view::npos &&
                     bat.scientificName.find("order") !=
                         std::string_view::npos &&
                     bat.settings.character == atlas::VoiceCharacter::struck &&
                     bat.settings.tuningSemitones == 0 &&
                     bat.settings.brightness == .5 &&
                     bat.settings.roughness == 0 && bat.settings.pulseHz == 0 &&
                     bat.settings.sweepSemitones == 0,
                 "bat order must retain neutral fallback, not invented species "
                 "voice");
             const auto &shark = find("nzsharks:species:great_white_shark");
             check(shark.scientificName == "Carcharodon carcharias" &&
                       shark.evidenceStatus == "ecological_metaphor" &&
                       shark.description.find("ecological") !=
                           std::string_view::npos,
                   "white shark must remain an explicit ecological invention");
         });
    test(
        "related families share cores and substantial gestures remain distinct",
        [] {
            const auto &bellbird = find("birdweather:species:1276");
            const auto &tui = find("birdweather:species:2561");
            check(bellbird.settings.character == tui.settings.character &&
                      tui.settings.roughness - bellbird.settings.roughness > .5,
                  "honeyeater core and coarse contrast");
            check(find("birdweather:species:35").settings.character ==
                      find("birdweather:species:61").settings.character,
                  "Turdus core");
            check(find("birdweather:species:100").settings.character ==
                      find("birdweather:species:130").settings.character,
                  "Anatidae core");
            for (const auto id :
                 {"birdweather:species:89", "birdweather:species:294",
                  "birdweather:species:25", "birdweather:species:913"})
                check(find(id).settings.character ==
                          atlas::VoiceCharacter::reed,
                      "finch shared core");
            constexpr std::array<std::pair<std::string_view, std::string_view>,
                                 10>
                contrasts{
                    {{"birdweather:species:1276", "birdweather:species:2561"},
                     {"birdweather:species:35", "birdweather:species:61"},
                     {"birdweather:species:89", "birdweather:species:294"},
                     {"birdweather:species:100", "birdweather:species:130"},
                     {"birdweather:species:106", "birdweather:species:1051"},
                     {"birdweather:species:64", "birdweather:species:928"},
                     {"birdweather:species:2298", "birdweather:species:102"},
                     {"birdweather:species:4456", "birdweather:species:2195"},
                     {"birdweather:species:4459", "birdweather:species:591"},
                     {"birdweather:species:102",
                      "nzsharks:species:great_white_shark"}}};
            for (const auto &[left, right] : contrasts)
                check(distance(commonNote(find(left).settings),
                               commonNote(find(right).settings)) > .01,
                      "candidate contrast disappears after removing register "
                      "and gain");
        });
    test("every candidate compiles across rate boundaries and renders finite",
         [] {
             for (const auto &preset : atlas::speciesPresets()) {
                 for (const auto rate : {22050u, 192000u}) {
                     atlas::Event event;
                     event.id = "catalog:1";
                     event.sourceId = "catalog";
                     event.kind = "acoustic_species_detection";
                     event.locationKind = "station_detection";
                     event.stationId = "catalog:station";
                     event.speciesId = std::string(preset.speciesId);
                     event.revisionSequence = 1;
                     const std::array<atlas::VoiceProfile, 2> profiles{
                         {{"", {}},
                          {std::string(preset.speciesId), preset.settings}}};
                     atlas::AudioSettings settings;
                     settings.sampleRate = rate;
                     const atlas::AudioPlan plan(
                         atlas::Timeline({event}, atlas::TimeBasis::observed),
                         settings, profiles, true);
                     atlas::VoiceCoefficients coefficients;
                     check(plan.prepareVoiceUpdate(1, preset.settings,
                                                   coefficients),
                           "candidate compile");
                     atlas::AudioRenderer renderer(plan);
                     check(renderer.updateVoice(1, coefficients) &&
                               renderer.play(renderer.generation()),
                           "candidate update rejected");
                     std::array<float, 8192> output{};
                     while (renderer.state() != atlas::AudioState::finished) {
                         check(renderer.process(output),
                               "candidate endpoint render");
                         for (const auto sample : output)
                             check(std::isfinite(sample) &&
                                       std::abs(sample) <= .050001f,
                                   "candidate endpoint output bound");
                     }
                     check(renderer.statistics().faults == 0,
                           "candidate endpoint fault");
                 }
             }
         });
    return failed ? 1 : 0;
}
