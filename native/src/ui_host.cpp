#include "event_validation.hpp"
#include "json_text.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <atlas/playback.hpp>
#include <atlas/storage.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace atlas {
namespace {
using detail::quoteJson;
using detail::Statement;
std::int64_t integer(std::string_view value, std::int64_t maximum) {
    std::int64_t result{};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || result < 0 ||
        result > maximum)
        throw std::invalid_argument("integer outside supported range");
    return result;
}
class Object {
  public:
    Object(sqlite3 *db, std::string_view json,
           std::initializer_list<std::string_view> allowed = {}) {
        Statement root(db, "SELECT json_valid(?1),CASE WHEN json_valid(?1) "
                           "THEN json_type(?1) ELSE 'invalid' END");
        root.bindJson(json);
        if (!root.next() || root.integer(0) != 1 || root.text(1) != "object")
            throw std::invalid_argument("JSON object required");
        Statement fields(
            db, "SELECT key,type,CAST(value AS TEXT),value FROM json_each(?1)");
        fields.bindJson(json);
        while (fields.next()) {
            const auto key = fields.text(0);
            if (allowed.size() &&
                std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                throw std::invalid_argument("unexpected request field");
            if (!fields_
                     .emplace(key,
                              Field{fields.text(1),
                                    fields.optionalText(2).value_or("null"),
                                    fields.number(3)})
                     .second)
                throw std::invalid_argument("duplicate JSON field");
        }
    }
    std::string text(std::string_view key, std::size_t maximum = 512) const {
        const auto &field = at(key);
        if (field.type != "text" || field.value.size() > maximum ||
            field.value.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid text argument");
        return field.value;
    }
    std::string optionalText(std::string_view key) const {
        return null(key) ? "" : text(key, 2048);
    }
    std::string object(std::string_view key) const {
        const auto &field = at(key);
        if (field.type != "object")
            throw std::invalid_argument("object argument required");
        return field.value;
    }
    std::int64_t number(std::string_view key, std::int64_t maximum) const {
        const auto &field = at(key);
        if (field.type != "integer")
            throw std::invalid_argument("integer argument required");
        return integer(field.value, maximum);
    }
    double real(std::string_view key) const {
        const auto &field = at(key);
        if (field.type != "real" && field.type != "integer")
            throw std::invalid_argument("numeric argument required");
        if (!std::isfinite(field.numeric))
            throw std::invalid_argument("finite numeric argument required");
        return field.numeric;
    }
    bool boolean(std::string_view key) const {
        const auto &field = at(key);
        if (field.type != "true" && field.type != "false")
            throw std::invalid_argument("boolean argument required");
        return field.type == "true";
    }
    bool null(std::string_view key) const {
        const auto i = fields_.find(std::string(key));
        return i == fields_.end() || i->second.type == "null";
    }
    std::string json(std::string_view key) const {
        if (null(key))
            return "null";
        const auto &field = at(key);
        if (field.type == "text")
            return quoteJson(field.value);
        if (field.type == "true" || field.type == "false")
            return field.type;
        return field.value;
    }
    std::size_t size() const { return fields_.size(); }

  private:
    struct Field {
        std::string type, value;
        double numeric{};
    };
    std::map<std::string, Field> fields_;
    const Field &at(std::string_view key) const {
        const auto i = fields_.find(std::string(key));
        if (i == fields_.end())
            throw std::invalid_argument("missing request field");
        return i->second;
    }
};
struct CloseDb {
    void operator()(sqlite3 *db) const {
        if (db)
            sqlite3_close(db);
    }
};
using Database = std::unique_ptr<sqlite3, CloseDb>;
Database open(const std::filesystem::path &path) {
    const auto utf8 = path.u8string();
    const std::string filename(reinterpret_cast<const char *>(utf8.data()),
                               utf8.size());
    sqlite3 *raw{};
    const auto status =
        sqlite3_open_v2(filename.c_str(), &raw,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
    Database db(raw);
    if (status != SQLITE_OK)
        throw std::runtime_error("library unavailable");
    sqlite3_busy_timeout(db.get(), 5000);
    return db;
}
void append(std::string &array, std::string_view value) {
    if (array.size() > 1)
        array += ',';
    array += value;
    if (array.size() > 8 * 1024 * 1024)
        throw std::runtime_error("presentation reply exceeds capacity");
}
std::string eventView(sqlite3 *db, std::string_view json,
                      std::int64_t sequence) {
    if (sequence < 1)
        throw std::runtime_error("invalid revision sequence");
    detail::validateStoredEvent(db, json);
    const Object e(db, json);
    const auto position =
        e.null("latitude")
            ? "null"
            : "{\"latitude\":" + e.json("latitude") +
                  ",\"longitude\":" + e.json("longitude") +
                  ",\"uncertaintyMetres\":" + e.json("uncertainty_m") + "}";
    const auto confidence =
        e.null("confidence")
            ? "null"
            : "{\"value\":" + e.json("confidence") +
                  ",\"meaning\":" + e.json("confidence_meaning") + "}";
    return "{\"id\":" + e.json("event_id") +
           ",\"revisionSequence\":" + quoteJson(std::to_string(sequence)) +
           ",\"sourceId\":" + e.json("source_id") +
           ",\"speciesId\":" + e.json("species_id") +
           ",\"commonName\":" + e.json("common_name") +
           ",\"individualId\":" + e.json("individual_id") +
           ",\"deploymentId\":" + e.json("deployment_id") +
           ",\"stationId\":" + e.json("station_id") +
           ",\"eventKind\":" + e.json("event_kind") +
           ",\"locationKind\":" + e.json("location_kind") +
           ",\"position\":" + position +
           ",\"observedAtUtc\":" + e.json("observed_at_utc") +
           ",\"firstSeenAtUtc\":" + e.json("first_seen_at_utc") +
           ",\"confidence\":" + confidence +
           ",\"attribution\":" + e.json("attribution") + ",\"qualityFlags\":" +
           (e.null("quality_flags") ? "[]" : e.json("quality_flags")) +
           ",\"provenance\":" + e.json("provenance_mode") + "}";
}
std::uint64_t frameMicroseconds(std::uint64_t frames, std::uint32_t rate) {
    return rate ? (frames / rate) * 1000000 + (frames % rate) * 1000000 / rate
                : 0;
}
std::string preparationId(const PlaybackSnapshot &snapshot) {
    return snapshot.prepared ? "audio-" + std::to_string(snapshot.preparationId)
                             : "";
}
std::string transportView(const PlaybackSnapshot &snapshot) {
    const auto state = snapshot.state == AudioState::paused ? "paused"
                       : snapshot.state == AudioState::playing ||
                               snapshot.state == AudioState::pausing ||
                               snapshot.state == AudioState::draining
                           ? "playing"
                           : "stopped";
    return "{\"preparationId\":" +
           (snapshot.prepared ? quoteJson(preparationId(snapshot)) : "null") +
           ",\"generation\":" + quoteJson(std::to_string(snapshot.generation)) +
           ",\"state\":" + quoteJson(state) + ",\"positionUs\":" +
           quoteJson(std::to_string(frameMicroseconds(snapshot.positionFrames,
                                                      snapshot.sampleRate))) +
           ",\"durationUs\":" +
           quoteJson(std::to_string(
               frameMicroseconds(snapshot.totalFrames, snapshot.sampleRate))) +
           "}";
}
const char *playbackMessage(PlaybackError error) noexcept {
    switch (error) {
    case PlaybackError::none:
        return "";
    case PlaybackError::invalidVoiceSettings:
        return "Voice settings are outside supported limits.";
    case PlaybackError::notPrepared:
        return "Select a saved piece before enabling audio.";
    case PlaybackError::disabled:
        return "Enable Listening before playing this piece.";
    case PlaybackError::stalePreparation:
    case PlaybackError::staleGeneration:
        return "Playback preparation changed. Select the saved piece again.";
    case PlaybackError::invalidTransition:
        return "Playback is changing state. Wait briefly and try again.";
    case PlaybackError::deviceUnavailable:
        return "The default audio output could not be opened. Check the device "
               "and enable Listening again.";
    case PlaybackError::unsupportedDevice:
        return "The default output does not support the prepared stereo sample "
               "rate or buffer size.";
    case PlaybackError::deviceChanged:
        return "Audio output changed. Playback stopped; enable Listening to "
               "reconnect.";
    case PlaybackError::callbackFailure:
        return "Audio output stopped because its callback format was invalid.";
    case PlaybackError::commandTimeout:
        return "Audio output did not acknowledge the command; playback has "
               "stopped.";
    case PlaybackError::leaseExpired:
        return "The UI connection expired. Playback stopped; enable Listening "
               "to reconnect.";
    }
    return "Audio output is unavailable.";
}
std::string audioView(const PlaybackSnapshot &snapshot) {
    const auto status = snapshot.error != PlaybackError::none ? "device_lost"
                        : snapshot.enabled                    ? "running"
                                                              : "disabled";
    return "{\"status\":" + quoteJson(status) +
           ",\"deviceName\":null,\"masterGainDb\":" +
           std::to_string(snapshot.gainDb) +
           ",\"peakDbfs\":null,\"meterIsSimulated\":false,\"clipping\":false,"
           "\"activeSynthVoices\":null,\"maxSynthVoices\":8," +
           "\"sampleRate\":" +
           (snapshot.sampleRate ? std::to_string(snapshot.sampleRate)
                                : "null") +
           ",\"error\":" +
           (snapshot.error == PlaybackError::none
                ? "null"
                : quoteJson(playbackMessage(snapshot.error))) +
           "}";
}
std::string
boundsJson(const std::optional<std::pair<Microseconds, Microseconds>> &bounds) {
    return bounds ? "[" + quoteJson(std::to_string(bounds->first)) + "," +
                        quoteJson(std::to_string(bounds->second)) + "]"
                  : "null";
}
std::string decimal(double value) {
    std::array<char, 64> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{})
        throw std::runtime_error("numeric serialization failed");
    return {buffer.data(), result.ptr};
}
std::string voiceView(const VoiceSnapshot &voice) {
    std::string values = "{";
    for (std::size_t i = 0; i < voiceParameterIds.size(); ++i) {
        if (i)
            values += ',';
        values += quoteJson(voiceParameterIds[i]) + ":" +
                  decimal(voice.normalizedValues[i]);
    }
    return "{\"id\":" + quoteJson(voice.id) +
           ",\"name\":" + quoteJson(voice.name) +
           ",\"scope\":" + quoteJson(voice.speciesId ? "species" : "group") +
           ",\"targetId\":" + quoteJson(voice.speciesId.value_or("native")) +
           ",\"inheritsFromId\":null,\"type\":\"struck\",\"values\":" + values +
           "},\"overriddenParameterIds\":[],\"savedRevision\":" +
           quoteJson(std::to_string(voice.savedRevision)) +
           ",\"changeRevision\":" +
           quoteJson(std::to_string(voice.changeRevision)) +
           ",\"dirty\":" + (voice.dirty ? "true" : "false") + "}";
}
std::string sessionView(const SavedSession &s) {
    return "{\"id\":" + quoteJson(s.id) + ",\"name\":" + quoteJson(s.name) +
           ",\"state\":" + quoteJson(s.state) +
           ",\"origin\":" + quoteJson(s.origin) +
           ",\"eventCount\":" + std::to_string(s.eventCount) +
           ",\"captureTruncated\":" + (s.truncated ? "true" : "false") +
           ",\"metadataIntegrity\":" + quoteJson(s.metadataIntegrity) +
           ",\"observedWindowPolicy\":" + quoteJson(s.observedWindowPolicy) +
           ",\"observedBoundsUs\":" + boundsJson(s.observedBounds) +
           ",\"arrivalBoundsUs\":" + boundsJson(s.arrivalBounds) + "}";
}
} // namespace

struct UiHost::Impl {
    ReadOnlyLibrary library;
    Database db;
    std::optional<Timeline> prepared;
    SavedSession saved;
    std::optional<Timeline> material;
    SavedSession materialSaved;
    std::uint64_t generation{};
    PlaybackRuntime *playback{};
    VoiceStore *voices{};
    std::map<std::string, VoiceSlot> voiceSlots;
    Impl(const std::filesystem::path &path, PlaybackRuntime *output,
         VoiceStore *presets)
        : library(path), db(open(path)), playback(output), voices(presets) {}
    void registerPayloads(const std::vector<std::string> &payloads) {
        if (!voices)
            return;
        std::map<std::string, std::string> unique;
        for (const auto &payload : payloads) {
            detail::validateStoredEvent(db.get(), payload);
            const Object e(db.get(), payload);
            const auto id = e.optionalText("species_id");
            if (!id.empty())
                unique.try_emplace(id, e.optionalText("common_name"));
        }
        std::vector<VoiceSpecies> species;
        for (const auto &[id, name] : unique)
            species.push_back({id, name.empty() ? id : name});
        voices->registerSpecies(species);
    }
    void applyVoice(const VoiceSnapshot &next, const VoiceSnapshot &rollback) {
        const auto slot = voiceSlots.find(next.id);
        if (!playback || slot == voiceSlots.end())
            return;
        const auto result = playback->updateVoice(slot->second, next.settings);
        if (!result.ok) {
            // A timed-out callback may already have applied the command.
            // Quiesce and restore the authoritative draft before reporting
            // failure.
            playback->enable(false);
            const auto restored =
                playback->updateVoice(slot->second, rollback.settings);
            if (!restored.ok)
                throw std::runtime_error("Voice audio recovery failed");
            throw std::runtime_error(playbackMessage(result.error));
        }
    }
    std::string voiceLibrary() {
        (void)catalog(Object(db.get(), "{}"), true);
        std::string species = "[";
        for (const auto &voice : voices->voices()) {
            if (!voice.speciesId)
                continue;
            append(species, "{\"speciesId\":" + quoteJson(*voice.speciesId) +
                                ",\"commonName\":" + quoteJson(voice.name) +
                                ",\"scientificName\":\"\",\"voiceId\":" +
                                quoteJson(voice.id) + ",\"individuals\":[]}");
        }
        return "{\"entries\":[{\"groupId\":\"native\",\"groupName\":\"Species "
               "voices\",\"groupVoiceId\":\"default\",\"species\":" +
               species + "]}],\"parameters\":" + parameterSpecs() +
               ",\"types\":[{\"id\":\"struck\",\"label\":\"Struck "
               "tone\",\"proposal\":false}],\"maxDefinitions\":513,"
               "\"editingBehavior\":\"new_notes\"}";
    }
    std::string parameterSpecs();
    void load(std::string_view id, TimeBasis basis = TimeBasis::observed,
              Speed speed = {}) {
        // Stop prior sound before verification, including when the new
        // preparation fails.
        if (playback)
            playback->enable(false);
        // Replace only after complete verification; a failed preparation never
        // exposes new material.
        SavedSession metadata;
        auto next = library.load(id, basis, speed, &metadata);
        if (playback) {
            std::vector<VoiceProfile> profiles;
            std::map<std::string, VoiceSlot> nextVoiceSlots;
            if (voices) {
                std::vector<std::string> payloads;
                for (const auto &event : next.events())
                    payloads.push_back(event.normalizedJson);
                registerPayloads(payloads);
                profiles.push_back({"", voices->getVoice("default").settings});
                nextVoiceSlots.emplace("default", 0);
                std::set<std::string> species;
                for (const auto &event : next.events())
                    if (event.speciesId && !event.speciesId->empty())
                        species.insert(*event.speciesId);
                for (const auto &id : species) {
                    const auto voice = voices->getVoice("species:" + id);
                    nextVoiceSlots.emplace(
                        voice.id, static_cast<VoiceSlot>(profiles.size()));
                    profiles.push_back({id, voice.settings});
                }
            }
            AudioSettings settings;
            settings.masterDb = -18;
            const auto result =
                playback->prepare(next, settings, profiles, voices != nullptr);
            if (!result.ok)
                throw std::runtime_error(playbackErrorText(result.error));
            voiceSlots = std::move(nextVoiceSlots);
        }
        prepared = std::move(next);
        saved = std::move(metadata);
        ++generation;
    }
    void loadMaterial(std::string_view id) {
        if (material && materialSaved.id == id)
            return;
        SavedSession metadata;
        auto next = library.load(id, TimeBasis::observed, {}, &metadata);
        material = std::move(next);
        materialSaved = std::move(metadata);
    }
    std::string changes(const Object &args) {
        const auto limit = args.number("limit", 500);
        if (limit < 1)
            throw std::invalid_argument("positive page limit required");
        const auto after = integer(args.text("afterSequence", 20), INT64_MAX);
        const auto requestedStore = args.text("storeId", 128);
        // One statement pins store identity, high water and rows to a single
        // read snapshot.
        Statement head(db.get(),
                       "SELECT value,(SELECT COALESCE(MAX(seq),0) FROM "
                       "revisions) FROM metadata WHERE key='store_id'");
        if (!head.next())
            throw std::runtime_error("library identity unavailable");
        const auto storeId = head.text(0);
        const auto high = head.integer(1);
        const bool baseline = requestedStore != storeId || after > high;
        const auto sql =
            baseline ? "SELECT seq,event_json,change_kind,live_eligible FROM "
                       "(SELECT * FROM revisions WHERE seq<=" +
                           std::to_string(high) + " ORDER BY seq DESC LIMIT " +
                           std::to_string(limit) + ") ORDER BY seq"
                     : "SELECT seq,event_json,change_kind,live_eligible FROM "
                       "revisions WHERE seq>" +
                           std::to_string(after) +
                           " AND seq<=" + std::to_string(high) +
                           " ORDER BY seq LIMIT " + std::to_string(limit);
        Statement rows(db.get(), sql.c_str());
        std::string events = "[";
        auto next = baseline ? high : after;
        while (rows.next()) {
            const auto seq = rows.integer(0);
            const auto kind = rows.text(2);
            if (kind != "new" && kind != "corrected" && kind != "baseline")
                throw std::runtime_error("invalid revision kind");
            append(events,
                   "{\"event\":" + eventView(db.get(), rows.text(1), seq) +
                       ",\"liveEligible\":" +
                       (!baseline && kind == "new" && rows.integer(3) == 1
                            ? "true"
                            : "false") +
                       ",\"change\":" +
                       quoteJson(baseline ? "baseline" : kind) + "}");
            if (!baseline)
                next = seq;
        }
        return "{\"storeId\":" + quoteJson(storeId) +
               ",\"nextSequence\":" + quoteJson(std::to_string(next)) +
               ",\"events\":" + events + "]}";
    }
    std::string catalog(const Object &args, bool registerVoices = false) {
        std::vector<std::string> payloads;
        if (!args.null("material")) {
            Object selection(db.get(), args.object("material"),
                             {"kind", "sessionId"});
            if (selection.text("kind") != "session")
                throw std::invalid_argument("only saved material is supported");
            const auto id = selection.text("sessionId", 128);
            loadMaterial(id);
            for (const auto &e : material->events()) {
                if (payloads.size() == 2000)
                    break;
                payloads.push_back(e.normalizedJson);
            }
        } else {
            Statement rows(
                db.get(),
                "SELECT r.event_json FROM events e JOIN revisions r ON "
                "r.seq=e.revision_seq ORDER BY r.seq DESC LIMIT 2000");
            while (rows.next())
                payloads.push_back(rows.text(0));
        }
        if (registerVoices)
            registerPayloads(payloads);
        std::map<std::string, std::string> species, stations, sources;
        bool limited = payloads.size() == 2000;
        for (const auto &payload : payloads) {
            detail::validateStoredEvent(db.get(), payload);
            const Object e(db.get(), payload);
            const auto source = e.text("source_id", 128),
                       speciesId = e.optionalText("species_id"),
                       station = e.optionalText("station_id");
            if (!speciesId.empty() && species.size() < 500) {
                const auto common = e.optionalText("common_name");
                species.try_emplace(
                    speciesId,
                    "{\"id\":" + quoteJson(speciesId) + ",\"commonName\":" +
                        quoteJson(common.empty() ? speciesId : common) +
                        ",\"scientificName\":" +
                        quoteJson(e.optionalText("scientific_name")) +
                        ",\"group\":\"unclassified\"}");
            }
            const auto locationKind = e.text("location_kind");
            const bool receiver = locationKind == "station_detection" ||
                                  locationKind == "receiver_detection";
            if (receiver && !station.empty() && !e.null("latitude") &&
                stations.size() < 500)
                stations.try_emplace(
                    station, "{\"id\":" + quoteJson(station) +
                                 ",\"name\":" + quoteJson(station) +
                                 ",\"sourceId\":" + quoteJson(source) +
                                 ",\"latitude\":" + e.json("latitude") +
                                 ",\"longitude\":" + e.json("longitude") +
                                 ",\"positionDescription\":\"Reported station "
                                 "position; individual position unknown\"}");
            if (sources.size() < 128)
                sources.try_emplace(
                    source, "{\"id\":" + quoteJson(source) +
                                ",\"name\":" + quoteJson(source) +
                                ",\"measurement\":\"Provider records; inspect "
                                "event kind\",\"attribution\":" +
                                quoteJson(e.optionalText("attribution")) + "}");
        }
        limited = limited || species.size() == 500 || stations.size() == 500 ||
                  sources.size() == 128;
        const auto array = [](const auto &entries) {
            std::string result = "[";
            for (const auto &[id, json] : entries) {
                (void)id;
                append(result, json);
            }
            return result + "]";
        };
        return "{\"species\":" + array(species) +
               ",\"stations\":" + array(stations) +
               ",\"sources\":" + array(sources) +
               ",\"limited\":" + (limited ? "true" : "false") + "}";
    }
};
std::string UiHost::Impl::parameterSpecs() {
    std::string result = "[";
    for (const auto &spec : voiceParameterSpecs) {
        auto json = "{\"id\":" + quoteJson(spec.id) +
                    ",\"label\":" + quoteJson(spec.label) +
                    ",\"unit\":" + quoteJson(spec.unit) +
                    ",\"curve\":" + quoteJson(spec.curve) +
                    ",\"min\":" + decimal(spec.min) +
                    ",\"max\":" + decimal(spec.max) +
                    ",\"defaultValue\":" + decimal(spec.defaultValue) +
                    ",\"proposal\":false";
        if (spec.step > 0)
            json += ",\"step\":" + decimal(spec.step);
        append(result, json + "}");
    }
    return result + "]";
}
UiHost::UiHost(const std::filesystem::path &library, PlaybackRuntime *playback,
               VoiceStore *voices)
    : impl_(std::make_unique<Impl>(library, playback, voices)) {}
UiHost::~UiHost() = default;

std::string UiHost::request(std::string_view envelope) {
    std::string id;
    const auto failure = [&](std::string_view code, std::string_view message) {
        return "{\"requestId\":" + quoteJson(id) +
               ",\"ok\":false,\"error\":{\"code\":" + quoteJson(code) +
               ",\"message\":" + quoteJson(message) + "}}";
    };
    try {
        if (envelope.size() > 16384)
            throw std::invalid_argument("request exceeds capacity");
        Object request(impl_->db.get(), envelope,
                       {"protocolVersion", "requestId", "method", "args"});
        id = request.text("requestId", 128);
        if (id.empty() || request.number("protocolVersion", 1) != 1)
            throw std::invalid_argument("invalid protocol or request ID");
        const auto method = request.text("method", 64),
                   arguments = request.object("args");
        std::string result;
        if (method == "getCapabilities") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            result =
                R"({"protocolVersion":1,"implementation":"native","supportedMethods":["getCapabilities","getChanges","getCatalog","listSessions","prepareReplay","getMaterialEvents")";
            if (impl_->playback)
                result +=
                    R"(,"getTransport","setTransport","getAudioState","setAudioEnabled")";
            if (impl_->voices)
                result +=
                    R"(,"getVoiceLibrary","getVoice","setVoiceParameter","saveVoice","revertVoice")";
            result += std::string("],\"audioAvailable\":") +
                      (impl_->playback ? "true" : "false") +
                      ",\"maxEventBatch\":500,\"maxVisibleMarkers\":500}";
        } else if (impl_->playback &&
                   (method == "getTransport" || method == "getAudioState")) {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            impl_->playback->service();
            if (method == "getTransport")
                impl_->playback->heartbeat();
            const auto snapshot = impl_->playback->snapshot();
            result = method == "getTransport" ? transportView(snapshot)
                                              : audioView(snapshot);
        } else if (impl_->playback && method == "setAudioEnabled") {
            Object args(impl_->db.get(), arguments, {"enabled"});
            const auto reply = impl_->playback->enable(args.boolean("enabled"));
            if (!reply.ok)
                return failure("unavailable", playbackMessage(reply.error));
            result = audioView(reply.snapshot);
        } else if (impl_->playback && method == "setTransport") {
            Object args(
                impl_->db.get(), arguments,
                {"action", "preparationId", "generation", "positionUs"});
            const auto action = args.text("action", 16);
            PlaybackCommand command;
            if (action == "play")
                command.kind = PlaybackCommandKind::play;
            else if (action == "pause")
                command.kind = PlaybackCommandKind::pause;
            else if (action == "stop")
                command.kind = PlaybackCommandKind::stop;
            else if (action == "seek")
                command.kind = PlaybackCommandKind::seek;
            else if (action == "panic")
                command.kind = PlaybackCommandKind::panic;
            else
                throw std::invalid_argument("unknown transport action");
            const bool safety = action == "stop" || action == "panic";
            if (!safety || !args.null("preparationId") ||
                !args.null("generation")) {
                const auto idText = args.text("preparationId", 32);
                if (!idText.starts_with("audio-"))
                    return failure("stale_generation",
                                   "Playback preparation is no longer current");
                command.preparationId = static_cast<std::uint64_t>(
                    integer(std::string_view(idText).substr(6), INT64_MAX));
                command.generation = static_cast<std::uint64_t>(
                    integer(args.text("generation", 20), INT64_MAX));
                if (!command.preparationId || !command.generation)
                    throw std::invalid_argument(
                        "positive playback identity required");
            }
            if (action == "seek") {
                const auto position = static_cast<std::uint64_t>(
                    integer(args.text("positionUs", 20), INT64_MAX));
                const auto snapshot = impl_->playback->snapshot();
                if (position > frameMicroseconds(snapshot.totalFrames,
                                                 snapshot.sampleRate))
                    throw std::invalid_argument("seek exceeds prepared audio");
                command.frame =
                    (position / 1000000) * snapshot.sampleRate +
                    ((position % 1000000) * snapshot.sampleRate) / 1000000;
            } else if (!args.null("positionUs"))
                throw std::invalid_argument("position is only valid for seek");
            const auto reply = impl_->playback->command(command);
            if (!reply.ok) {
                const bool stale =
                    reply.error == PlaybackError::stalePreparation ||
                    reply.error == PlaybackError::staleGeneration;
                return failure(stale ? "stale_generation" : "unavailable",
                               playbackMessage(reply.error));
            }
            result = transportView(reply.snapshot);
        } else if (impl_->voices && method == "getVoiceLibrary") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            result = impl_->voiceLibrary();
        } else if (impl_->voices && method == "getVoice") {
            Object args(impl_->db.get(), arguments, {"voiceId"});
            result = voiceView(impl_->voices->getVoice(args.text("voiceId")));
        } else if (impl_->voices && method == "setVoiceParameter") {
            Object args(
                impl_->db.get(), arguments,
                {"voiceId", "parameterId", "normalizedValue", "gesture"});
            const auto voiceId = args.text("voiceId"),
                       parameter = args.text("parameterId"),
                       phase = args.text("gesture");
            const auto value = args.real("normalizedValue");
            VoiceGesture gesture;
            if (phase == "begin")
                gesture = VoiceGesture::begin;
            else if (phase == "change")
                gesture = VoiceGesture::change;
            else if (phase == "end")
                gesture = VoiceGesture::end;
            else
                throw std::invalid_argument("unknown gesture phase");
            const auto before = impl_->voices->getVoice(voiceId);
            const auto preview = impl_->voices->previewParameter(
                voiceId, parameter, value, gesture);
            try {
                impl_->applyVoice(preview, before);
            } catch (const std::exception &error) {
                // A failed end must still release the gesture at the last
                // acknowledged value, otherwise Save/Revert remain blocked.
                if (gesture == VoiceGesture::end) {
                    const auto index = static_cast<std::size_t>(
                        std::find(voiceParameterIds.begin(),
                                  voiceParameterIds.end(), parameter) -
                        voiceParameterIds.begin());
                    impl_->voices->setParameter(voiceId, parameter,
                                                before.normalizedValues[index],
                                                VoiceGesture::end);
                }
                return failure("unavailable", error.what());
            }
            const auto voice =
                impl_->voices->setParameter(voiceId, parameter, value, gesture);
            const auto index = static_cast<std::size_t>(
                std::find(voiceParameterIds.begin(), voiceParameterIds.end(),
                          parameter) -
                voiceParameterIds.begin());
            result = "{\"normalizedValue\":" +
                     decimal(voice.normalizedValues[index]) +
                     ",\"changeRevision\":" +
                     quoteJson(std::to_string(voice.changeRevision)) +
                     ",\"dirty\":" + (voice.dirty ? "true" : "false") + "}";
        } else if (impl_->voices && method == "saveVoice") {
            Object args(impl_->db.get(), arguments,
                        {"voiceId", "expectedRevision"});
            result = voiceView(impl_->voices->save(
                args.text("voiceId"),
                static_cast<std::uint64_t>(
                    integer(args.text("expectedRevision", 20), INT64_MAX))));
        } else if (impl_->voices && method == "revertVoice") {
            Object args(impl_->db.get(), arguments, {"voiceId"});
            const auto voice = impl_->voices->revert(args.text("voiceId"));
            try {
                impl_->applyVoice(voice, voice);
            } catch (const std::exception &) {
                return failure(
                    "unavailable",
                    "Voice reverted, but audio stopped during the update. Read "
                    "the voice again and enable Listening to resume.");
            }
            result = voiceView(voice);
        } else if (method == "getChanges") {
            result =
                impl_->changes(Object(impl_->db.get(), arguments,
                                      {"storeId", "afterSequence", "limit"}));
        } else if (method == "getCatalog") {
            result = impl_->catalog(
                Object(impl_->db.get(), arguments, {"material"}));
        } else if (method == "listSessions") {
            Object args(impl_->db.get(), arguments, {"limit"});
            result = "[";
            for (const auto &saved : impl_->library.sessions(
                     static_cast<std::size_t>(args.number("limit", 100))))
                append(result, sessionView(saved));
            result += ']';
        } else if (method == "prepareReplay") {
            Object args(
                impl_->db.get(), arguments,
                {"sessionId", "basis", "speedNumerator", "speedDenominator"});
            const auto basis = args.text("basis");
            if (basis != "observed" && basis != "arrival")
                throw std::invalid_argument("invalid time basis");
            const auto speed =
                Speed::parse(args.text("speedNumerator", 7) + "/" +
                             args.text("speedDenominator", 7));
            impl_->load(args.text("sessionId", 128),
                        basis == "observed" ? TimeBasis::observed
                                            : TimeBasis::arrival,
                        speed);
            const auto playback = impl_->playback ? impl_->playback->snapshot()
                                                  : PlaybackSnapshot{};
            const auto id = impl_->playback
                                ? preparationId(playback)
                                : "native-" + std::to_string(impl_->generation);
            const auto generation =
                impl_->playback ? playback.generation : impl_->generation;
            const auto duration =
                impl_->playback
                    ? frameMicroseconds(playback.totalFrames,
                                        playback.sampleRate)
                    : static_cast<std::uint64_t>(impl_->prepared->durationUs());
            result =
                "{\"preparationId\":" + quoteJson(id) +
                ",\"generation\":" + quoteJson(std::to_string(generation)) +
                ",\"durationUs\":" + quoteJson(std::to_string(duration)) +
                ",\"eventCount\":" + std::to_string(impl_->saved.eventCount) +
                ",\"peakEventsPerSecond\":" +
                std::to_string(impl_->prepared->peakEventsPerSecond()) +
                ",\"session\":" + sessionView(impl_->saved) + "}";
        } else if (method == "getMaterialEvents") {
            Object args(impl_->db.get(), arguments,
                        {"material", "offset", "limit"});
            Object material(impl_->db.get(), args.object("material"),
                            {"kind", "sessionId"});
            if (material.text("kind") != "session")
                throw std::invalid_argument("only saved material is supported");
            const auto sid = material.text("sessionId", 128);
            const auto offset = static_cast<std::size_t>(
                           args.number("offset", 50000)),
                       limit =
                           static_cast<std::size_t>(args.number("limit", 500));
            if (!limit)
                throw std::invalid_argument("positive page limit required");
            impl_->loadMaterial(sid);
            const auto &events = impl_->material->events();
            result = "[";
            for (auto i = offset; i < std::min(events.size(), offset + limit);
                 ++i)
                append(result,
                       eventView(impl_->db.get(), events[i].normalizedJson,
                                 events[i].revisionSequence));
            result = "{\"total\":" + std::to_string(events.size()) +
                     ",\"events\":" + result + "]}";
        } else
            return failure(
                "unsupported",
                "This capability is not implemented by the native data host");
        return "{\"requestId\":" + quoteJson(id) +
               ",\"ok\":true,\"result\":" + result + "}";
    } catch (const VoiceStoreError &e) {
        const auto code = e.code() == VoiceStoreErrorCode::conflict
                              ? "stale_generation"
                          : e.code() == VoiceStoreErrorCode::invalidArgument ||
                                  e.code() == VoiceStoreErrorCode::notFound ||
                                  e.code() == VoiceStoreErrorCode::gestureActive
                              ? "invalid_request"
                              : "unavailable";
        return failure(code, e.what());
    } catch (const std::invalid_argument &e) {
        return failure("invalid_request", e.what());
    } catch (const std::exception &) {
        return failure("unavailable", "Library data could not be verified or "
                                      "read; no material was returned");
    }
}
} // namespace atlas
