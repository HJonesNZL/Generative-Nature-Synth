#include "event_validation.hpp"
#include "json_text.hpp"
#include "live_performance.hpp"
#include "live_species_counts.hpp"
#include "piece_jobs.hpp"
#include "sqlite_statement.hpp"
#include <algorithm>
#include <atlas/exports.hpp>
#include <atlas/live_admission.hpp>
#include <atlas/piece_import.hpp>
#include <atlas/playback.hpp>
#include <atlas/presets.hpp>
#include <atlas/render_jobs.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/storage.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <charconv>
#include <chrono>
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
    std::string array(std::string_view key) const {
        const auto &field = at(key);
        if (field.type != "array")
            throw std::invalid_argument("array argument required");
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
    bool has(std::string_view key) const {
        return fields_.contains(std::string(key));
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
std::string decimal(double value);
void append(std::string &array, std::string_view value);
std::string compositionView(const CompositionSettings &settings) {
    constexpr std::string_view scales[]{"majorPentatonic", "minorPentatonic",
                                        "major", "naturalMinor"};
    return "{\"enabled\":" + std::string(settings.enabled ? "true" : "false") +
           ",\"tempoBpm\":" + std::to_string(settings.tempoBpm) +
           ",\"tonicPitchClass\":" + std::to_string(settings.tonicPitchClass) +
           ",\"scale\":" +
           quoteJson(scales[static_cast<unsigned>(settings.scale)]) +
           ",\"density\":" +
           std::to_string(static_cast<unsigned>(settings.density)) +
           ",\"variation\":" + decimal(settings.variation) +
           ",\"seed\":" + std::to_string(settings.seed) +
           ",\"quantize\":" + (settings.quantize ? "true" : "false") + "}";
}
CompositionSettings readComposition(sqlite3 *db, std::string_view json) {
    const Object args(db, json,
                      {"enabled", "tempoBpm", "tonicPitchClass", "scale",
                       "density", "variation", "seed", "quantize"});
    CompositionSettings settings;
    settings.enabled = args.boolean("enabled");
    settings.tempoBpm = static_cast<unsigned>(args.number("tempoBpm", 180));
    if (settings.tempoBpm < 40)
        throw std::invalid_argument("Tempo must be 40 to 180 BPM");
    settings.tonicPitchClass =
        static_cast<unsigned>(args.number("tonicPitchClass", 11));
    const auto scale = args.text("scale", 32);
    if (scale == "majorPentatonic")
        settings.scale = CompositionScale::majorPentatonic;
    else if (scale == "minorPentatonic")
        settings.scale = CompositionScale::minorPentatonic;
    else if (scale == "major")
        settings.scale = CompositionScale::major;
    else if (scale == "naturalMinor")
        settings.scale = CompositionScale::naturalMinor;
    else
        throw std::invalid_argument("Unknown composition scale");
    settings.density =
        static_cast<CompositionDensity>(args.number("density", 2));
    settings.variation = args.real("variation");
    if (settings.variation < 0 || settings.variation > 1)
        throw std::invalid_argument("Variation must be 0 to 1");
    settings.seed = static_cast<std::uint32_t>(args.number("seed", UINT32_MAX));
    settings.quantize = args.boolean("quantize");
    return settings;
}
std::string spatialView(const SpatialSettings &settings) {
    std::string regions = "[";
    for (const auto &region : settings.regions) {
        append(regions,
               "{\"id\":" + quoteJson(region.id) +
                   ",\"name\":" + quoteJson(region.name) +
                   ",\"south\":" + decimal(region.south) + ",\"north\":" +
                   decimal(region.north) + ",\"west\":" + decimal(region.west) +
                   ",\"east\":" + decimal(region.east) +
                   ",\"pan\":" + decimal(region.pan) +
                   ",\"gainDb\":" + decimal(region.gainDb) +
                   ",\"send\":" + decimal(region.send) +
                   ",\"echoMix\":" + decimal(region.echoMix) +
                   ",\"delayMs\":" + decimal(region.delayMs) +
                   ",\"feedback\":" + decimal(region.feedback) +
                   ",\"roomSeconds\":" + decimal(region.roomSeconds) +
                   ",\"tone\":" + decimal(region.tone) + ",\"positionPan\":" +
                   (region.positionPan ? "true" : "false") + "}");
    }
    return "{\"enabled\":" + std::string(settings.enabled ? "true" : "false") +
           ",\"amount\":" + decimal(settings.amount) +
           ",\"regions\":" + regions + "]}";
}
SpatialSettings readSpatial(sqlite3 *db, std::string_view json) {
    const Object args(db, json, {"enabled", "amount", "regions"});
    SpatialSettings settings;
    settings.enabled = args.boolean("enabled");
    settings.amount = args.real("amount");
    Statement entries(db, "SELECT type,value FROM json_each(?1)");
    entries.bindJson(args.array("regions"));
    while (entries.next()) {
        if (entries.text(0) != "object" || settings.regions.size() >= 8)
            throw std::invalid_argument(
                "At most eight region objects required");
        const Object fields(db, entries.text(1),
                            {"id", "name", "south", "north", "west", "east",
                             "pan", "gainDb", "send", "echoMix", "delayMs",
                             "feedback", "roomSeconds", "tone", "positionPan"});
        RegionSettings region;
        region.id = fields.text("id", 64);
        region.name = fields.text("name", 128);
        region.south = fields.real("south");
        region.north = fields.real("north");
        region.west = fields.real("west");
        region.east = fields.real("east");
        region.pan = fields.real("pan");
        region.gainDb = fields.real("gainDb");
        region.send = fields.real("send");
        region.echoMix = fields.real("echoMix");
        region.delayMs = fields.real("delayMs");
        region.feedback = fields.real("feedback");
        region.roomSeconds = fields.real("roomSeconds");
        region.tone = fields.real("tone");
        region.positionPan = fields.boolean("positionPan");
        settings.regions.push_back(std::move(region));
    }
    if (!validSpatialSettings(settings))
        throw std::invalid_argument(
            "Invalid spatial settings or region bounds");
    return settings;
}
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
    const auto status = sqlite3_open_v2(
        filename.c_str(), &raw,
        (filename == ":memory:" ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                : SQLITE_OPEN_READONLY) |
            SQLITE_OPEN_FULLMUTEX,
        nullptr);
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
    // Live listening has no bounded saved-piece timeline. Its callback clock
    // keeps advancing beyond the empty preparation used to own the voices.
    const auto positionFrames = snapshot.live ? 0 : snapshot.positionFrames;
    const auto totalFrames = snapshot.live ? 0 : snapshot.totalFrames;
    return "{\"preparationId\":" +
           (snapshot.prepared ? quoteJson(preparationId(snapshot)) : "null") +
           ",\"generation\":" + quoteJson(std::to_string(snapshot.generation)) +
           ",\"state\":" + quoteJson(state) + ",\"positionUs\":" +
           quoteJson(std::to_string(
               frameMicroseconds(positionFrames, snapshot.sampleRate))) +
           ",\"durationUs\":" +
           quoteJson(std::to_string(
               frameMicroseconds(totalFrames, snapshot.sampleRate))) +
           "}";
}
const char *playbackMessage(PlaybackError error) noexcept {
    switch (error) {
    case PlaybackError::none:
        return "";
    case PlaybackError::liveNoteRejected:
        return "Live note capacity exceeded.";
    case PlaybackError::staleControlRevision:
        return "Audio devices changed. Refresh the output choices and try "
               "again.";
    case PlaybackError::invalidDeviceConfiguration:
        return "Choose a listed output and supported buffer size.";
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
        return "The selected audio output could not be opened. Check the "
               "device "
               "and enable Listening again.";
    case PlaybackError::unsupportedDevice:
        return "The selected output does not support the prepared stereo "
               "sample "
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
std::string deviceCatalogView(const AudioDeviceCatalog &catalog,
                              const PlaybackSnapshot &snapshot) {
    std::string devices = "[";
    for (const auto &device : catalog.devices) {
        if (devices.size() > 1)
            devices += ',';
        std::string rates = "[", buffers = "[";
        for (auto rate : device.sampleRates) {
            if (rates.size() > 1)
                rates += ',';
            rates += std::to_string(rate);
        }
        for (auto frames : device.bufferFrames) {
            if (buffers.size() > 1)
                buffers += ',';
            buffers += std::to_string(frames);
        }
        devices +=
            "{\"id\":" + quoteJson(device.id) +
            ",\"name\":" + quoteJson(device.name) +
            ",\"type\":" + quoteJson(device.type) +
            ",\"isDefault\":" + (device.isDefault ? "true" : "false") +
            ",\"outputChannels\":" + std::to_string(device.outputChannels) +
            ",\"sampleRates\":" + rates + "],\"bufferFrames\":" + buffers +
            "],\"defaultBufferFrames\":" +
            std::to_string(device.defaultBufferFrames) + "}";
    }
    const auto &configuration = catalog.configuration;
    std::string active = "null";
    if (catalog.active && snapshot.enabled) {
        const auto &device = *catalog.active;
        active =
            "{\"deviceId\":" + quoteJson(device.deviceId) +
            ",\"name\":" + quoteJson(device.name) +
            ",\"type\":" + quoteJson(device.type) +
            ",\"sampleRate\":" + std::to_string(snapshot.deviceSampleRate) +
            ",\"bufferFrames\":" + std::to_string(snapshot.deviceBufferFrames) +
            ",\"outputChannels\":" +
            std::to_string(snapshot.deviceOutputChannels) + "}";
    }
    return "{\"available\":" +
           std::string(catalog.available ? "true" : "false") +
           ",\"devices\":" + devices + "],\"configuration\":{\"mode\":" +
           quoteJson(configuration.mode == AudioDeviceMode::systemDefault
                         ? "systemDefault"
                         : "explicitDevice") +
           ",\"deviceId\":" +
           (configuration.deviceId.empty()
                ? "null"
                : quoteJson(configuration.deviceId)) +
           ",\"bufferFrames\":" + std::to_string(configuration.bufferFrames) +
           "},\"active\":" + active + ",\"controlRevision\":" +
           quoteJson(std::to_string(catalog.controlRevision)) + ",\"error\":" +
           (catalog.error.empty() ? "null" : quoteJson(catalog.error)) +
           ",\"preparationId\":" +
           (snapshot.prepared ? quoteJson(preparationId(snapshot)) : "null") +
           ",\"generation\":" + quoteJson(std::to_string(snapshot.generation)) +
           "}";
}
std::string audioView(const PlaybackSnapshot &snapshot,
                      const AudioDeviceCatalog &catalog) {
    const auto status = snapshot.error != PlaybackError::none ? "device_lost"
                        : snapshot.enabled                    ? "running"
                                                              : "disabled";
    const double peak =
        std::max(snapshot.outputPeakLeft, snapshot.outputPeakRight);
    const double peakDb =
        peak > 0 ? std::max(-96.0, 20 * std::log10(peak)) : -96.0;
    return "{\"status\":" + quoteJson(status) + ",\"deviceName\":" +
           (catalog.active ? quoteJson(catalog.active->name) : "null") +
           ",\"masterGainDb\":" + std::to_string(snapshot.gainDb) +
           ",\"peakDbfs\":" + (snapshot.enabled ? decimal(peakDb) : "null") +
           ",\"meterIsSimulated\":false,\"clipping\":" +
           (peak >= 1 ? "true," : "false,") +
           "\"activeSynthVoices\":null,\"maxSynthVoices\":8," +
           "\"sampleRate\":" +
           (snapshot.deviceSampleRate
                ? std::to_string(snapshot.deviceSampleRate)
                : "null") +
           ",\"bufferFrames\":" +
           (snapshot.deviceBufferFrames
                ? std::to_string(snapshot.deviceBufferFrames)
                : "null") +
           ",\"outputChannels\":" +
           (snapshot.deviceOutputChannels
                ? std::to_string(snapshot.deviceOutputChannels)
                : "null") +
           ",\"liveTriggeredNotes\":" +
           quoteJson(std::to_string(snapshot.liveTriggeredNotes)) +
           ",\"callbackCount\":" +
           quoteJson(std::to_string(snapshot.callbackCount)) +
           ",\"callbackFrames\":" +
           quoteJson(std::to_string(snapshot.callbackFrames)) +
           ",\"faultCount\":" + quoteJson(std::to_string(snapshot.faultCount)) +
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
           ",\"inheritsFromId\":null,\"type\":" +
           quoteJson(voiceCharacterIds[static_cast<std::size_t>(
               voice.settings.character)]) +
           ",\"values\":" + values +
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
std::string jobView(const std::optional<PieceJob> &job) {
    if (!job)
        return "null";
    return "{\"id\":" + quoteJson(job->id) +
           ",\"operation\":" + quoteJson(job->operation) +
           ",\"status\":" + quoteJson(job->status) +
           ",\"stage\":" + quoteJson(job->stage) +
           ",\"processed\":" + std::to_string(job->processed) +
           ",\"total\":" + (job->total ? std::to_string(*job->total) : "null") +
           ",\"piece\":" + (job->piece ? sessionView(*job->piece) : "null") +
           ",\"error\":" +
           (job->error.empty() ? "null" : quoteJson(job->error)) + "}";
}
} // namespace

struct UiHost::Impl {
    std::unique_ptr<ReadOnlyLibrary> library;
    Database db;
    std::filesystem::path attachedPath;
    std::unique_ptr<PieceJobs> pieces;
    std::optional<Timeline> prepared;
    SavedSession saved;
    std::optional<Timeline> material;
    SavedSession materialSaved;
    std::optional<LibraryRevision> materialRevision;
    std::uint64_t generation{};
    PlaybackRuntime *playback{};
    std::unique_ptr<LiveAdmission> liveAdmission;
    std::optional<LiveSessionBoundary> liveSession;
    LiveSpeciesCounts liveSpeciesRanking;
    LiveSettings liveSettings;
    LivePerformance performance;
    bool liveEnabled{}, liveCatchingUp{};
    std::uint64_t liveReceived{}, liveInvalid{}, liveSkipped{};
    std::string liveError, liveStoreId;
    std::chrono::steady_clock::time_point liveLastService{};
    std::string liveState() const {
        return "{\"enabled\":" + std::string(liveEnabled ? "true" : "false") +
               ",\"status\":" +
               quoteJson(!liveError.empty() ? "error"
                         : liveEnabled      ? "listening"
                                            : "off") +
               ",\"received\":" + std::to_string(liveReceived) +
               ",\"admitted\":" + std::to_string(performance.sounded) +
               ",\"dropped\":" +
               std::to_string(performance.skipped + liveInvalid) +
               ",\"grouped\":" + std::to_string(performance.grouped) +
               ",\"pending\":" + std::to_string(performance.pendingCount()) +
               ",\"catchingUp\":" + (liveCatchingUp ? "true" : "false") +
               ",\"mode\":" +
               quoteJson(liveSettings.mode == LiveMode::musical    ? "musical"
                         : liveSettings.mode == LiveMode::smoothed ? "smoothed"
                                                                   : "direct") +
               ",\"density\":" + std::to_string(liveSettings.density) +
               ",\"composition\":" + compositionView(liveSettings.composition) +
               ",\"spatial\":" + spatialView(liveSettings.spatial) +
               ",\"skippedRevisions\":" + std::to_string(liveSkipped) +
               ",\"error\":" + quoteJson(liveError) + "}";
    }
    void retireLive() {
        if (playback)
            performance.acknowledge(playback->snapshot());
        performance.cancel();
        liveCatchingUp = false;
        liveEnabled = false;
        liveAdmission.reset();
    }
    void stopLive() {
        if (liveEnabled && playback)
            playback->enable(false);
        retireLive();
    }
    bool liveAvailable() const {
        std::error_code error;
        return playback && library && !storage.managedLibrary.empty() &&
               std::filesystem::equivalent(attachedPath, storage.managedLibrary,
                                           error) &&
               !error;
    }
    void startLive() {
        if (liveEnabled)
            return;
        if (!liveAvailable())
            throw std::invalid_argument(
                "Live listening requires the managed live library and output");
        if (storage.requireCollectionSession && !liveSession)
            throw std::invalid_argument("Start Collect before enabling Listening");
        auto admission = std::make_unique<LiveAdmission>(db.get());
        if (liveSession) {
            admission->baseline();
            if (admission->storeId() != liveSession->storeId)
                throw std::invalid_argument("Live library changed. Restart Collect to listen");
            auto boundary = *liveSession;
            boundary.baselineRevision = std::max(boundary.baselineRevision,
                                                  admission->revision());
            admission->setSession(boundary);
        } else
            admission->baseline();
        liveStoreId = admission->storeId();
        auto settings = context.audio;
        if (!prepared)
            settings.masterDb = -18;
        prepared.reset();
        saved = {};
        voiceSlots.clear();
        context = {};
        context.audio = settings;
        prepareLiveOutput();
        liveAdmission = std::move(admission);
        liveEnabled = true;
        liveError.clear();
        performance = {};
        liveReceived = liveInvalid = liveSkipped = 0;
        liveLastService = std::chrono::steady_clock::now();
    }
    void prepareLiveOutput() {
        auto settings = context.audio;
        settings.composition = liveSettings.composition;
        settings.composition.enabled = liveSettings.mode == LiveMode::musical;
        settings.spatial = liveSettings.spatial;
        settings.spatial.enabled =
            liveSettings.mode != LiveMode::direct && settings.spatial.enabled;
        auto result = playback->prepareLive(settings);
        if (result.ok)
            result = playback->enable(true);
        if (result.ok)
            result = playback->command({PlaybackCommandKind::play,
                                        result.snapshot.preparationId,
                                        result.snapshot.generation});
        if (!result.ok) {
            playback->enable(false);
            throw std::runtime_error(playbackErrorText(result.error));
        }
    }
    void updateLiveSettings(const Object &args) {
        if (!liveAvailable() || !args.size())
            throw std::invalid_argument(
                "Live settings require the managed library and an edit");
        auto next = liveSettings;
        if (args.has("mode")) {
            const auto mode = args.text("mode", 16);
            if (mode == "direct")
                next.mode = LiveMode::direct;
            else if (mode == "musical")
                next.mode = LiveMode::musical;
            else if (mode == "smoothed")
                next.mode = LiveMode::smoothed;
            else
                throw std::invalid_argument("Unknown live mode");
        }
        if (args.has("density")) {
            next.density = static_cast<unsigned>(args.number("density", 8));
            if (!next.density)
                throw std::invalid_argument("Choose one to eight live notes");
        }
        if (args.has("composition")) {
            next.composition =
                readComposition(db.get(), args.object("composition"));
            next.composition.enabled = true;
        }
        if (args.has("spatial"))
            next.spatial = readSpatial(db.get(), args.object("spatial"));
        const bool retune =
            next.composition.tonicPitchClass !=
                liveSettings.composition.tonicPitchClass ||
            next.composition.scale != liveSettings.composition.scale;
        const bool reroute =
            args.has("spatial") &&
            spatialView(next.spatial) != spatialView(liveSettings.spatial);
        const bool modeChanged = next.mode != liveSettings.mode;
        const bool rebuild = modeChanged ||
                             (next.mode != LiveMode::direct && reroute) ||
                             (next.mode == LiveMode::musical && retune);
        liveSettings = std::move(next);
        if (liveEnabled && rebuild) {
            try {
                playback->enable(false);
                performance.acknowledge(playback->snapshot());
                if (!modeChanged && liveSettings.mode == LiveMode::smoothed)
                    performance.retryHandedOff();
                else
                    performance.cancel();
                liveCatchingUp = false;
                prepareLiveOutput();
            } catch (const std::exception &e) {
                stopLive();
                liveError = e.what();
            }
        }
    }
    std::string liveOutcome(const std::string &id) const {
        const auto *o = performance.outcome(id);
        if (!o)
            return "{\"outcome\":null}";
        // A cached result must not disclose a withdrawn record or a replaced
        // library.
        Statement visible(db.get(), "SELECT (SELECT value FROM metadata WHERE "
                                    "key='store_id') FROM events WHERE id=?1");
        visible.bindJson(id);
        if (!visible.next() || visible.text(0) != liveStoreId)
            return "{\"outcome\":null}";
        return "{\"outcome\":{\"eventId\":" + quoteJson(o->eventId) +
               ",\"speciesId\":" + quoteJson(o->speciesId) +
               ",\"region\":" + quoteJson(o->region) +
               ",\"status\":" + quoteJson(o->status) +
               ",\"voiceCharacter\":" + (o->voiceCharacter.empty() ? "null" : quoteJson(o->voiceCharacter)) +
               ",\"representativeId\":" +
               (o->representativeId.empty() ? "null"
                                            : quoteJson(o->representativeId)) +
               "}}";
    }
    void serviceLive() {
        if (!liveEnabled)
            return;
        try {
            const auto now = std::chrono::steady_clock::now();
            const auto snapshot = playback->snapshot();
            if (!snapshot.live || !snapshot.enabled ||
                snapshot.error != PlaybackError::none) {
                liveError = snapshot.error == PlaybackError::none
                                ? "Live output stopped"
                                : playbackMessage(snapshot.error);
                stopLive();
                return;
            }
            playback->heartbeat();
            performance.acknowledge(snapshot);
            const bool smooth = liveSettings.mode == LiveMode::smoothed;
            if (!smooth && now - liveLastService > std::chrono::seconds(5))
                performance.cancel();
            if (!smooth && now - liveLastService > std::chrono::seconds(5) &&
                liveAdmission->baseline()) {
                performance = {};
                liveError =
                    "Live library changed. Restart Collect to resume Listening.";
                stopLive();
                return;
            }
            liveLastService = now;
            const auto batch =
                smooth ? liveAdmission->pollFifo(performance.smoothCapacity())
                       : liveAdmission->poll(512);
            liveCatchingUp = batch.hasMore;
            if (batch.storeReset) {
                performance = {};
                liveError =
                    "Live library changed. Restart Collect to resume Listening.";
                stopLive();
                return;
            }
            liveReceived += batch.received;
            liveInvalid += batch.dropped;
            liveSkipped += batch.skippedRevisions;
            std::vector<LiveObservation> observations;
            observations.reserve(batch.payloads.size());
            for (const auto &payload : batch.payloads) {
                Object event(db.get(), payload);
                LiveObservation observation{event.text("event_id", 2048),
                                            event.optionalText("species_id"),
                                            event.optionalText("common_name"),
                                            {}};
                if (!event.null("latitude") && !event.null("longitude"))
                    observation.position = Position{event.real("latitude"),
                                                    event.real("longitude")};
                observations.push_back(std::move(observation));
            }
            const auto clock = std::chrono::steady_clock::now();
            performance.accept(std::move(observations), liveSettings, clock,
                               batch.smoothTiming);
            performance.playDue(clock, db.get(), *playback, voices,
                                liveSettings, context.audio.rootMidi,
                                liveSession ? &*liveSession : nullptr);
        } catch (const std::exception &) {
            liveError =
                "Live listening failed while reading committed detections";
            stopLive();
        }
    }
    VoiceStore *voices{};
    std::map<std::string, VoiceSlot> voiceSlots;
    UiHostStorage storage;
    std::unique_ptr<RenderJobs> renders;
    std::map<std::string, std::string> renderScenes;
    SoundscapeCapture context;
    Impl(const std::filesystem::path &path, PlaybackRuntime *output,
         VoiceStore *presets, UiHostStorage config)
        : db(open(!std::filesystem::exists(path) && !config.soundscapes.empty()
                      ? std::filesystem::path(":memory:")
                      : path)),
          attachedPath(path), playback(output), voices(presets),
          storage(std::move(config)) {
        if (!storage.managedLibrary.empty())
            pieces = std::make_unique<PieceJobs>();
        if (std::filesystem::exists(path))
            library = std::make_unique<ReadOnlyLibrary>(path);
        if (!storage.soundscapes.empty()) {
            std::filesystem::create_directories(storage.soundscapes);
            if (!std::filesystem::is_directory(
                    std::filesystem::symlink_status(storage.soundscapes)))
                throw std::invalid_argument(
                    "Soundscape directory must not be a symlink");
            if (!storage.exports.empty()) {
                std::filesystem::create_directories(storage.exports);
                renders = std::make_unique<RenderJobs>(
                    storage.exports, storage.engineSourceSha256);
            }
        }
    }
    static bool opaqueId(std::string_view id) {
        return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }
    std::filesystem::path scenePath(std::string_view id) const {
        if (storage.soundscapes.empty() || !opaqueId(id))
            throw std::invalid_argument("Invalid soundscape ID");
        return storage.soundscapes / (std::string(id) + ".atlas-scene");
    }
    std::string newId() {
        Statement query(db.get(), "SELECT lower(hex(randomblob(16)))");
        if (!query.next())
            throw std::runtime_error("ID generation failed");
        return query.text(0);
    }
    std::vector<VoiceProfile> currentProfiles(const Timeline &timeline) {
        std::vector<std::string> payloads;
        for (const auto &event : timeline.events())
            payloads.push_back(event.normalizedJson);
        registerPayloads(payloads);
        std::vector<VoiceProfile> profiles{
            {"",
             voices ? voices->getVoice("default").settings : VoiceSettings{}}};
        std::set<std::string> species;
        for (const auto &event : timeline.events())
            if (event.speciesId && !event.speciesId->empty())
                species.insert(*event.speciesId);
        for (const auto &id : species)
            profiles.push_back(
                {id, voices ? voices->getVoice("species:" + id).settings
                            : VoiceSettings{}});
        return profiles;
    }
    void prepare(Timeline next, SavedSession metadata,
                 SoundscapeCapture nextContext,
                 std::optional<VoiceStore::DraftUpdate> update = {}) {
        stopLive();
        std::map<std::string, VoiceSlot> slots;
        for (std::size_t i = 0; i < nextContext.profiles.size(); ++i)
            slots.emplace(i == 0
                              ? "default"
                              : "species:" + nextContext.profiles[i].speciesId,
                          static_cast<VoiceSlot>(i));
        if (playback) {
            const auto result =
                playback->prepare(next, nextContext.audio, nextContext.profiles,
                                  voices != nullptr);
            if (!result.ok)
                throw std::runtime_error(playbackErrorText(result.error));
        }
        if (update && !voices->commitProfiles(std::move(*update))) {
            if (playback)
                playback->enable(false);
            throw std::runtime_error("Voice draft changed during recall");
        }
        prepared = std::move(next);
        saved = std::move(metadata);
        context = std::move(nextContext);
        voiceSlots = std::move(slots);
        ++generation;
    }
    std::string preparation() const {
        const auto snap = playback ? playback->snapshot() : PlaybackSnapshot{};
        return "{\"preparationId\":" +
               quoteJson(playback ? preparationId(snap)
                                  : "native-" + std::to_string(generation)) +
               ",\"generation\":" +
               quoteJson(
                   std::to_string(playback ? snap.generation : generation)) +
               ",\"durationUs\":" +
               quoteJson(std::to_string(
                   playback
                       ? frameMicroseconds(snap.totalFrames, snap.sampleRate)
                       : static_cast<std::uint64_t>(prepared->durationUs()))) +
               ",\"eventCount\":" + std::to_string(saved.eventCount) +
               ",\"peakEventsPerSecond\":" +
               std::to_string(prepared->peakEventsPerSecond()) +
               ",\"session\":" + sessionView(saved) + "}";
    }
    std::string compositionState() const {
        std::string result =
            "{\"settings\":" + compositionView(context.audio.composition) +
            ",\"preparation\":" + (prepared ? preparation() : "null");
        if (prepared && playback) {
            const auto snapshot = playback->snapshot();
            result += ",\"generatedNotes\":" +
                      std::to_string(snapshot.generatedNotes) +
                      ",\"suppressed\":" +
                      std::to_string(snapshot.omittedCompositionNotes) +
                      ",\"suppressedSourceEvents\":" +
                      std::to_string(snapshot.suppressedSourceEvents);
        }
        return result + "}";
    }
    std::string spatialState() const {
        std::string result =
            "{\"settings\":" + spatialView(context.audio.spatial) +
            ",\"preparation\":" + (prepared ? preparation() : "null");
        if (prepared && playback) {
            const auto snapshot = playback->snapshot();
            result +=
                ",\"routedNotes\":" + std::to_string(snapshot.routedNotes) +
                ",\"unassignedNotes\":" +
                std::to_string(snapshot.unassignedNotes) +
                ",\"wetActive\":" + (snapshot.wetActive ? "true" : "false");
        }
        return result + "}";
    }
    static SavedSession sceneSession(const Soundscape &scene,
                                     std::string_view id) {
        auto session = scene.capture().session;
        session.id = "scene:" + std::string(id);
        session.name = scene.capture().name;
        return session;
    }
    std::string sceneView(const SoundscapeSummary &s) const {
        const auto id = s.path.stem().string();
        return "{\"id\":" + quoteJson(id) + ",\"name\":" + quoteJson(s.name) +
               ",\"valid\":" + (s.valid ? "true" : "false") +
               ",\"contentVerified\":" +
               (s.contentVerified ? "true" : "false") +
               ",\"error\":" + (s.error.empty() ? "null" : quoteJson(s.error)) +
               ",\"contentHash\":" +
               (s.valid ? quoteJson(s.contentHash) : "null") +
               ",\"sourceSessionId\":" +
               (s.valid ? quoteJson(s.sourceSessionId) : "null") +
               ",\"eventCount\":" + std::to_string(s.eventCount) +
               ",\"durationUs\":" +
               (s.valid ? quoteJson(std::to_string(s.durationUs)) : "null") +
               ",\"basis\":" +
               (s.valid ? quoteJson(s.basis == TimeBasis::observed ? "observed"
                                                                   : "arrival")
                        : "null") +
               ",\"speedNumerator\":" +
               (s.valid ? quoteJson(std::to_string(s.speed.numerator))
                        : "null") +
               ",\"speedDenominator\":" +
               (s.valid ? quoteJson(std::to_string(s.speed.denominator))
                        : "null") +
               "}";
    }
    std::string sceneView(const Soundscape &scene, std::string_view id) const {
        const auto &c = scene.capture();
        SoundscapeSummary s;
        s.path = scenePath(id);
        s.name = c.name;
        s.sourceSessionId = c.session.id;
        s.contentHash = scene.contentHash();
        s.eventCount = scene.timeline().events().size();
        s.durationUs = scene.timeline().durationUs();
        s.basis = c.basis;
        s.speed = c.speed;
        s.valid = true;
        s.contentVerified = true;
        return sceneView(s);
    }
    std::string renderView(const RenderJobSnapshot &job) const {
        const char *state =
            job.state == RenderJobState::running     ? "rendering"
            : job.state == RenderJobState::completed ? "complete"
            : job.state == RenderJobState::cancelled ? "cancelled"
                                                     : "failed";
        const auto found = renderScenes.find(job.id);
        return "{\"jobId\":" + quoteJson(job.id) +
               ",\"state\":" + quoteJson(state) + ",\"progress\":" +
               decimal(job.totalFrames
                           ? static_cast<double>(job.framesWritten) /
                                 static_cast<double>(job.totalFrames)
                           : 0) +
               ",\"artifactId\":" +
               (job.state == RenderJobState::completed ? quoteJson(job.id)
                                                       : "null") +
               ",\"filename\":" +
               (job.outputFilename.empty() ? "null"
                                           : quoteJson(job.outputFilename)) +
               ",\"soundscapeId\":" +
               quoteJson(found == renderScenes.end() ? "" : found->second) +
               ",\"message\":" +
               (job.error.empty() ? "null" : quoteJson(job.error)) + "}";
    }
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
        if (liveEnabled || !playback || slot == voiceSlots.end())
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
        if (prepared) {
            std::vector<std::string> payloads;
            for (const auto &e : prepared->events())
                payloads.push_back(e.normalizedJson);
            registerPayloads(payloads);
        } else if (library)
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
        std::string types = "[";
        for (const auto character : voiceCharacterIds)
            append(types, "{\"id\":" + quoteJson(character) + ",\"label\":" +
                              quoteJson(character) + ",\"proposal\":false}");
        return "{\"entries\":[{\"groupId\":\"native\",\"groupName\":\"Species "
               "voices\",\"groupVoiceId\":\"default\",\"species\":" +
               species + "]}],\"parameters\":" + parameterSpecs() +
               ",\"types\":" + types +
               "],\"maxDefinitions\":513,"
               "\"editingBehavior\":\"new_notes\"}";
    }
    std::string parameterSpecs();
    void load(std::string_view id, TimeBasis basis = TimeBasis::observed,
              Speed speed = {}) {
        if (playback)
            playback->enable(false);
        SavedSession metadata;
        SoundscapeCapture nextContext;
        std::optional<Timeline> next;
        if (id.starts_with("scene:")) {
            const auto scene = loadSoundscape(scenePath(id.substr(6)));
            metadata = sceneSession(scene, id.substr(6));
            nextContext = scene.capture();
            nextContext.basis = basis;
            nextContext.speed = speed;
            nextContext.bounds = basis == TimeBasis::observed
                                     ? nextContext.session.observedBounds
                                     : nextContext.session.arrivalBounds;
            next.emplace(scene.timeline().events(), basis, speed,
                         nextContext.bounds);
        } else {
            if (!library)
                throw std::runtime_error("Wildlife library is unavailable");
            loadMaterial(id);
            metadata = materialSaved;
            next.emplace(material->events(), basis, speed,
                         basis == TimeBasis::observed ? metadata.observedBounds
                                                      : metadata.arrivalBounds);
            nextContext.session = metadata;
            nextContext.basis = basis;
            nextContext.speed = speed;
            nextContext.bounds = basis == TimeBasis::observed
                                     ? metadata.observedBounds
                                     : metadata.arrivalBounds;
            nextContext.audio.masterDb = -18;
        }
        if (prepared && saved.id == id)
            nextContext.audio = context.audio;
        nextContext.profiles = currentProfiles(*next);
        prepare(std::move(*next), std::move(metadata), std::move(nextContext));
    }
    void loadMaterial(std::string_view id) {
        const bool scene = id.starts_with("scene:");
        const auto revision = !scene && library
                                  ? std::optional{library->revision()}
                                  : std::nullopt;
        if (material && materialSaved.id == id && materialRevision == revision)
            return;
        SavedSession metadata;
        std::optional<Timeline> next;
        if (id.starts_with("scene:")) {
            const auto scene = loadSoundscape(scenePath(id.substr(6)));
            metadata = sceneSession(scene, id.substr(6));
            next.emplace(scene.timeline().events(), TimeBasis::observed,
                         Speed{}, metadata.observedBounds);
        } else {
            if (!library)
                throw std::runtime_error("Wildlife library is unavailable");
            next = library->load(id, TimeBasis::observed, {}, &metadata);
        }
        if (revision && library->revision() != *revision)
            throw StaleLibraryCursor(
                "Library changed while material was read. Refresh the piece.");
        material = std::move(next);
        materialSaved = std::move(metadata);
        materialRevision = revision;
    }
    std::string liveChanges(const Object &args) {
        const auto sessionId = args.text("sessionId", 128);
        const auto after = integer(args.text("afterSequence", 20), INT64_MAX);
        const auto requestedStore = args.text("storeId", 128);
        const auto limit = args.number("limit", 500);
        if (limit < 1)
            throw std::invalid_argument("positive page limit required");
        Statement head(db.get(), "SELECT value,(SELECT COALESCE(MAX(seq),0) FROM "
                                 "revisions) FROM metadata WHERE key='store_id'");
        if (!head.next())
            throw std::runtime_error("Live library identity unavailable");
        const auto store = head.text(0);
        const auto high = head.integer(1);
        const bool active = liveSession && liveSession->storeId == store &&
                            high >= liveSession->baselineRevision;
        const auto frontier = active ? liveSession->baselineRevision : high;
        auto next = frontier;
        std::string events = "[", removed = "[";
        bool hasMore{};
        if (active && sessionId == liveSession->id &&
            (requestedStore.empty() || requestedStore == store)) {
            const auto cursor = after > high ? frontier : std::max(after, frontier);
            next = cursor;
            Statement rows(db.get(), "SELECT r.seq,r.event_json,r.change_kind,"
                                     "r.live_eligible,e.revision_seq,r.observed_at,r.event_id,r.source_id,"
                                     "r.first_seen_at,r.ingested_at "
                                     "FROM revisions r LEFT JOIN events e ON e.id=r.event_id "
                                     "WHERE r.seq>?1 AND r.seq<=?2 ORDER BY r.seq LIMIT ?3");
            rows.bind(1, cursor);
            rows.bind(2, high);
            rows.bind(3, limit);
            std::size_t examined{};
            while (rows.next()) {
                ++examined;
                next = rows.integer(0);
                if (rows.null(4) || rows.integer(4) != next)
                    continue;
                const auto payload = rows.text(1);
                const auto removeCorrection = [&] {
                    if (rows.text(2) == "corrected")
                        append(removed, quoteJson(rows.text(6)));
                };
                try {
                    detail::validateStoredEvent(db.get(), payload);
                    Object event(db.get(), payload);
                    if (event.text("provenance_mode") != "live_source" ||
                        parseUtc(rows.text(5)) < parseUtc(liveSession->observedFromUtc) ||
                        parseUtc(event.text("observed_at_utc")) != parseUtc(rows.text(5)) ||
                        event.text("event_id") != rows.text(6) ||
                        event.text("source_id") != rows.text(7) ||
                        parseUtc(event.text("first_seen_at_utc")) != parseUtc(rows.text(8)) ||
                        parseUtc(event.text("ingested_at_utc")) != parseUtc(rows.text(9))) {
                        removeCorrection();
                        continue;
                    }
                    append(events, "{\"event\":" + eventView(db.get(), payload, next) +
                                   ",\"liveEligible\":" +
                                   ((rows.text(2) == "new" && rows.integer(3) == 1) ||
                                    rows.text(2) == "baseline" ? "true" : "false") +
                                   ",\"change\":" + quoteJson(rows.text(2)) + "}");
                } catch (const std::invalid_argument &) {
                    // A correction can remove previously displayed material.
                    removeCorrection();
                }
            }
            if (examined < static_cast<std::size_t>(limit))
                next = high;
            hasMore = next < high;
        }
        return "{\"sessionId\":" + quoteJson(active ? liveSession->id : "") +
               ",\"sessionActive\":" + (active ? "true" : "false") +
               ",\"storeId\":" + quoteJson(store) +
               ",\"nextSequence\":" + quoteJson(std::to_string(next)) +
               ",\"hasMore\":" + (hasMore ? "true" : "false") +
               ",\"removedEventIds\":" + removed + "],\"events\":" + events + "]}";
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
        std::int64_t baselineTotal = 0;
        if (baseline) {
            // The still-active head statement pins the same read snapshot.
            Statement total(db.get(), "SELECT COUNT(*) FROM events");
            if (!total.next())
                throw std::runtime_error("library baseline count unavailable");
            baselineTotal = total.integer(0);
        }
        // Baseline-only coverage sampling: reserve current measured fixes,
        // then representative source/geographic-sector records. The remaining
        // slots keep newest observations plus the newest revision. Incremental
        // cursor delivery is unchanged.
        const auto sql =
            baseline
                ? "WITH current AS (SELECT r.seq,r.observed_at,r.source_id AS source,"
                  "json_extract(r.event_json,'$.location_kind') AS location,"
                  "CAST((json_extract(r.event_json,'$.latitude')+90)/30 AS INTEGER) AS lat_sector,"
                  "CAST((json_extract(r.event_json,'$.longitude')+180)/60 AS INTEGER) AS lon_sector "
                  "FROM events e JOIN revisions r ON r.seq=e.revision_seq WHERE r.seq<=" +
                      std::to_string(high) +
                      "), ranked AS (SELECT *,ROW_NUMBER() OVER (PARTITION BY "
                      "source,location,lat_sector,lon_sector ORDER BY seq DESC) AS coverage_rank "
                      "FROM current), numbered AS (SELECT *,ROW_NUMBER() OVER (PARTITION BY "
                      "source ORDER BY coverage_rank,seq DESC) AS source_rank FROM ranked), "
                      "fixes AS (SELECT seq "
                      "FROM numbered WHERE location='individual_fix' AND lat_sector IS NOT NULL "
                      "AND lon_sector IS NOT NULL ORDER BY source_rank,coverage_rank,seq DESC LIMIT " +
                      std::to_string(std::min<std::int64_t>(100, limit / 2)) +
                      "), coverage AS (SELECT seq "
                      "FROM numbered WHERE coverage_rank=1 AND seq NOT IN (SELECT seq FROM fixes) "
                      "ORDER BY source_rank,seq DESC LIMIT " +
                      std::to_string(std::min<std::int64_t>(50, limit / 4)) +
                      "), recent AS (SELECT seq "
                      "FROM current WHERE seq NOT IN (SELECT seq FROM fixes UNION ALL "
                      "SELECT seq FROM coverage) ORDER BY (seq=" + std::to_string(high) +
                      ") DESC,observed_at DESC,seq DESC LIMIT (" +
                      std::to_string(limit) +
                      "-(SELECT count(*) FROM fixes)-(SELECT count(*) FROM coverage))) "
                      "SELECT r.seq,r.event_json,r.change_kind,r.live_eligible FROM "
                      "(SELECT seq FROM fixes UNION ALL SELECT seq FROM coverage "
                      "UNION ALL SELECT seq FROM recent) selected JOIN revisions r "
                      "ON r.seq=selected.seq ORDER BY r.seq"
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
               (baseline ? ",\"baselineTotal\":" + std::to_string(baselineTotal) : "") +
               ",\"events\":" + events + "]}";
    }
    std::string catalog(const Object &args, bool registerVoices = false) {
        std::vector<std::pair<std::string, std::string>> payloads;
        if (!args.null("material")) {
            Object selection(db.get(), args.object("material"),
                             {"kind", "sessionId"});
            if (selection.text("kind") != "session")
                throw std::invalid_argument("only saved material is supported");
            const auto id = selection.text("sessionId", 128);
            loadMaterial(id);
        } else {
            if (!library)
                throw std::runtime_error("Wildlife library is unavailable");
            // Return one newest current record per distinct identity, with
            // one extra identity to distinguish a full catalogue from overflow.
            // SQL owns the scan; C++ retains at most 1,131 representative rows.
            const auto collect = [&](std::string_view key,
                                     std::string_view condition,
                                     std::size_t capacity) {
                const auto field = "json_extract(r.event_json,'$." +
                                   std::string(key) + "')";
                const auto sql =
                    "SELECT r.event_json,MAX(r.seq) FROM events e JOIN "
                    "revisions r ON r.seq=e.revision_seq WHERE " + field +
                    " IS NOT NULL AND " + field + "<>'' " +
                    std::string(condition) + " GROUP BY " + field +
                    " ORDER BY MAX(r.seq) DESC LIMIT " +
                    std::to_string(capacity + 1);
                Statement rows(db.get(), sql.c_str());
                while (rows.next())
                    payloads.emplace_back(key, rows.text(0));
            };
            collect("species_id", "", 500);
            collect("station_id",
                    "AND json_extract(r.event_json,'$.location_kind') IN "
                    "('station_detection','receiver_detection') AND "
                    "json_extract(r.event_json,'$.latitude') IS NOT NULL "
                    "AND json_extract(r.event_json,'$.longitude') IS NOT NULL",
                    500);
            collect("source_id", "", 128);
        }
        std::map<std::string, std::string> species, stations, sources;
        bool limited = false;
        const auto add = [&](auto &entries, const std::string &id,
                             std::string json, std::size_t capacity) {
            if (entries.contains(id))
                return;
            if (entries.size() == capacity) {
                limited = true;
                return;
            }
            entries.emplace(id, std::move(json));
        };
        const auto inspect = [&](const std::string &payload, std::string_view category) {
            detail::validateStoredEvent(db.get(), payload);
            const Object e(db.get(), payload);
            const auto source = e.text("source_id", 128),
                       speciesId = e.optionalText("species_id"),
                       station = e.optionalText("station_id");
            if ((category.empty() || category == "species_id") &&
                !speciesId.empty()) {
                const auto common = e.optionalText("common_name");
                const auto scientific = e.optionalText("scientific_name");
                const auto name =
                    common.empty() ? (scientific.empty() ? speciesId : scientific)
                                   : common;
                add(species, speciesId,
                    "{\"id\":" + quoteJson(speciesId) + ",\"commonName\":" +
                        quoteJson(name) + ",\"scientificName\":" +
                        quoteJson(scientific) +
                        ",\"group\":\"unclassified\"}", 500);
            }
            const auto locationKind = e.text("location_kind");
            const bool receiver = locationKind == "station_detection" ||
                                  locationKind == "receiver_detection";
            if ((category.empty() || category == "station_id") && receiver &&
                !station.empty() && !e.null("latitude") && !e.null("longitude"))
                add(stations, station,
                    "{\"id\":" + quoteJson(station) +
                        ",\"name\":" + quoteJson(station) +
                        ",\"sourceId\":" + quoteJson(source) +
                        ",\"latitude\":" + e.json("latitude") +
                        ",\"longitude\":" + e.json("longitude") +
                        ",\"positionDescription\":\"Reported station "
                        "position; individual position unknown\"}", 500);
            if (category.empty() || category == "source_id")
                add(sources, source,
                    "{\"id\":" + quoteJson(source) +
                        ",\"name\":" + quoteJson(source) +
                        ",\"measurement\":\"Provider records; inspect "
                        "event kind\",\"attribution\":" +
                        quoteJson(e.optionalText("attribution")) + "}", 128);
        };
        if (!args.null("material")) {
            // The verified material already owns its events. Inspect in place
            // without copying an arbitrarily large session into this catalogue.
            for (auto event = material->events().rbegin();
                 event != material->events().rend(); ++event)
                inspect(event->normalizedJson, "");
        } else {
            for (const auto &[category, payload] : payloads)
                inspect(payload, category);
        }
        if (registerVoices && voices) {
            std::vector<VoiceSpecies> available;
            for (const auto &[id, json] : species)
                available.push_back(
                    {id, Object(db.get(), json).text("commonName", 2048)});
            voices->registerSpecies(available);
        }
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
               VoiceStore *voices, UiHostStorage storage)
    : impl_(std::make_unique<Impl>(library, playback, voices,
                                   std::move(storage))) {}
UiHost::~UiHost() { stopLive(); }
void UiHost::setLibrary(const std::filesystem::path &library) {
    if (!library.empty() && !std::filesystem::is_regular_file(library))
        throw std::invalid_argument("library must be an existing regular file");
    auto next = std::make_unique<Impl>(library, impl_->playback, impl_->voices,
                                       impl_->storage);
    next->liveSettings = impl_->liveSettings;
    next->liveSession = impl_->liveSession;
    next->pieces = std::move(impl_->pieces);
    next->renders = std::move(impl_->renders);
    next->renderScenes = std::move(impl_->renderScenes);
    impl_->stopLive();
    impl_.swap(next);
}

std::chrono::milliseconds UiHost::serviceLive() {
    impl_->serviceLive();
    if (impl_->liveCatchingUp && impl_->performance.smoothCapacity() > 0)
        return std::chrono::milliseconds(1);
    return impl_->performance.nextServiceDelay(impl_->liveSettings.mode ==
                                               LiveMode::smoothed);
}
void UiHost::stopLive() noexcept {
    try {
        impl_->stopLive();
    } catch (...) {
        impl_->liveEnabled = false;
    }
}

void UiHost::setLiveSession(std::optional<LiveSessionBoundary> session) {
    if (impl_->liveSession == session)
        return;
    impl_->stopLive();
    impl_->liveSession = std::move(session);
    impl_->liveError.clear();
    impl_->liveReceived = impl_->liveInvalid = impl_->liveSkipped = 0;
    impl_->performance = {};
}

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
                R"({"protocolVersion":1,"implementation":"native","supportedMethods":["getCapabilities","getChanges","getLiveChanges","getLiveSpeciesCounts","getCatalog","listSessions","listSessionsPage","prepareReplay","getMaterialEvents")";
            if (impl_->pieces)
                result +=
                    R"(,"startPieceFreeze","startPieceImport","getPieceJob","cancelPieceJob","openManagedPiece")";
            if (impl_->liveAvailable())
                result +=
                    R"(,"getLiveState","setLiveEnabled","getLiveOutcome","setLiveSettings")";
            if (impl_->playback)
                result +=
                    R"(,"getTransport","setTransport","getAudioState","setAudioEnabled","getAudioDevices","setAudioDevice")";
            if (impl_->voices)
                result +=
                    R"(,"getVoiceLibrary","getVoice","setVoiceParameter","saveVoice","revertVoice","getComposition","setComposition","getSpatial","setSpatial")";
            if (!impl_->storage.soundscapes.empty() && impl_->voices)
                result +=
                    R"(,"listSoundscapes","saveSoundscape","recallSoundscape")";
            if (impl_->renders)
                result +=
                    R"(,"startSoundscapeRender","getRenderJob","cancelRender","listExports")";
            if (impl_->voices)
                result += R"(,"listVoicePresets","applyVoicePreset")";
            result +=
                std::string("],\"audioAvailable\":") +
                (impl_->playback ? "true" : "false") +
                ",\"libraryAvailable\":" + (impl_->library ? "true" : "false") +
                ",\"supportsSmoothedLive\":" +
                (impl_->liveAvailable() ? "true" : "false") +
                ",\"maxEventBatch\":500,\"maxVisibleMarkers\":500}";
        } else if (impl_->playback && method == "getLiveState") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            result = impl_->liveState();
        } else if (impl_->liveAvailable() && method == "setLiveSettings") {
            Object args(impl_->db.get(), arguments,
                        {"mode", "density", "composition", "spatial"});
            impl_->updateLiveSettings(args);
            result = impl_->liveState();
        } else if (impl_->liveAvailable() && method == "getLiveOutcome") {
            Object args(impl_->db.get(), arguments, {"eventId"});
            result = impl_->liveOutcome(args.text("eventId", 2048));
        } else if (impl_->playback && method == "setLiveEnabled") {
            Object args(impl_->db.get(), arguments, {"enabled"});
            if (args.boolean("enabled")) {
                try {
                    impl_->startLive();
                } catch (const std::exception &error) {
                    impl_->stopLive();
                    impl_->liveError = error.what();
                }
            } else {
                impl_->stopLive();
                impl_->liveError.clear();
            }
            result = impl_->liveState();
        } else if (impl_->playback &&
                   (method == "getTransport" || method == "getAudioState")) {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            impl_->playback->service();
            if (method == "getTransport")
                impl_->playback->heartbeat();
            const auto snapshot = impl_->playback->snapshot();
            result =
                method == "getTransport"
                    ? transportView(snapshot)
                    : audioView(snapshot, impl_->playback->audioDevices(false));
        } else if (impl_->playback && method == "getAudioDevices") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            const auto catalog = impl_->playback->audioDevices();
            result = deviceCatalogView(catalog, impl_->playback->snapshot());
        } else if (impl_->playback && method == "setAudioDevice") {
            Object args(impl_->db.get(), arguments,
                        {"controlRevision", "preparationId", "generation",
                         "configuration"});
            Object config(impl_->db.get(), args.object("configuration"),
                          {"mode", "deviceId", "bufferFrames"});
            AudioDeviceConfiguration configuration;
            const auto mode = config.text("mode", 16);
            if (mode == "systemDefault")
                configuration.mode = AudioDeviceMode::systemDefault;
            else if (mode == "explicitDevice")
                configuration.mode = AudioDeviceMode::explicitDevice;
            else
                throw std::invalid_argument("unknown audio device mode");
            if (!config.null("deviceId"))
                configuration.deviceId = config.text("deviceId", 64);
            configuration.bufferFrames = static_cast<int>(
                config.number("bufferFrames", AudioRenderer::maxBlockFrames));
            std::uint64_t preparation{};
            if (!args.null("preparationId")) {
                const auto identity = args.text("preparationId", 32);
                if (!identity.starts_with("audio-"))
                    throw std::invalid_argument("invalid preparation ID");
                preparation = static_cast<std::uint64_t>(
                    integer(std::string_view(identity).substr(6), INT64_MAX));
            }
            const auto reply = impl_->playback->setAudioDevice(
                std::move(configuration),
                static_cast<std::uint64_t>(
                    integer(args.text("controlRevision", 20), INT64_MAX)),
                preparation,
                static_cast<std::uint64_t>(
                    integer(args.text("generation", 20), INT64_MAX)));
            if (impl_->liveEnabled && (reply.ok || !reply.snapshot.enabled)) {
                impl_->retireLive();
                impl_->liveError = reply.ok ? "" : playbackMessage(reply.error);
            }
            if (!reply.ok) {
                const auto code =
                    reply.error == PlaybackError::staleControlRevision
                        ? "stale_generation"
                    : reply.error == PlaybackError::stalePreparation ||
                            reply.error == PlaybackError::staleGeneration
                        ? "stale_generation"
                    : reply.error == PlaybackError::invalidDeviceConfiguration
                        ? "invalid_request"
                        : "unavailable";
                return failure(code, playbackMessage(reply.error));
            }
            result = deviceCatalogView(impl_->playback->audioDevices(false),
                                       reply.snapshot);
        } else if (impl_->playback && method == "setAudioEnabled") {
            Object args(impl_->db.get(), arguments, {"enabled"});
            const bool enabled = args.boolean("enabled");
            const auto reply = impl_->playback->enable(enabled);
            if (!reply.ok)
                return failure("unavailable", playbackMessage(reply.error));
            if (!enabled) {
                impl_->retireLive();
                impl_->liveError.clear();
            }
            result =
                audioView(reply.snapshot, impl_->playback->audioDevices(false));
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
            if (safety || action == "pause") {
                impl_->retireLive();
                impl_->liveError.clear();
            }
            result = transportView(reply.snapshot);
        } else if (impl_->voices && method == "getComposition") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            result = impl_->compositionState();
        } else if (impl_->voices && method == "setComposition") {
            Object args(impl_->db.get(), arguments,
                        {"settings", "preparationId", "generation"});
            const auto settings =
                readComposition(impl_->db.get(), args.object("settings"));
            if (!impl_->prepared)
                throw std::invalid_argument(
                    "Prepare material before composing");
            const auto snapshot = impl_->playback ? impl_->playback->snapshot()
                                                  : PlaybackSnapshot{};
            const auto currentId =
                impl_->playback ? preparationId(snapshot)
                                : "native-" + std::to_string(impl_->generation);
            const auto currentGeneration =
                impl_->playback ? snapshot.generation : impl_->generation;
            if (args.text("preparationId", 32) != currentId ||
                static_cast<std::uint64_t>(
                    integer(args.text("generation", 20), INT64_MAX)) !=
                    currentGeneration)
                return failure("stale_generation",
                               "Composition preparation is no longer current");
            for (const auto &voice : impl_->voices->voices())
                if (voice.gestureActive)
                    throw std::invalid_argument(
                        "Finish voice gestures before changing composition");
            auto context = impl_->context;
            context.audio.composition = settings;
            context.profiles = impl_->currentProfiles(*impl_->prepared);
            if (impl_->playback) {
                const auto quiet = impl_->playback->enable(false);
                if (!quiet.ok)
                    return failure("unavailable", playbackMessage(quiet.error));
            }
            impl_->prepare(*impl_->prepared, impl_->saved, std::move(context));
            result = impl_->compositionState();
        } else if (impl_->voices && method == "getSpatial") {
            Object args(impl_->db.get(), arguments);
            if (args.size())
                throw std::invalid_argument("unexpected arguments");
            result = impl_->spatialState();
        } else if (impl_->voices && method == "setSpatial") {
            Object args(impl_->db.get(), arguments,
                        {"settings", "preparationId", "generation"});
            const auto settings =
                readSpatial(impl_->db.get(), args.object("settings"));
            if (!impl_->prepared)
                throw std::invalid_argument(
                    "Prepare material before spatial editing");
            const auto snapshot = impl_->playback ? impl_->playback->snapshot()
                                                  : PlaybackSnapshot{};
            const auto currentId =
                impl_->playback ? preparationId(snapshot)
                                : "native-" + std::to_string(impl_->generation);
            const auto currentGeneration =
                impl_->playback ? snapshot.generation : impl_->generation;
            if (args.text("preparationId", 32) != currentId ||
                static_cast<std::uint64_t>(
                    integer(args.text("generation", 20), INT64_MAX)) !=
                    currentGeneration)
                return failure("stale_generation",
                               "Spatial preparation is no longer current");
            for (const auto &voice : impl_->voices->voices())
                if (voice.gestureActive)
                    throw std::invalid_argument("Finish voice gestures before "
                                                "changing spatial settings");
            auto context = impl_->context;
            context.audio.spatial = settings;
            context.profiles = impl_->currentProfiles(*impl_->prepared);
            if (impl_->playback) {
                const auto quiet = impl_->playback->enable(false);
                if (!quiet.ok)
                    return failure("unavailable", playbackMessage(quiet.error));
            }
            impl_->prepare(*impl_->prepared, impl_->saved, std::move(context));
            result = impl_->spatialState();
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
        } else if (impl_->voices && method == "listVoicePresets") {
            Object args(impl_->db.get(), arguments, {"voiceId"});
            const auto voice = impl_->voices->getVoice(args.text("voiceId"));
            result = "{\"listeningApproved\":false,\"items\":[";
            std::string items = "[";
            for (const auto &preset : speciesPresets())
                if (voice.speciesId && *voice.speciesId == preset.speciesId) {
                    append(items,
                           "{\"id\":" + quoteJson(preset.id) +
                               ",\"name\":" + quoteJson(preset.name) +
                               ",\"speciesId\":" + quoteJson(preset.speciesId) +
                               ",\"scientificName\":" +
                               quoteJson(preset.scientificName) +
                               ",\"description\":" +
                               quoteJson(preset.description) +
                               ",\"evidenceStatus\":" +
                               quoteJson(preset.evidenceStatus) +
                               ",\"researchPath\":" +
                               quoteJson(preset.researchPath) + "}");
                }
            result = "{\"listeningApproved\":false,\"items\":" + items + "]}";
        } else if (impl_->voices && method == "applyVoicePreset") {
            Object args(impl_->db.get(), arguments, {"voiceId", "presetId"});
            const auto before = impl_->voices->getVoice(args.text("voiceId"));
            const auto *preset = speciesPreset(args.text("presetId"));
            if (!preset || !before.speciesId ||
                *before.speciesId != preset->speciesId)
                throw std::invalid_argument(
                    "Preset does not match the selected species");
            const std::array<VoiceProfile, 1> profiles{
                {{*before.speciesId, preset->settings}}};
            auto update = impl_->voices->stageProfiles(profiles);
            auto next = before;
            next.settings = preset->settings;
            impl_->applyVoice(next, before);
            if (!impl_->voices->commitProfiles(std::move(update)))
                throw std::runtime_error(
                    "Voice changed during preset application");
            result = voiceView(impl_->voices->getVoice(before.id));
        } else if (impl_->voices && !impl_->storage.soundscapes.empty() &&
                   method == "listSoundscapes") {
            Object args(impl_->db.get(), arguments, {"limit"});
            const auto limit =
                static_cast<std::size_t>(args.number("limit", 100));
            if (!limit)
                throw std::invalid_argument("Positive limit required");
            const auto scenes =
                listSoundscapes(impl_->storage.soundscapes, limit);
            std::string items = "[";
            std::size_t unmanaged{};
            for (const auto &scene : scenes) {
                if (!Impl::opaqueId(scene.path.stem().string())) {
                    ++unmanaged;
                    continue;
                }
                append(items, impl_->sceneView(scene));
            }
            result = "{\"items\":" + items + "],\"limited\":" +
                     (scenes.size() == limit || unmanaged ? "true" : "false") +
                     ",\"unmanagedFiles\":" + std::to_string(unmanaged) + "}";
        } else if (impl_->voices && !impl_->storage.soundscapes.empty() &&
                   method == "saveSoundscape") {
            Object args(impl_->db.get(), arguments, {"name"});
            if (!impl_->prepared)
                throw std::invalid_argument(
                    "Prepare material before saving a soundscape");
            for (const auto &v : impl_->voices->voices())
                if (v.gestureActive)
                    throw std::invalid_argument(
                        "Finish voice gestures before saving");
            auto capture = impl_->context;
            capture.schemaRevision = soundscapeSchemaRevision;
            capture.synthesisRevision = soundscapeSynthesisRevision;
            capture.mappingRevision = soundscapeMappingRevision;
            capture.name = args.text("name", 256);
            capture.profiles = impl_->currentProfiles(*impl_->prepared);
            const auto scene =
                captureSoundscape(*impl_->prepared, std::move(capture));
            const auto sid = impl_->newId();
            saveSoundscape(impl_->scenePath(sid), scene);
            result = impl_->sceneView(scene, sid);
        } else if (impl_->voices && !impl_->storage.soundscapes.empty() &&
                   method == "recallSoundscape") {
            Object args(impl_->db.get(), arguments, {"soundscapeId"});
            const auto sid = args.text("soundscapeId", 32);
            if (impl_->playback)
                impl_->playback->enable(false);
            const auto scene = loadSoundscape(impl_->scenePath(sid));
            std::map<std::string, std::string> names;
            for (const auto &e : scene.timeline().events())
                if (e.speciesId) {
                    const Object event(impl_->db.get(), e.normalizedJson);
                    names.try_emplace(*e.speciesId,
                                      event.optionalText("common_name"));
                }
            std::vector<VoiceSpecies> species;
            for (const auto &[speciesId, name] : names)
                species.push_back({speciesId, name.empty() ? speciesId : name});
            auto update =
                impl_->voices->stageProfiles(scene.capture().profiles, species);
            auto response = impl_->sceneView(scene, sid);
            impl_->prepare(scene.timeline(), Impl::sceneSession(scene, sid),
                           scene.capture(), std::move(update));
            result = "{\"soundscape\":" + response +
                     ",\"preparation\":" + impl_->preparation() + "}";
        } else if (impl_->renders && method == "listExports") {
            Object args(impl_->db.get(), arguments, {"limit"});
            const auto history = listExports(
                impl_->storage.exports,
                static_cast<std::size_t>(args.number("limit", 100)));
            std::string items = "[";
            for (const auto &item : history.items) {
                append(
                    items,
                    "{\"artifactId\":" + quoteJson(item.artifactId) +
                        ",\"sceneName\":" + quoteJson(item.sceneName) +
                        ",\"sceneHash\":" + quoteJson(item.sceneHash) +
                        ",\"engineSourceSha256\":" +
                        quoteJson(item.engineSourceSha256) + ",\"bytes\":" +
                        quoteJson(std::to_string(item.bytes)) + ",\"frames\":" +
                        quoteJson(std::to_string(item.frames)) +
                        ",\"durationUs\":" +
                        quoteJson(std::to_string(item.durationUs)) +
                        ",\"sampleRate\":" + std::to_string(item.sampleRate) +
                        "}");
            }
            std::string activeRender = "null";
            for (const auto &[id, sceneId] : impl_->renderScenes) {
                (void)sceneId;
                const auto job = impl_->renders->get(id);
                if (job.state == RenderJobState::running) {
                    activeRender = impl_->renderView(job);
                    break;
                }
            }
            result = "{\"items\":" + items +
                     "],\"activeRender\":" + activeRender +
                     ",\"limited\":" + (history.limited ? "true" : "false") +
                     ",\"unavailableFiles\":" +
                     std::to_string(history.unavailableFiles) +
                     ",\"incompleteRenders\":" +
                     std::to_string(history.incompleteRenders) + "}";
        } else if (impl_->renders && method == "startSoundscapeRender") {
            Object args(impl_->db.get(), arguments, {"soundscapeId"});
            const auto sid = args.text("soundscapeId", 32);
            const auto scene = inspectSoundscape(impl_->scenePath(sid));
            const auto jid = impl_->newId();
            const auto job = impl_->renders->start(impl_->scenePath(sid),
                                                   scene.contentHash, jid);
            for (auto it = impl_->renderScenes.begin();
                 it != impl_->renderScenes.end();) {
                try {
                    (void)impl_->renders->get(it->first);
                    ++it;
                } catch (const RenderJobError &error) {
                    if (error.code() != RenderJobErrorCode::notFound)
                        throw;
                    it = impl_->renderScenes.erase(it);
                }
            }
            impl_->renderScenes.emplace(jid, sid);
            result = impl_->renderView(job);
        } else if (impl_->renders &&
                   (method == "getRenderJob" || method == "cancelRender")) {
            Object args(impl_->db.get(), arguments, {"jobId"});
            const auto jid = args.text("jobId", 32);
            if (!Impl::opaqueId(jid))
                throw std::invalid_argument("Invalid render ID");
            result = impl_->renderView(method == "getRenderJob"
                                           ? impl_->renders->get(jid)
                                           : impl_->renders->cancel(jid));
        } else if (method == "getLiveChanges") {
            result = impl_->liveChanges(Object(impl_->db.get(), arguments,
                {"sessionId", "storeId", "afterSequence", "limit"}));
        } else if (method == "getLiveSpeciesCounts") {
            Object args(impl_->db.get(), arguments, {"sessionId", "cursor", "limit"});
            const auto limit = args.number("limit", 250);
            if (limit < 1)
                throw std::invalid_argument("positive page limit required");
            const auto offset = args.has("cursor") && !args.null("cursor")
                ? integer(args.text("cursor", 20), INT64_MAX) : 0;
            result = impl_->liveSpeciesRanking.page(impl_->db.get(), impl_->liveSession,
                                       args.text("sessionId", 128),
                                       static_cast<std::size_t>(offset),
                                       static_cast<std::size_t>(limit));
        } else if (method == "getChanges") {
            result =
                impl_->changes(Object(impl_->db.get(), arguments,
                                      {"storeId", "afterSequence", "limit"}));
        } else if (method == "getCatalog") {
            result = impl_->catalog(
                Object(impl_->db.get(), arguments, {"material"}));
        } else if (impl_->pieces && method == "startPieceFreeze") {
            Object args(impl_->db.get(), arguments,
                        {"requestId", "name", "startUtc", "endUtc", "sourceId",
                         "speciesId"});
            const auto id = args.text("requestId", 32);
            if (!Impl::opaqueId(id))
                throw std::invalid_argument("Invalid saved piece request ID.");
            const auto filter =
                [&](std::string_view field) -> std::optional<std::string> {
                if (args.null(field))
                    return {};
                return args.text(field, field == "sourceId" ? 128 : 512);
            };
            const auto now =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            FreezePieceRequest capture{id,
                                       args.text("name", 128),
                                       formatUtc(now),
                                       args.text("startUtc", 64),
                                       args.text("endUtc", 64),
                                       filter("sourceId"),
                                       filter("speciesId"),
                                       0};
            result = jobView(impl_->pieces->start(
                impl_->newId(), "freeze", arguments,
                [path = impl_->storage.managedLibrary,
                 capture = std::move(capture)](const PieceProgress &progress) {
                    return freezeObservedPiece(path, capture, progress);
                }));
        } else if (impl_->pieces && method == "startPieceImport") {
            Object args(impl_->db.get(), arguments, {"sessionId"});
            const auto id = args.text("sessionId", 128);
            if (!impl_->library ||
                std::filesystem::equivalent(impl_->attachedPath,
                                            impl_->storage.managedLibrary))
                throw std::invalid_argument(
                    "Attach a different wildlife library to import a piece.");
            result = jobView(impl_->pieces->start(
                impl_->newId(), "import",
                impl_->attachedPath.string() + '\0' + id,
                [source = impl_->attachedPath,
                 path = impl_->storage.managedLibrary,
                 id](const PieceProgress &progress) {
                    return importSavedPiece(source, path, id, progress);
                }));
        } else if (impl_->pieces && method == "getPieceJob") {
            Object args(impl_->db.get(), arguments, {"jobId"});
            result = jobView(impl_->pieces->get(args.optionalText("jobId")));
        } else if (impl_->pieces && method == "cancelPieceJob") {
            Object args(impl_->db.get(), arguments, {"jobId"});
            result = jobView(impl_->pieces->cancel(args.text("jobId", 32)));
        } else if (impl_->pieces && method == "openManagedPiece") {
            Object args(impl_->db.get(), arguments, {"sessionId"});
            const auto id = args.text("sessionId", 128);
            if (id.empty() || id == "latest")
                throw std::invalid_argument("Choose a specific saved piece.");
            const auto managed = impl_->storage.managedLibrary;
            const bool changed =
                !impl_->library ||
                !std::filesystem::equivalent(impl_->attachedPath, managed);
            const auto saved = changed ? ReadOnlyLibrary(managed).session(id)
                                       : impl_->library->session(id);
            if (changed) {
                setLibrary(managed);
                if (impl_->playback)
                    impl_->playback->enable(false);
            }
            result = "{\"session\":" + sessionView(saved) +
                     ",\"libraryChanged\":" + (changed ? "true}" : "false}");
        } else if (method == "listSessionsPage") {
            Object args(impl_->db.get(), arguments, {"limit", "cursor"});
            if (!impl_->library)
                throw std::runtime_error("Wildlife library is unavailable");
            const auto page = impl_->library->sessionPage(
                static_cast<std::size_t>(args.number("limit", 100)),
                args.optionalText("cursor"));
            std::string items = "[";
            for (const auto &saved : page.sessions)
                append(items, sessionView(saved));
            result = "{\"items\":" + items + "],\"nextCursor\":" +
                     (page.nextCursor ? quoteJson(*page.nextCursor) : "null") +
                     ",\"sourceId\":" + quoteJson(page.library.sourceId) +
                     ",\"revision\":" + quoteJson(page.library.revision) + "}";
        } else if (method == "listSessions") {
            Object args(impl_->db.get(), arguments, {"limit"});
            if (!impl_->library)
                throw std::runtime_error("Wildlife library is unavailable");
            result = "[";
            for (const auto &saved : impl_->library->sessions(
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
            result = impl_->preparation();
        } else if (method == "getMaterialEvents") {
            Object args(impl_->db.get(), arguments,
                        {"material", "offset", "limit", "revision"});
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
            const auto revision =
                impl_->materialRevision
                    ? impl_->materialRevision->sourceId + ":" +
                          impl_->materialRevision->revision
                    : "scene:" + impl_->materialSaved.contentHash;
            const auto requestedRevision = args.optionalText("revision");
            if (!requestedRevision.empty() && requestedRevision != revision)
                throw StaleLibraryCursor("Library changed between material "
                                         "pages. Reload the piece.");
            const auto &events = impl_->material->events();
            result = "[";
            for (auto i = offset; i < std::min(events.size(), offset + limit);
                 ++i)
                append(result,
                       eventView(impl_->db.get(), events[i].normalizedJson,
                                 events[i].revisionSequence));
            result = "{\"total\":" + std::to_string(events.size()) +
                     ",\"revision\":" + quoteJson(revision) +
                     ",\"events\":" + result + "]}";
        } else
            return failure(
                "unsupported",
                "This capability is not implemented by the native data host");
        return "{\"requestId\":" + quoteJson(id) +
               ",\"ok\":true,\"result\":" + result + "}";
    } catch (const StaleLibraryCursor &e) {
        return failure("stale_generation", e.what());
    } catch (const LibraryOpenError &e) {
        return failure("unavailable", e.what());
    } catch (const SoundscapeError &e) {
        return failure(e.code() == SoundscapeErrorCode::invalidArgument
                           ? "invalid_request"
                           : "unavailable",
                       e.what());
    } catch (const RenderJobError &e) {
        return failure(e.code() == RenderJobErrorCode::invalidArgument ||
                               e.code() == RenderJobErrorCode::notFound
                           ? "invalid_request"
                           : "unavailable",
                       e.what());
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
