#include <algorithm>
#include <atlas/live_arrangement.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
using namespace atlas;
namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("Live arrangement regression");
}
} // namespace
int main() {
    try {
        check(arrangeLivePhrase({}).empty());
        std::vector<LiveArrangementCandidate> rotating{{"a", "bird", "east"},
                                                       {"b", "bird", "west"},
                                                       {"c", "frog", "east"}};
        const auto first = arrangeLivePhrase(rotating, 1);
        std::vector<LiveArrangementCandidate> recent;
        for (const auto &decision : first)
            if (decision.disposition == LiveDisposition::selected)
                recent.push_back(rotating[decision.candidateIndex]);
        const auto second = arrangeLivePhrase(rotating, 1, 120, recent);
        for (const auto &decision : second)
            if (decision.disposition == LiveDisposition::selected)
                check(rotating[decision.candidateIndex].speciesKey !=
                      recent.front().speciesKey);
        const std::vector<LiveArrangementCandidate> sameSpecies{
            {"a", "bird", "east"}, {"b", "bird", "west"}};
        const std::vector<LiveArrangementCandidate> eastHistory{
            {"old", "bird", "east"}};
        check(arrangeLivePhrase(sameSpecies, 1, 120, eastHistory)[1]
                  .disposition == LiveDisposition::selected);
        std::vector<LiveArrangementCandidate> sparse{{"b", "bird", "north"},
                                                     {"a", "frog", "south"}};
        for (const auto &d : arrangeLivePhrase(sparse))
            check(d.disposition == LiveDisposition::selected &&
                  d.offsetMs == 0);
        for (const auto &d : arrangeLivePhrase(sparse, 0))
            check(d.disposition == LiveDisposition::skipped && d.offsetMs == 0);
        std::vector<LiveArrangementCandidate> dense;
        for (int i = 0; i < 100; ++i)
            dense.push_back({"common" + std::to_string(i), "bird", "north"});
        const auto homogeneous = arrangeLivePhrase(dense, 4);
        std::size_t rhythmicOnsets = 0;
        for (const auto &decision : homogeneous) {
            check(decision.offsetMs <= 1600);
            if (decision.disposition == LiveDisposition::selected)
                ++rhythmicOnsets;
            else {
                check(decision.disposition == LiveDisposition::grouped);
                check(homogeneous[decision.representativeIndex].disposition ==
                      LiveDisposition::selected);
            }
        }
        check(rhythmicOnsets == 4);
        dense.push_back({"rare-a", "frog", "south"});
        dense.push_back({"rare-b", "whale", "ocean"});
        dense.push_back({"regional", "bird", "west"});
        auto result = arrangeLivePhrase(dense, 2);
        check(result[100].disposition == LiveDisposition::selected);
        check(result[101].disposition == LiveDisposition::selected);
        check(result[0].disposition == LiveDisposition::skipped);
        result = arrangeLivePhrase(dense);
        std::size_t selected = 0, grouped = 0;
        std::map<std::string, unsigned> offsets;
        for (const auto &d : result) {
            check(d.candidateIndex < dense.size() &&
                  d.representativeIndex < dense.size() && d.offsetMs <= 1600);
            if (d.disposition == LiveDisposition::selected) {
                ++selected;
                offsets[dense[d.candidateIndex].eventId] = d.offsetMs;
            }
            if (d.disposition == LiveDisposition::grouped) {
                ++grouped;
                check(result[d.representativeIndex].disposition ==
                      LiveDisposition::selected);
                check(result[d.representativeIndex].offsetMs == d.offsetMs);
            }
        }
        check(selected == 4 && grouped == 99);
        std::reverse(dense.begin(), dense.end());
        for (const auto &d : arrangeLivePhrase(dense))
            if (d.disposition == LiveDisposition::selected)
                check(offsets.at(dense[d.candidateIndex].eventId) ==
                      d.offsetMs);
        check(arrangeLivePhrase(dense, 8, 60.0).back().offsetMs <= 1600);
        bool rejected = false;
        try {
            (void)arrangeLivePhrase(dense, 8,
                                    std::numeric_limits<double>::quiet_NaN());
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected);
        rejected = false;
        try {
            (void)arrangeLivePhrase(std::vector<LiveArrangementCandidate>(513));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected);
        std::cout << "Live arrangement checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
