#pragma once
#include <atlas/playback.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <deque>
#include <map>

namespace atlas {
struct LiveObservation {
    std::string id, species, name;
    std::optional<Position> position;
    std::string stationId{}, eventKind{}, locationKind{}, locationLabel{};
    std::optional<double> uncertaintyMetres{};
};
// One UiHost worker owns metadata and the bounded 30-second display journal.
// Callback records contain numeric tokens/times only. A submitted note never
// enters the journal until the output callback confirms its actual onset.
class LiveActivityJournal {
  public:
    static constexpr std::size_t voiceCapacity = 4096, deltaCapacity = 8192;
    static std::int64_t clockUs() noexcept;
    void submitted(std::uint64_t preparation, std::uint64_t token,
                   const LiveObservation &, const VoiceSettings &,
                   std::string region);
    void service(PlaybackRuntime &);
    void stop(PlaybackRuntime &);
    void stopAt(std::int64_t nowUs);
    void reset();
    std::string page(std::string_view sessionId, bool active,
                     std::uint64_t after, std::size_t limit,
                     std::int64_t nowUs = clockUs());
    // Numeric callback receipt seam; also supports deterministic worker tests.
    void ingest(std::uint64_t preparation, std::span<const LiveVoiceStart>,
                std::int64_t nowUs);

  private:
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    struct Voice {
        LiveObservation observation;
        std::string voiceName, voiceType, region;
        std::int64_t onsetUs{}, endUs{}, registeredUs{};
        std::uint64_t sequence{};
    };
    struct Delta { std::uint64_t sequence; Key key; };
    std::map<Key, Voice> submitted_, voices_;
    std::deque<Delta> deltas_;
    std::uint64_t sequence_{1}, dropped_{}, lostPreviousPreparations_{},
        preparation_{}, lostCurrentPreparation_{};
    void changed(Key, Voice &);
    void prune(std::int64_t nowUs);
    std::string view(Key, const Voice &) const;
};
} // namespace atlas
