#pragma once
#include "live_activity.hpp"
#include <atlas/live_admission.hpp>
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
enum class LiveMode { direct, musical, smoothed };
struct LiveSettings {
    LiveMode mode{LiveMode::direct};
    unsigned density{5};
    CompositionSettings composition{.enabled = true};
    SpatialSettings spatial{};
};
struct LiveOutcome {
    std::string eventId, speciesId, region, representativeId;
    std::string_view status, voiceCharacter{};
};
class LivePerformance {
  public:
    using Clock = std::chrono::steady_clock;
    void accept(std::vector<LiveObservation>, const LiveSettings &,
                Clock::time_point,
                std::span<const LiveSmoothTiming> timing = {});
    void playDue(Clock::time_point, sqlite3 *, PlaybackRuntime &, VoiceStore *,
                 const LiveSettings &, unsigned rootMidi,
                 const LiveSessionBoundary *session = nullptr,
                 LiveActivityJournal *activity = nullptr);
    void acknowledge(const PlaybackSnapshot &);
    void retryHandedOff();
    std::size_t pendingCount() const {
        return pending_.size() + inFlight_.size();
    }
    std::size_t smoothCapacity() const {
        return pendingCount() < 128 ? 128 - pendingCount() : 0;
    }
    void cancel();
    std::chrono::milliseconds nextServiceDelay(bool smoothed = false) const;
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
    struct HandedOff {
        Note note;
        std::uint64_t id;
        std::string_view character;
    };
    struct SourceWindow {
        std::int64_t ingestion;
        Clock::time_point start, lastDue;
    };
    void queueSmoothed(std::vector<LiveObservation> &,
                       std::span<const LiveSmoothTiming>, Clock::time_point);
    void submitSmoothed(Clock::time_point, sqlite3 *, PlaybackRuntime &,
                        VoiceStore *, const LiveSettings &, unsigned, const LiveSessionBoundary *,
                        LiveActivityJournal *);
    std::deque<Note> pending_;
    std::deque<HandedOff> inFlight_;
    std::map<std::string, SourceWindow> sourceWindows_;
    std::uint64_t nextId_{1};
    std::vector<LiveArrangementCandidate> recent_;
    std::deque<Clock::time_point> reservations_;
    std::deque<std::string> order_;
    std::map<std::string, LiveOutcome> outcomes_;
};
} // namespace atlas
