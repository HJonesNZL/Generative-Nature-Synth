#include <atlas/projects.hpp>
#include <atlas/soundscapes.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <thread>

namespace {
using namespace atlas;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Temporary {
    fs::path directory =
        fs::canonical(fs::temp_directory_path()) /
        ("atlas-ui-project-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { fs::create_directory(directory); }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
};
// Parse response semantics with SQLite's existing JSON support, independently
// of the native host's envelope writer. No production database is connected.
class Json {
  public:
    Json() {
        check(sqlite3_open(":memory:", &db_) == SQLITE_OK,
              "Could not open disposable JSON parser");
    }
    ~Json() { sqlite3_close(db_); }
    std::string at(const std::string &json, const char *path) const {
        return query(json, path, "SELECT json_extract(?1,?2)");
    }
    std::string type(const std::string &json, const char *path) const {
        return query(json, path, "SELECT json_type(?1,?2)");
    }

  private:
    std::string query(const std::string &json, const char *path,
                      const char *sql) const {
        sqlite3_stmt *raw{};
        check(sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) == SQLITE_OK,
              "Could not prepare JSON parser");
        const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>
            statement(raw, sqlite3_finalize);
        check(sqlite3_bind_text(raw, 1, json.data(),
                                static_cast<int>(json.size()),
                                SQLITE_TRANSIENT) == SQLITE_OK &&
                  sqlite3_bind_text(raw, 2, path, -1, SQLITE_TRANSIENT) ==
                      SQLITE_OK &&
                  sqlite3_step(raw) == SQLITE_ROW,
              "Host returned invalid JSON");
        const auto *value = sqlite3_column_text(raw, 0);
        return value ? reinterpret_cast<const char *>(value) : "";
    }

    sqlite3 *db_{};
};
class Client {
  public:
    Client(UiHost &host, const Json &json) : host_(host), json_(json) {}
    std::string call(std::string_view method, std::string_view args = "{}") {
        auto response = host_.request(
            "{\"protocolVersion\":1,\"requestId\":\"integration-" +
            std::to_string(++serial_) + "\",\"method\":\"" +
            std::string(method) + "\",\"args\":" + std::string(args) + "}");
        check(json_.at(response, "$.ok") == "1", response.c_str());
        return response;
    }
    std::string job(std::string_view method, const std::string &id) {
        return call(method, "{\"jobId\":\"" + id + "\"}");
    }

  private:
    UiHost &host_;
    const Json &json_;
    unsigned serial_{};
};
Soundscape scene(Microseconds duration) {
    const auto start = parseUtc("2026-11-07T00:00:00Z");
    SoundscapeCapture capture;
    capture.name = "Disposable UI workflow fixture";
    capture.sourceLibraryLabel = "Synthetic source already absent";
    capture.session.id = "fixture:copied";
    capture.session.name = "Synthetic copied material";
    capture.session.state = "frozen";
    capture.session.origin = "demo";
    capture.session.contentHash = std::string(64, '0');
    capture.session.metadataIntegrity = "legacy-metadata-unverified";
    capture.session.observedWindowPolicy = "preserve_requested";
    capture.session.eventCount = 1;
    capture.session.observedBounds = std::pair{start, start + duration};
    capture.session.arrivalBounds = capture.session.observedBounds;
    capture.bounds = capture.session.observedBounds;
    capture.audio = {44100, 60, .009, .5, -17};
    capture.profiles = {{"", VoiceSettings{}},
                        {"fixture:species", VoiceSettings{}}};
    Event event;
    event.id = "fixture:one";
    event.sourceId = "fixture";
    event.kind = "gps_fix";
    event.locationKind = "individual_fix";
    event.speciesId = "fixture:species";
    event.individualId = "fixture:individual";
    event.observedAt = event.firstSeenAt = event.ingestedAt = start;
    event.revisionSequence = 1;
    event.position = Position{-40, 170};
    event.normalizedJson =
        R"({"schema_version":1,"event_id":"fixture:one","source_id":"fixture","provider_event_id":"one","observed_at_utc":"2026-11-07T00:00:00Z","event_kind":"gps_fix","location_kind":"individual_fix","species_id":"fixture:species","individual_id":"fixture:individual","latitude":-40,"longitude":170,"coordinate_order":"latitude,longitude","provenance_mode":"demo","first_seen_at_utc":"2026-11-07T00:00:00Z","ingested_at_utc":"2026-11-07T00:00:00Z"})";
    Timeline timeline({std::move(event)}, capture.basis, capture.speed,
                      capture.bounds);
    return captureSoundscape(timeline, std::move(capture));
}
std::string waitFor(Client &client, const Json &json, const std::string &id,
                    bool progress) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;) {
        auto response = client.job("getRenderJob", id);
        const auto state = json.at(response, "$.result.state");
        if (progress) {
            check(state == "rendering",
                  "Long render finished before lifecycle check");
            if (std::stod(json.at(response, "$.result.progress")) > 0)
                return response;
        } else if (state != "rendering" && state != "queued")
            return response;
        check(std::chrono::steady_clock::now() < deadline,
              "Render did not reach expected state");
        std::this_thread::sleep_for(1ms);
    }
}
std::string bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(input.good(), "Expected fixture file is missing");
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
void tests() {
    Temporary fixture;
    Json json;
    const auto scenes = fixture.directory / "soundscapes";
    const auto exports = fixture.directory / "exports";
    fs::create_directory(scenes);
    fs::create_directory(exports);
    const std::string shortId(32, 'a'), longId(32, 'b');
    const auto shortScene = scene(200000);
    saveSoundscape(scenes / (shortId + ".atlas-scene"), shortScene);
    saveSoundscape(scenes / (longId + ".atlas-scene"), scene(1700000000));
    const auto voicesPath = fixture.directory / "voices.sqlite3";
    VoiceStore voices(voicesPath);
    const UiHostStorage storage{scenes, exports, std::string(64, 'c')};
    std::string completedId;
    {
        UiHost host({}, nullptr, &voices, storage);
        Client client(host, json);
        const auto running = client.call(
            "startSoundscapeRender", "{\"soundscapeId\":\"" + longId + "\"}");
        const auto jobId = json.at(running, "$.result.jobId");
        waitFor(client, json, jobId, true);
        host.setLibrary({});
        const auto preserved = client.job("getRenderJob", jobId);
        check(json.at(preserved, "$.result.state") == "rendering" &&
                  json.at(preserved, "$.result.soundscapeId") == longId,
              "Scene-only library switch lost active renderer ownership");
        const auto activeHistory =
            client.call("listExports", "{\"limit\":100}");
        check(json.at(activeHistory, "$.result.activeRender.jobId") == jobId &&
                  json.at(activeHistory,
                          "$.result.activeRender.soundscapeId") == longId &&
                  json.at(activeHistory, "$.result.activeRender.state") ==
                      "rendering",
              "Reloaded export history cannot recover the active render");
        const auto originalRecipe = bytes(scenes / (longId + ".atlas-scene"));
        const auto originalHash =
            loadSoundscape(scenes / (longId + ".atlas-scene")).contentHash();
        const std::string importedId(32, 'd');
        exportSoundscape(scenes / (shortId + ".atlas-scene"),
                         scenes / (importedId + ".atlas-scene"));
        const auto importedCatalogue =
            client.call("listSoundscapes", "{\"limit\":100}");
        check(json.at(importedCatalogue, "$.result.items[2].id").size() == 32 &&
                  json.at(importedCatalogue, "$.result.items[3]").empty(),
              "Copied scene import was not visible during rendering");
        const auto historyAfterImport =
            client.call("listExports", "{\"limit\":100}");
        check(
            json.at(historyAfterImport, "$.result.activeRender.jobId") ==
                    jobId &&
                json.at(historyAfterImport,
                        "$.result.activeRender.soundscapeId") == longId &&
                json.at(client.job("getRenderJob", jobId), "$.result.state") ==
                    "rendering" &&
                bytes(scenes / (longId + ".atlas-scene")) == originalRecipe &&
                loadSoundscape(scenes / (longId + ".atlas-scene"))
                        .contentHash() == originalHash,
            "Import changed the active render job or its sealed source recipe");
        const auto invalid = fixture.directory / "invalid-library.sqlite3";
        {
            std::ofstream file(invalid);
            file << "not a wildlife database";
        }
        bool rejected{};
        try {
            host.setLibrary(invalid);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "Invalid replacement library was accepted");
        check(json.at(client.call("getCapabilities"),
                      "$.result.libraryAvailable") == "0" &&
                  json.at(client.job("getRenderJob", jobId),
                          "$.result.state") == "rendering",
              "Failed library switch changed current host or render job");
        client.job("cancelRender", jobId);
        const auto cancelled = waitFor(client, json, jobId, false);
        check(json.at(cancelled, "$.result.state") == "cancelled" &&
                  json.at(cancelled, "$.result.artifactId").empty() &&
                  !fs::exists(exports / (jobId + ".wav")),
              "Cancelled render published a completed artifact");
        const auto shortJob = client.call(
            "startSoundscapeRender", "{\"soundscapeId\":\"" + shortId + "\"}");
        completedId = json.at(shortJob, "$.result.jobId");
        check(json.at(waitFor(client, json, completedId, false),
                      "$.result.state") == "complete",
              "Short scene render failed");
        check(json.type(client.call("listExports", "{\"limit\":100}"),
                        "$.result.activeRender") == "null",
              "Completed render must clear activeRender to explicit JSON null");
    }
    {
        UiHost restarted({}, nullptr, &voices, storage);
        Client client(restarted, json);
        const auto history = client.call("listExports", "{\"limit\":100}");
        check(json.at(history, "$.result.items[0].artifactId") == completedId &&
                  json.at(history, "$.result.items[1]").empty() &&
                  json.type(history, "$.result.activeRender") == "null" &&
                  json.at(history, "$.result.items[0].sceneHash") ==
                      shortScene.contentHash(),
              "Completed export did not survive host recreation exactly once");
    }
    const auto backup = fixture.directory / "portable.atlas-project";
    createProjectBackup(voicesPath, {scenes / (shortId + ".atlas-scene")},
                        backup);
    const auto restored = fixture.directory / "restored";
    restoreProjectBackup(backup, restored);
    VoiceStore restoredVoices(restored / "voices.sqlite3");
    UiHost restoredHost(
        {}, nullptr, &restoredVoices,
        {restored / "soundscapes", restored / "exports", std::string(64, 'c')});
    Client client(restoredHost, json);
    check(json.at(client.call("getCapabilities"),
                  "$.result.libraryAvailable") == "0",
          "Restored host unexpectedly requires a wildlife library");
    const auto catalogue = client.call("listSoundscapes", "{\"limit\":100}");
    const auto restoredId = json.at(catalogue, "$.result.items[0].id");
    check(restoredId.size() == 32 && restoredId != shortId &&
              json.at(catalogue, "$.result.items[0].valid") == "1" &&
              json.at(catalogue, "$.result.items[1]").empty(),
          "Restored scene is not usable under a fresh managed identity");
    const auto recall = client.call("recallSoundscape", "{\"soundscapeId\":\"" +
                                                            restoredId + "\"}");
    check(json.at(recall, "$.result.preparation.eventCount") == "1",
          "Restored scene could not be recalled without its source library");
    const auto render = client.call(
        "startSoundscapeRender", "{\"soundscapeId\":\"" + restoredId + "\"}");
    const auto restoredJob = json.at(render, "$.result.jobId");
    check(json.at(waitFor(client, json, restoredJob, false),
                  "$.result.state") == "complete",
          "Restored scene could not render without its source library");
    check(bytes(restored / "soundscapes" / (restoredId + ".atlas-scene")) ==
                  bytes(scenes / (shortId + ".atlas-scene")) &&
              bytes(restored / "exports" / (restoredJob + ".wav")) ==
                  bytes(exports / (completedId + ".wav")),
          "Restored scene changed sealed recipe or rendered WAV bytes");
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "UI project lifecycle checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
