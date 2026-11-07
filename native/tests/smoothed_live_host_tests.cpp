#include <array>
#include <atlas/collection.hpp>
#include <atlas/piece_writer.hpp>
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
using namespace atlas;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Temporary {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("atlas-live-host-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directory(path); }
    ~Temporary() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct Driver : OutputDriver {
    AudioDeviceCatalog audioDevices() override {
        AudioDeviceCatalog catalog;
        catalog.available = true;
        catalog.devices.push_back({"fixture",
                                   "Fixture output",
                                   "Fixture",
                                   true,
                                   2,
                                   {48000},
                                   {128},
                                   128});
        return catalog;
    }
    std::string start(PlaybackRuntime &runtime, std::uint32_t rate) override {
        runtime.deviceStarted(rate, 128, 2);
        return {};
    }
    void stop() noexcept override {}
};
struct Harness {
    PlaybackRuntime runtime{std::make_unique<Driver>()};
    double energy{};
    void tick() {
        std::array<float, 128> l{}, r{};
        float *out[]{l.data(), r.data()};
        runtime.process(out, 2, 128);
        for (auto v : l) {
            check(std::isfinite(v), "finite live PCM");
            energy += std::abs(v);
        }
    }
    template <class F> auto run(F function) {
        auto future = std::async(std::launch::async, function);
        while (future.wait_for(std::chrono::milliseconds(1)) !=
               std::future_status::ready)
            tick();
        return future.get();
    }
};
std::string request(UiHost &host, std::string method, std::string args = "{}") {
    const auto response = host.request(
        "{\"protocolVersion\":1,\"requestId\":\"fixture\",\"method\":\"" +
        method + "\",\"args\":" + args + "}");
    check(response.find("\"ok\":true") != std::string::npos, response.c_str());
    return response;
}
std::string bird(std::string id, std::string at, std::string longitude = "174",
                 std::string species = "42") {
    return "{\"id\":\"" + id + "\",\"timestamp\":\"" + at +
           "\",\"coords\":{\"lat\":-41,\"lon\":" + longitude +
           "},\"species\":{\"id\":" + species +
           ",\"scientificName\":\"Synthetic "
           "species\",\"commonName\":\"Synthetic "
           "bird\"},\"station\":{\"id\":\"fixture\"}}";
}
auto fetch(std::string nodes) {
    return [nodes](const CollectionRequest &) {
        return "{\"data\":{\"detections\":{\"nodes\":[" + nodes +
               "],\"pageInfo\":{\"hasNextPage\":false,\"endCursor\":null}}}}";
    };
}
void contains(const std::string &state, const std::string &field) {
    check(state.find(field) != std::string::npos, state.c_str());
}
} // namespace
std::int64_t number(const std::string &json, const std::string &key) {
    sqlite3 *db{};
    check(sqlite3_open(":memory:", &db) == SQLITE_OK, "json db");
    sqlite3_stmt *s{};
    const auto sql = "SELECT json_extract(?1,'$.result." + key + "')";
    check(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK,
          "json statement");
    sqlite3_bind_text(s, 1, json.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(s) == SQLITE_ROW, "json step");
    const auto result = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return result;
}
int main() {
    try {
        Temporary temporary;
        const auto db = temporary.path / "smoothed.sqlite3";
        Collector collector(db);
        collector.collect("birdweather", "2026-11-07T01:05:00Z",
                          fetch(bird("baseline", "2026-11-07T01:04:59Z")));
        const std::array<VoiceSpecies, 1> species{
            {{"birdweather:species:42", "Synthetic bird"}}};
        VoiceStore voices(temporary.path / "voices.sqlite3", species);
        Harness audio;
        UiHost host(db, &audio.runtime, &voices, {{}, {}, {}, db});
        request(host, "setLiveSettings", R"({"mode":"smoothed"})");
        audio.run([&] {
            return request(host, "setLiveEnabled", R"({"enabled":true})");
        });
        const auto collectBurst = [&](const std::string &prefix,
                                      std::size_t count,
                                      const std::string &pollAt,
                                      const std::string &observedAt) {
            std::vector<std::string> pages((count + 249) / 250);
            for (std::size_t i = 0; i < count; ++i) {
                if (i % 250)
                    pages[i / 250] += ",";
                pages[i / 250] += bird(prefix + std::to_string(i), observedAt);
            }
            std::size_t page{};
            const auto result = collector.collect(
                "birdweather", pollAt, [&](const CollectionRequest &) {
                    check(page < pages.size(), "bounded collector page count");
                    const auto index = page++;
                    const bool more = page < pages.size();
                    return "{\"data\":{\"detections\":{\"nodes\":[" +
                           pages[index] + "],\"pageInfo\":{\"hasNextPage\":" +
                           (more ? "true" : "false") + ",\"endCursor\":" +
                           (more ? "\"next-fixture-page-" +
                                       std::to_string(page) + "\""
                                 : "null") +
                           "}}}}";
                });
            check(result.inserted == count && result.complete,
                  "complete genuine paged fixture insert");
        };
        collectBurst("burst-", 600, "2026-11-07T01:05:02Z",
                     "2026-11-07T01:05:01Z");
        audio.run([&] { host.serviceLive(); });
        auto state = request(host, "getLiveState");
        check(number(state, "admitted") < 600,
              "smoothed poll remains pending before callback onsets");
        const auto drain = [&](std::int64_t count) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(16);
            do {
                audio.runtime.heartbeat();
                for (int i = 0; i < 4; ++i)
                    audio.tick();
                audio.run([&] { host.serviceLive(); });
                const auto current = request(host, "getLiveState");
                if (number(current, "admitted") == count)
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error(request(host, "getLiveState"));
        };
        drain(600);
        state = request(host, "getLiveState");
        check(number(state, "received") == 600,
              ">512 poll drains across admission pages");
        check(number(state, "grouped") == 0 && number(state, "dropped") == 0 &&
                  number(state, "skippedRevisions") == 0,
              "smoothed keeps every detection without grouping or drops");
        check(audio.runtime.snapshot().liveTriggeredNotes == 600,
              "all host sounded outcomes match callback onsets");
        check(audio.energy > .01, "smoothed produces finite callback PCM");
        for (int i = 0; i < 600; ++i)
            contains(request(host, "getLiveOutcome",
                             "{\"eventId\":\"birdweather:burst-" +
                                 std::to_string(i) + "\"}"),
                     "\"status\":\"sounded\"");
        collectBurst("correct-", 100, "2026-11-07T01:05:04Z",
                     "2026-11-07T01:05:03Z");
        audio.run([&] { host.serviceLive(); });
        const auto corrected = collector.collect(
            "birdweather", "2026-11-07T01:05:05Z",
            fetch(bird("correct-99", "2026-11-07T01:05:03Z", "170")));
        check(corrected.corrected == 1,
              "genuine pending detection revision committed");
        drain(699);
        contains(request(host, "getLiveOutcome",
                         R"({"eventId":"birdweather:correct-99"})"),
                 "\"status\":\"skipped\"");
        check(audio.runtime.snapshot().liveTriggeredNotes == 699,
              "corrected pending revision cannot produce stale or duplicate "
              "onset");
        check(number(request(host, "getLiveState"), "dropped") == 1,
              "corrected pending event is accounted explicitly");
        collectBurst("cancel-", 100, "2026-11-07T01:05:06Z",
                     "2026-11-07T01:05:05Z");
        audio.run([&] { host.serviceLive(); });
        check(number(request(host, "getLiveState"), "admitted") < 799,
              "stop fixture has pending onsets");
        request(host, "setLiveEnabled", R"({"enabled":false})");
        const auto stoppedCount = audio.runtime.snapshot().liveTriggeredNotes;
        audio.energy = 0;
        for (int i = 0; i < 50; ++i) {
            audio.tick();
            host.serviceLive();
        }
        check(audio.energy == 0 &&
                  audio.runtime.snapshot().liveTriggeredNotes == stoppedCount,
              "stop cancels pending and in-flight notes without stale onsets");
        audio.run([&] {
            return request(host, "setLiveEnabled", R"({"enabled":true})");
        });
        for (int i = 0; i < 50; ++i) {
            audio.tick();
            host.serviceLive();
        }
        check(audio.runtime.snapshot().liveTriggeredNotes == 0,
              "reenable establishes fresh baseline without replay");
        request(host, "setLiveEnabled", R"({"enabled":false})");
        std::cout << "smoothed live host tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
