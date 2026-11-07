#pragma once
#include <atlas/live_arrangement.hpp>
#include <atlas/playback.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <deque>
#include <map>

struct sqlite3;
namespace atlas {
// Owned by the same serial control worker as UiHost. Holds no callback
// resources.
struct LiveSettings {
    bool musical{};
    unsigned density{5};
    CompositionSettings composition{.enabled = true};
    SpatialSettings spatial{};
};
struct LiveObservation {
    std::string id, species, name;
    std::optional<Position> position;
};
struct LiveOutcome {
    std::string eventId, speciesId, region, representativeId;
    std::string_view status, voiceCharacter{};
};
class LivePerformance {
  public:
    using Clock = std::chrono::steady_clock;
    void accept(std::vector<LiveObservation>, const LiveSettings &,
                Clock::time_point);
    void playDue(Clock::time_point, sqlite3 *, PlaybackRuntime &, VoiceStore *,
                 const LiveSettings &, unsigned rootMidi);
    void cancel();
    std::chrono::milliseconds nextServiceDelay() const;
    const LiveOutcome *outcome(const std::string &) const;
    std::uint64_t sounded{}, grouped{}, skipped{};

  private:
    struct Note {
        LiveObservation observation;
        LiveArrangementCandidate group;
        Clock::time_point due;
        std::vector<std::string> members;
    };
    void finish(const Note &, std::string_view voiceCharacter = {});
    std::deque<Note> pending_;
    std::vector<LiveArrangementCandidate> recent_;
    std::deque<Clock::time_point> reservations_;
    std::deque<std::string> order_;
    std::map<std::string, LiveOutcome> outcomes_;
};
} // namespace atlas
