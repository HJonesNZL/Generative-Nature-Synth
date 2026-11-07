#include "live_activity.hpp"
#include "json_text.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <atlas/presets.hpp>
#include <stdexcept>

namespace atlas {
namespace {
constexpr std::int64_t historyUs = 30000000;
bool sameSettings(const VoiceSettings &a, const VoiceSettings &b) {
    return a.tuningSemitones == b.tuningSemitones &&
           a.attackSeconds == b.attackSeconds && a.decaySeconds == b.decaySeconds &&
           a.gainDb == b.gainDb && a.character == b.character &&
           a.brightness == b.brightness && a.roughness == b.roughness &&
           a.pulseHz == b.pulseHz && a.sweepSemitones == b.sweepSemitones;
}
std::string number(double value) {
    char text[32]{};
    const auto result = std::to_chars(text, text + sizeof(text), value,
                                       std::chars_format::general, 17);
    if (result.ec != std::errc{})
        throw std::runtime_error("Activity position could not be represented");
    return {text, result.ptr};
}
std::string nullable(const std::string &value) {
    return value.empty() ? "null" : detail::quoteJson(value);
}
}
std::int64_t LiveActivityJournal::clockUs() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void LiveActivityJournal::submitted(std::uint64_t preparation,
                                    std::uint64_t token,
                                    const LiveObservation &observation,
                                    const VoiceSettings &settings,
                                    std::string region) {
    if (!token)
        return;
    if (submitted_.size() == 256) {
        ++dropped_;
        return;
    }
    static constexpr std::array<std::string_view, 6> names{
        "Struck tone", "Whistle", "Reed", "Bell", "Air", "Pulse"};
    const auto character = static_cast<std::size_t>(settings.character);
    if (character >= names.size())
        throw std::invalid_argument("Invalid activity voice character");
    Voice voice;
    voice.observation = observation;
    voice.registeredUs = clockUs();
    voice.voiceType = voiceCharacterIds[character];
    voice.voiceName = names[character];
    voice.region = std::move(region);
    for (const auto &preset : speciesPresets())
        if (preset.speciesId == observation.species && sameSettings(preset.settings, settings)) {
            voice.voiceName = std::string(preset.name) + " · " + std::string(names[character]);
            break;
        }
    submitted_.insert_or_assign(Key{preparation, token}, std::move(voice));
}
void LiveActivityJournal::changed(Key key, Voice &voice) {
    voice.sequence = ++sequence_;
    if (deltas_.size() == deltaCapacity) {
        deltas_.pop_front();
        ++dropped_;
    }
    deltas_.push_back({sequence_, key});
}
void LiveActivityJournal::prune(std::int64_t nowUs) {
    for (auto it = submitted_.begin(); it != submitted_.end();) {
        if (it->second.registeredUs < nowUs - historyUs)
            it = submitted_.erase(it);
        else
            ++it;
    }
    for (auto it = voices_.begin(); it != voices_.end();) {
        if (it->second.endUs < nowUs - historyUs) {
            it = voices_.erase(it);
        } else if (!deltas_.empty() && it->second.sequence < deltas_.front().sequence) {
            // Cursor recovery must not repeatedly restart on an entry whose
            // last update has already fallen out of the bounded journal.
            ++dropped_;
            it = voices_.erase(it);
        } else
            ++it;
    }
    while (!deltas_.empty() && !voices_.contains(deltas_.front().key))
        deltas_.pop_front();
}
void LiveActivityJournal::ingest(std::uint64_t preparation,
                                 std::span<const LiveVoiceStart> starts,
                                 std::int64_t nowUs) {
    prune(nowUs);
    for (const auto &start : starts) {
        if (start.endedToken) {
            const Key ended{preparation, start.endedToken};
            if (auto old = voices_.find(ended); old != voices_.end()) {
                old->second.endUs = std::clamp(start.shortenedEndUs,
                                               old->second.onsetUs,
                                               old->second.endUs);
                changed(ended, old->second);
            }
        }
        if (!start.token)
            continue;
        const Key key{preparation, start.token};
        auto metadata = submitted_.find(key);
        if (metadata == submitted_.end()) {
            ++dropped_;
            continue;
        }
        if (voices_.size() == voiceCapacity) {
            const auto oldest = std::min_element(
                voices_.begin(), voices_.end(), [](const auto &a, const auto &b) {
                    return a.second.onsetUs < b.second.onsetUs;
                });
            voices_.erase(oldest);
            ++dropped_;
        }
        auto voice = std::move(metadata->second);
        submitted_.erase(metadata);
        voice.onsetUs = start.onsetUs;
        voice.endUs = std::max(start.onsetUs, start.endUs);
        auto [inserted, fresh] = voices_.insert_or_assign(key, std::move(voice));
        (void)fresh;
        changed(key, inserted->second);
    }
}
void LiveActivityJournal::service(PlaybackRuntime &playback) {
    const auto snapshot = playback.snapshot();
    if (preparation_ != snapshot.preparationId) {
        lostPreviousPreparations_ += lostCurrentPreparation_;
        lostCurrentPreparation_ = 0;
        preparation_ = snapshot.preparationId;
    }
    lostCurrentPreparation_ = snapshot.lostLiveStarts;
    std::array<LiveVoiceStart, 128> starts{};
    for (std::size_t page = 0; page < 32; ++page) {
        const auto count = playback.drainLiveStarts(starts);
        if (!count)
            break;
        ingest(snapshot.preparationId, std::span(starts.data(), count), clockUs());
    }
    prune(clockUs());
}
void LiveActivityJournal::stop(PlaybackRuntime &playback) {
    service(playback);
    stopAt(clockUs());
}
void LiveActivityJournal::stopAt(std::int64_t nowUs) {
    for (auto &[key, voice] : voices_)
        if (voice.endUs > nowUs) {
            voice.endUs = std::max(nowUs, voice.onsetUs);
            changed(key, voice);
        }
    submitted_.clear();
}
void LiveActivityJournal::reset() {
    submitted_.clear();
    voices_.clear();
    deltas_.clear();
    sequence_ = 1;
    dropped_ = lostPreviousPreparations_ = preparation_ = lostCurrentPreparation_ = 0;
}
std::string LiveActivityJournal::view(Key key, const Voice &voice) const {
    const auto &event = voice.observation;
    const auto position = event.position
        ? "{\"latitude\":" + number(event.position->latitude) +
          ",\"longitude\":" + number(event.position->longitude) +
          ",\"uncertaintyMetres\":" +
          (event.uncertaintyMetres ? number(*event.uncertaintyMetres) : "null") + "}"
        : "null";
    return "{\"token\":" + detail::quoteJson(std::to_string(key.first) + ":" + std::to_string(key.second)) +
        ",\"eventId\":" + detail::quoteJson(event.id) +
        ",\"speciesId\":" + nullable(event.species) +
        ",\"commonName\":" + detail::quoteJson(event.name) +
        ",\"voiceName\":" + detail::quoteJson(voice.voiceName) +
        ",\"voiceType\":" + detail::quoteJson(voice.voiceType) +
        ",\"stationId\":" + nullable(event.stationId) +
        ",\"eventKind\":" + detail::quoteJson(event.eventKind) +
        ",\"locationKind\":" + detail::quoteJson(event.locationKind) +
        ",\"locationLabel\":" + detail::quoteJson(event.locationLabel) +
        ",\"region\":" + nullable(voice.region) + ",\"position\":" + position +
        ",\"onsetUs\":" + detail::quoteJson(std::to_string(voice.onsetUs)) +
        ",\"endUs\":" + detail::quoteJson(std::to_string(voice.endUs)) + "}";
}
std::string LiveActivityJournal::page(std::string_view sessionId, bool active,
                                     std::uint64_t after, std::size_t limit,
                                     std::int64_t nowUs) {
    if (!limit || limit > 250)
        throw std::invalid_argument("Activity page limit must be one to 250");
    prune(nowUs);
    const bool reset = active && (after == 0 || after > sequence_ ||
        (!deltas_.empty() && after < deltas_.front().sequence - 1));
    auto next = active ? after : sequence_;
    bool more{};
    std::string rows;
    const auto append = [&](Key key, const Voice &voice) {
        if (!rows.empty()) rows += ',';
        rows += view(key, voice);
    };
    if (active && reset) {
        std::vector<std::pair<Key, const Voice *>> retained;
        retained.reserve(voices_.size());
        for (const auto &[key, voice] : voices_) retained.push_back({key, &voice});
        std::sort(retained.begin(), retained.end(), [](const auto &a, const auto &b) {
            return a.second->sequence < b.second->sequence;
        });
        const auto count = std::min(limit, retained.size());
        for (std::size_t i = 0; i < count; ++i) {
            append(retained[i].first, *retained[i].second);
            next = retained[i].second->sequence;
        }
        more = count < retained.size();
        if (!more) next = sequence_;
    } else if (active) {
        std::size_t examined{};
        for (const auto &delta : deltas_) {
            if (delta.sequence <= after) continue;
            if (examined++ == limit) break;
            next = delta.sequence;
            if (const auto voice = voices_.find(delta.key); voice != voices_.end())
                append(delta.key, voice->second);
        }
        if (examined < limit) next = sequence_;
        more = next < sequence_;
    }
    return "{\"sessionId\":" + detail::quoteJson(sessionId) +
        ",\"sessionActive\":" + (active ? "true" : "false") +
        ",\"clockUs\":" + detail::quoteJson(std::to_string(nowUs)) +
        ",\"nextSequence\":" + detail::quoteJson(std::to_string(next)) +
        ",\"hasMore\":" + (more ? "true" : "false") +
        ",\"reset\":" + (reset ? "true" : "false") +
        ",\"dropped\":" + std::to_string(dropped_ + lostPreviousPreparations_ + lostCurrentPreparation_) +
        ",\"voices\":[" + rows + "]}";
}
} // namespace atlas
