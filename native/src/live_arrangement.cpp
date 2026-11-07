#include <algorithm>
#include <atlas/live_arrangement.hpp>
#include <cmath>
#include <map>
#include <stdexcept>
namespace atlas {
std::vector<LiveArrangementDecision>
arrangeLivePhrase(const std::vector<LiveArrangementCandidate> &candidates,
                  std::size_t capacity, double tempoBpm,
                  std::span<const LiveArrangementCandidate> recent) {
    if (candidates.size() > 512)
        throw std::invalid_argument("Live phrase exceeds 512 candidates");
    if (!std::isfinite(tempoBpm) || tempoBpm <= 0.0)
        throw std::invalid_argument("Live tempo must be positive and finite");
    capacity = std::min<std::size_t>(capacity, 8);
    using Regions = std::map<std::string, std::vector<std::size_t>>;
    std::map<std::string, Regions> groups;
    std::vector<LiveArrangementDecision> result;
    result.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto &c = candidates[i];
        result.push_back({i, i, 0, LiveDisposition::skipped});
        groups[c.speciesKey][c.regionKey].push_back(i);
    }
    std::map<std::string, std::size_t> lastSpecies;
    std::map<std::pair<std::string, std::string>, std::size_t> lastRegion;
    recent = recent.last(std::min<std::size_t>(recent.size(), 64));
    for (std::size_t i = 0; i < recent.size(); ++i) {
        lastSpecies[recent[i].speciesKey] = i + 1;
        lastRegion[{recent[i].speciesKey, recent[i].regionKey}] = i + 1;
    }
    std::map<std::string, std::vector<std::string>> regionOrder;
    std::vector<std::string> speciesOrder;
    for (auto &[species, regions] : groups) {
        speciesOrder.push_back(species);
        for (auto &[region, entries] : regions) {
            regionOrder[species].push_back(region);
            std::sort(entries.begin(), entries.end(), [&](auto a, auto b) {
                return candidates[a].eventId < candidates[b].eventId;
            });
        }
    }
    // Least recently represented species/regions get the scarce slots. Among
    // equally recent choices, rarity then lexical keys make selection stable.
    for (auto &[species, regions] : regionOrder) {
        std::stable_sort(regions.begin(), regions.end(),
                         [&](const auto &a, const auto &b) {
                             const auto ageA = lastRegion[{species, a}];
                             const auto ageB = lastRegion[{species, b}];
                             if (ageA != ageB)
                                 return ageA < ageB;
                             return groups.at(species).at(a).size() <
                                    groups.at(species).at(b).size();
                         });
    }
    const auto population = [&](const auto &species) {
        std::size_t total = 0;
        for (const auto &[region, entries] : groups.at(species)) {
            (void)region;
            total += entries.size();
        }
        return total;
    };
    std::stable_sort(speciesOrder.begin(), speciesOrder.end(),
                     [&](const auto &a, const auto &b) {
                         if (lastSpecies[a] != lastSpecies[b])
                             return lastSpecies[a] < lastSpecies[b];
                         return population(a) < population(b);
                     });
    std::map<std::string, std::size_t> regionCursor;
    std::vector<std::vector<std::size_t>> chosen;
    while (chosen.size() < capacity) {
        bool progress = false;
        for (const auto &species : speciesOrder) {
            if (chosen.size() == capacity)
                break;
            const auto &regions = groups.at(species);
            auto &cursor = regionCursor[species];
            if (cursor == regions.size())
                continue;
            const auto &region = regionOrder.at(species)[cursor++];
            chosen.push_back(regions.at(region));
            progress = true;
        }
        if (!progress)
            break;
    }
    // A sustained homogeneous burst can carry a short rhythm using distinct
    // real representatives, rather than compressing hundreds of events to one.
    if (chosen.size() == 1 && candidates.size() >= 16 && capacity > 1) {
        auto entries = std::move(chosen.front());
        chosen.clear();
        const auto repetitions = std::min(capacity, entries.size() / 4);
        chosen.resize(repetitions);
        for (std::size_t i = 0; i < entries.size(); ++i)
            chosen[i % repetitions].push_back(entries[i]);
    }
    const double stepMs = std::min(800.0, 30000.0 / tempoBpm);
    const double phraseMs =
        chosen.size() > 1
            ? std::min(1600.0, stepMs * static_cast<double>(chosen.size() - 1))
            : 0.0;
    for (std::size_t rank = 0; rank < chosen.size(); ++rank) {
        const auto representative = chosen[rank].front();
        const unsigned offset =
            candidates.size() <= 2 || chosen.size() <= 1
                ? 0U
                : static_cast<unsigned>(
                      std::lround(phraseMs * static_cast<double>(rank) /
                                  static_cast<double>(chosen.size() - 1)));
        for (const auto index : chosen[rank]) {
            result[index].representativeIndex = representative;
            result[index].offsetMs = offset;
            result[index].disposition = index == representative
                                            ? LiveDisposition::selected
                                            : LiveDisposition::grouped;
        }
    }
    return result;
}
} // namespace atlas
