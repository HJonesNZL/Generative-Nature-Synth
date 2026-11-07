#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atlas {

using Microseconds = std::int64_t;
Microseconds parseUtc(std::string_view iso8601);
std::string formatUtc(Microseconds value);

struct Position { double latitude{}, longitude{}; };

// Prepared on a worker; string ownership and source parsing never enter an audio callback.
struct Event {
    std::string id, sourceId, kind, locationKind;
    std::optional<std::string> speciesId, individualId, deploymentId, stationId;
    std::optional<Position> position;
    Microseconds observedAt{}, firstSeenAt{}, ingestedAt{};
    std::int64_t revisionSequence{};
    std::string normalizedJson;
    void validate() const;
};

enum class TimeBasis { observed, arrival };

struct Speed {
    std::int64_t numerator{1}, denominator{1};
    static Speed parse(std::string_view value);
    Microseconds scale(Microseconds interval) const;
};

struct ScheduledEvent {
    Microseconds dueUs{};
    std::size_t eventIndex{};
    std::int64_t revisionSequence{};
};

struct BlockTrigger {
    std::size_t eventIndex{};
    std::int64_t revisionSequence{};
    std::uint32_t sampleOffset{};
};

struct BlockResult {
    std::size_t written{}, omitted{}, total{};
    bool valid{true};
};

class Timeline {
public:
    // Construction allocates/sorts/validates; perform it OFF the realtime thread.
    Timeline(std::vector<Event> events, TimeBasis basis, Speed speed = {},
             std::optional<std::pair<Microseconds, Microseconds>> bounds = {});
    const std::vector<Event>& events() const noexcept { return events_; }
    const std::vector<ScheduledEvent>& schedule() const noexcept { return schedule_; }
    Microseconds durationUs() const noexcept { return duration_; }
    std::size_t peakEventsPerSecond() const noexcept;

    // Immutable lookup, no allocation/I/O/locks/throws. Caller owns preallocated output.
    // Half-open blocks. Overflow retains the earliest entries and reports omissions.
    // This schedules DATA events, not yet notes or DSP voices.
    BlockResult block(std::int64_t startFrame, std::uint32_t frames,
                      std::uint32_t sampleRate, std::span<BlockTrigger> output) const noexcept;
private:
    std::vector<Event> events_;
    std::vector<ScheduledEvent> schedule_;
    Microseconds duration_{};
};

} // namespace atlas
