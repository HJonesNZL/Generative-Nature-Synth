#pragma once
#include <atlas/live_admission.hpp>
#include <map>
#include <string_view>
#include <unordered_map>
namespace atlas {
// Serial worker ownership. A committed frontier caches current validated
// species totals across all pages; later revisions update only changed events.
// Session changes rebuild off-thread; privacy store rotation invalidates scope.
class LiveSpeciesCounts {
  public:
    std::string page(sqlite3 *, const std::optional<LiveSessionBoundary> &,
                     std::string_view sessionId, std::size_t offset,
                     std::size_t limit);

    std::size_t lastExaminedRevisions() const noexcept { return lastExamined_; }

  private:
    struct Row {
        std::string id, name;
        std::uint64_t count{};
    };
    std::string cachedSession_;
    std::int64_t cachedRevision_{-1};
    std::vector<Row> ranked_;
    std::map<std::string, Row> counts_;
    std::unordered_map<std::string, std::string> eventSpecies_;
    std::size_t lastExamined_{};
};
} // namespace atlas
