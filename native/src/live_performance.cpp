#include "live_performance.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <cmath>

namespace atlas {
namespace {
constexpr auto phraseWindow = std::chrono::milliseconds(1600);
std::pair<std::string, std::string>
liveRegion(const LiveSettings &settings,
           const std::optional<Position> &position) {
    if (!position)
        return {"", "Unassigned"};
    if (settings.musical && settings.spatial.enabled) {
        const auto slot =
            prepareSpatialRoute(settings.spatial, position).regionSlot;
        if (slot) {
            const auto &region = settings.spatial.regions[slot - 1];
            return {"region:" + region.id, region.name};
        }
    }
    // Coarse geographic cells also give unassigned observations geographic
    // representation.
    const int south =
        -90 +
        30 * std::min(5, static_cast<int>((position->latitude + 90) / 30));
    const int west =
        -180 +
        60 * std::min(5, static_cast<int>((position->longitude + 180) / 60));
    const auto label = std::to_string(south) + "° to " +
                       std::to_string(south + 30) + "° latitude, " +
                       std::to_string(west) + "° to " +
                       std::to_string(west + 60) + "° longitude";
    return {label, label};
}
} // namespace
const LiveOutcome *LivePerformance::outcome(const std::string &id) const {
    const auto it = outcomes_.find(id);
    return it == outcomes_.end() ? nullptr : &it->second;
}
void LivePerformance::accept(std::vector<LiveObservation> observations,
                             const LiveSettings &settings,
                             Clock::time_point now) {
    if (observations.empty())
        return;
    std::vector<LiveArrangementCandidate> candidates;
    candidates.reserve(observations.size());
    for (const auto &event : observations) {
        auto [key, label] = liveRegion(settings, event.position);
        candidates.push_back({event.id, event.species, std::move(key)});
        outcomes_.insert_or_assign(
            event.id,
            LiveOutcome{
                event.id, event.species, std::move(label), {}, "queued"});
        order_.push_back(event.id);
    }
    while (order_.size() > 2048) {
        outcomes_.erase(order_.front());
        order_.pop_front();
    }
    std::vector<LiveArrangementDecision> decisions;
    if (settings.musical) {
        while (!reservations_.empty() &&
               reservations_.front() + phraseWindow <= now)
            reservations_.pop_front();
        const auto budget =
            settings.density -
            std::min<std::size_t>(settings.density, reservations_.size());
        decisions = arrangeLivePhrase(candidates, budget,
                                      settings.composition.tempoBpm, recent_);
    } else {
        for (std::size_t i = 0; i < candidates.size(); ++i)
            decisions.push_back(
                {i, i, 0,
                 i < 8 ? LiveDisposition::selected : LiveDisposition::skipped});
    }
    std::map<std::size_t, std::vector<std::string>> members;
    for (const auto &decision : decisions) {
        auto &trace = outcomes_.at(candidates[decision.candidateIndex].eventId);
        if (decision.disposition == LiveDisposition::skipped) {
            trace.status = "skipped";
            ++skipped;
        } else if (decision.disposition == LiveDisposition::grouped) {
            trace.representativeId =
                candidates[decision.representativeIndex].eventId;
            members[decision.representativeIndex].push_back(trace.eventId);
        }
    }
    for (const auto &decision : decisions) {
        if (decision.disposition != LiveDisposition::selected)
            continue;
        pending_.push_back({std::move(observations[decision.candidateIndex]),
                            candidates[decision.candidateIndex],
                            now + std::chrono::milliseconds(decision.offsetMs),
                            std::move(members[decision.candidateIndex])});
        if (settings.musical)
            reservations_.push_back(now);
    }
    std::stable_sort(
        pending_.begin(), pending_.end(),
        [](const Note &a, const Note &b) { return a.due < b.due; });
}
void LivePerformance::finish(const Note &note, std::string_view character) {
    const bool played = !character.empty();
    auto mark = [&](const std::string &id, std::string_view status) {
        if (auto it = outcomes_.find(id); it != outcomes_.end()) {
            it->second.status = status;
            it->second.voiceCharacter = character;
        }
    };
    mark(note.observation.id, played ? "sounded" : "skipped");
    for (const auto &id : note.members)
        mark(id, played ? "grouped" : "skipped");
    if (played) {
        ++sounded;
        grouped += note.members.size();
    } else
        skipped += 1 + note.members.size();
}
void LivePerformance::playDue(Clock::time_point now, sqlite3 *db,
                              PlaybackRuntime &playback, VoiceStore *voices,
                              const LiveSettings &settings, unsigned rootMidi) {
    while (!pending_.empty() && pending_.front().due <= now) {
        const auto &note = pending_.front();
        const auto &event = note.observation;
        std::string_view character;
        // Recheck the representative after it has waited. A correction or
        // withdrawal retires its note.
        if (Clock::now() - note.due <= std::chrono::milliseconds(300)) {
            detail::Statement current(
                db, "SELECT 1 FROM events e JOIN revisions r ON "
                    "r.seq=e.revision_seq WHERE e.id=?1 AND "
                    "r.change_kind='new' AND r.live_eligible=1");
            current.bindJson(event.id);
            if (current.next()) {
                const auto voice =
                    voices ? voices->liveSettings(event.species, event.name)
                           : VoiceSettings{};
                const auto pitch =
                    settings.musical
                        ? musicalSpeciesNote(rootMidi, event.species,
                                             settings.composition)
                        : directSpeciesNote(rootMidi, event.species);
                if (playback.submitLive(voice, pitch, 1, 0, event.position).ok)
                    character = voiceCharacterIds[static_cast<std::size_t>(
                        voice.character)];
            }
        }
        if (!character.empty() && settings.musical) {
            if (recent_.size() == 64)
                recent_.erase(recent_.begin());
            recent_.push_back(note.group);
        }
        finish(note, character);
        pending_.pop_front();
    }
}
std::chrono::milliseconds LivePerformance::nextServiceDelay() const {
    using namespace std::chrono;
    if (pending_.empty())
        return milliseconds(100);
    return std::clamp(ceil<milliseconds>(pending_.front().due - Clock::now()),
                      milliseconds(0), milliseconds(100));
}
void LivePerformance::cancel() {
    for (const auto &note : pending_)
        finish(note);
    pending_.clear();
    reservations_.clear();
}
} // namespace atlas
