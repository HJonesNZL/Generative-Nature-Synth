#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>
namespace atlas {
struct LiveArrangementCandidate {
    std::string eventId, speciesKey, regionKey;
};
enum class LiveDisposition { selected, grouped, skipped };
struct LiveArrangementDecision {
    std::size_t candidateIndex{}, representativeIndex{};
    unsigned offsetMs{};
    LiveDisposition disposition{LiveDisposition::skipped};
};
// Worker-owned, bounded to 512 candidates/eight groups. Sparse arrivals are
// immediate; dense phrases rotate species/regions and finish within 1600 ms.
// Decisions remain in input order; grouped entries name a selected input index.
// recent is oldest-first; only its last 64 actual sounded groups are consulted.
// Homogeneous dense bursts use distinct genuine representatives for
// repetitions.
std::vector<LiveArrangementDecision>
arrangeLivePhrase(const std::vector<LiveArrangementCandidate> &candidates,
                  std::size_t capacity = 8, double tempoBpm = 120.0,
                  std::span<const LiveArrangementCandidate> recent = {});
} // namespace atlas
