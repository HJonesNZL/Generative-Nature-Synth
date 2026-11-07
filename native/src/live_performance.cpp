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
    if (settings.mode != LiveMode::direct && settings.spatial.enabled) {
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
std::string activityRegion(const LiveSettings &settings,
                           const std::optional<Position> &position) {
    if (!position || settings.mode == LiveMode::direct || !settings.spatial.enabled)
        return {};
    const auto slot = prepareSpatialRoute(settings.spatial, position).regionSlot;
    return slot ? settings.spatial.regions[slot - 1].name : std::string{};
}
bool currentEligible(sqlite3 *db, const std::string &eventId,
                     const LiveSessionBoundary *session) {
    detail::Statement revision(
        db, "SELECT r.seq,r.observed_at FROM events e JOIN revisions r "
            "ON r.seq=e.revision_seq WHERE e.id=?1 AND "
            "((r.change_kind='new' AND r.live_eligible=1) OR "
            "(r.change_kind='baseline' AND ?2=1))");
    revision.bindJson(eventId);
    revision.bind(2, static_cast<std::int64_t>(session != nullptr));
    if (!revision.next())
        return false;
    return !session || (revision.integer(0) > session->baselineRevision &&
                        parseUtc(revision.text(1)) >= parseUtc(session->observedFromUtc));
}
} // namespace
const LiveOutcome *LivePerformance::outcome(const std::string &id) const {
    const auto it = outcomes_.find(id);
    return it == outcomes_.end() ? nullptr : &it->second;
}
void LivePerformance::accept(std::vector<LiveObservation> observations,
                             const LiveSettings &settings,
                             Clock::time_point now,
                             std::span<const LiveSmoothTiming> timing) {
    if (observations.empty())
        return;
    std::vector<LiveArrangementCandidate> candidates;
    candidates.reserve(observations.size());
    for (const auto &event : observations) {
        auto [key, label] = liveRegion(settings, event.position);
        if (settings.mode != LiveMode::smoothed)
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
    if (settings.mode == LiveMode::smoothed) {
        queueSmoothed(observations, timing, now);
        return;
    }
    std::vector<LiveArrangementDecision> decisions;
    if (settings.mode == LiveMode::musical) {
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
        if (settings.mode == LiveMode::musical)
            reservations_.push_back(now);
    }
    std::stable_sort(
        pending_.begin(), pending_.end(),
        [](const Note &a, const Note &b) { return a.due < b.due; });
}
void LivePerformance::queueSmoothed(std::vector<LiveObservation> &observations,
                                    std::span<const LiveSmoothTiming> timing,
                                    Clock::time_point now) {
    if (timing.size() != observations.size() ||
        observations.size() > smoothCapacity())
        throw std::logic_error("Invalid smoothed playback page");
    for (std::size_t i = 0; i < observations.size(); ++i) {
        const auto &row = timing[i];
        auto [entry, inserted] = sourceWindows_.try_emplace(
            row.sourceId, SourceWindow{row.ingestedUs, now, now});
        auto &window = entry->second;
        if (!inserted && window.ingestion != row.ingestedUs)
            window = {row.ingestedUs, std::max(now, window.lastDue),
                      window.lastDue};
        const auto due = window.start + std::chrono::microseconds(row.offsetUs);
        window.lastDue = std::max(window.lastDue, due);
        pending_.push_back(
            {std::move(observations[i]), {}, window.lastDue, {}});
    }
    std::stable_sort(
        pending_.begin(), pending_.end(),
        [](const Note &a, const Note &b) { return a.due < b.due; });
}
void LivePerformance::acknowledge(const PlaybackSnapshot &snapshot) {
    while (!inFlight_.empty() &&
           inFlight_.front().id <= snapshot.liveAcknowledgedId) {
        finish(inFlight_.front().note, inFlight_.front().character);
        inFlight_.pop_front();
    }
}
void LivePerformance::retryHandedOff() {
    while (!inFlight_.empty()) {
        pending_.push_front(std::move(inFlight_.back().note));
        inFlight_.pop_back();
    }
}
void LivePerformance::submitSmoothed(Clock::time_point now, sqlite3 *db,
                                     PlaybackRuntime &playback,
                                     VoiceStore *voices, const LiveSettings &settings, unsigned rootMidi,
                                     const LiveSessionBoundary *session,
                                     LiveActivityJournal *activity) {
    const auto snap = playback.snapshot();
    acknowledge(snap);
    // Keep decisions near the output, so changes and withdrawals can retire
    // waiting notes.
    constexpr std::size_t handoffLimit = 4;
    const auto limit = handoffLimit - inFlight_.size();
    std::array<LiveNoteInput, handoffLimit> notes{};
    std::size_t n = 0;
    for (auto it = pending_.begin();
         it != pending_.end() && n < limit &&
         it->due <= now + std::chrono::milliseconds(30);) {
        const auto &event = it->observation;
        if (!currentEligible(db, event.id, session)) {
            finish(*it, {});
            it = pending_.erase(it);
            continue;
        }
        const auto delay =
            std::max(0.0, std::chrono::duration<double>(it->due - now).count());
        auto &note = notes[n];
        note.id = nextId_ + n;
        note.voice = voices ? voices->liveSettings(event.species, event.name)
                            : VoiceSettings{};
        note.midiNote = directSpeciesNote(rootMidi, event.species);
        note.position = event.position;
        note.notBeforeFrame =
            snap.positionFrames +
            static_cast<std::uint64_t>(std::ceil(delay * snap.sampleRate));
        ++n;
        ++it;
    }
    if (!n)
        return;
    const auto reply = playback.submitLiveBatch(
        std::span<const LiveNoteInput>{notes.data(), n});
    for (std::size_t i = 0; i < reply.accepted; ++i) {
        if (activity)
            activity->submitted(snap.preparationId, notes[i].id,
                                pending_.front().observation, notes[i].voice,
                                activityRegion(settings, pending_.front().observation.position));
        inFlight_.push_back({std::move(pending_.front()), notes[i].id,
                             voiceCharacterIds[static_cast<std::size_t>(
                                 notes[i].voice.character)]});
        pending_.pop_front();
    }
    nextId_ += reply.accepted;
    if (reply.error != PlaybackError::none)
        throw std::runtime_error("Smoothed playback submission failed");
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
                              const LiveSettings &settings, unsigned rootMidi,
                              const LiveSessionBoundary *session,
                              LiveActivityJournal *activity) {
    if (settings.mode == LiveMode::smoothed) {
        submitSmoothed(now, db, playback, voices, settings, rootMidi, session, activity);
        return;
    }
    while (!pending_.empty() && pending_.front().due <= now) {
        const auto &note = pending_.front();
        const auto &event = note.observation;
        std::string_view character;
        // Recheck the representative after it has waited. A correction or
        // withdrawal retires its note.
        if (Clock::now() - note.due <= std::chrono::milliseconds(300) &&
            currentEligible(db, event.id, session)) {
            const auto voice =
                voices ? voices->liveSettings(event.species, event.name)
                       : VoiceSettings{};
            const auto pitch = (settings.mode == LiveMode::musical)
                                   ? musicalSpeciesNote(rootMidi, event.species,
                                                        settings.composition)
                                   : directSpeciesNote(rootMidi, event.species);
            const auto preparation = playback.snapshot().preparationId;
            if (playback.submitLive(voice, pitch, 1, 0, event.position, nextId_).ok) {
                if (activity)
                    activity->submitted(preparation, nextId_, event, voice,
                                        activityRegion(settings, event.position));
                ++nextId_;
                character = voiceCharacterIds[static_cast<std::size_t>(voice.character)];
            }
        }
        if (!character.empty() && settings.mode == LiveMode::musical) {
            if (recent_.size() == 64)
                recent_.erase(recent_.begin());
            recent_.push_back(note.group);
        }
        finish(note, character);
        pending_.pop_front();
    }
}
std::chrono::milliseconds
LivePerformance::nextServiceDelay(bool smoothed) const {
    using namespace std::chrono;
    if (smoothed && (!pending_.empty() || !inFlight_.empty())) {
        if (pending_.empty() || inFlight_.size() >= 4)
            return milliseconds(8);
        return std::clamp(
            ceil<milliseconds>(pending_.front().due - Clock::now()) -
                milliseconds(30),
            milliseconds(5), milliseconds(100));
    }
    if (pending_.empty())
        return milliseconds(100);
    return std::clamp(ceil<milliseconds>(pending_.front().due - Clock::now()),
                      milliseconds(0), milliseconds(100));
}
void LivePerformance::cancel() {
    retryHandedOff();
    sourceWindows_.clear();
    for (const auto &note : pending_)
        finish(note);
    pending_.clear();
    reservations_.clear();
}
} // namespace atlas
