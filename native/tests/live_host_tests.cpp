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
int main() {
    try {
        Temporary temporary;
        const auto db = temporary.path / "live.sqlite3";
        Collector collector(db);
        collector.collect("birdweather", "2026-11-07T01:05:00Z",
                          fetch(bird("baseline", "2026-11-07T01:00:00Z")));
        const std::array<VoiceSpecies, 1> species{
            {{"birdweather:species:42", "Synthetic bird"}}};
        VoiceStore voices(temporary.path / "voices.sqlite3", species);
        voices.setParameter("species:birdweather:species:42", "gain", 0,
                            VoiceGesture::begin);
        voices.setParameter("species:birdweather:species:42", "gain", 0,
                            VoiceGesture::end);
        Harness audio;
        UiHost host(db, &audio.runtime, &voices, {{}, {}, {}, db});
        contains(request(host, "getCapabilities"), "setLiveEnabled");
        contains(audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        }),
                 "\"enabled\":true");
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"admitted\":0");
        collector.collect("birdweather", "2026-11-07T01:07:00Z",
                          fetch(bird("one", "2026-11-07T01:06:30Z")));
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"admitted\":1");
        audio.energy = 0;
        for (int i = 0; i < 80; ++i)
            audio.tick();
        const auto quiet = audio.energy;
        voices.setParameter("species:birdweather:species:42", "gain", 1,
                            VoiceGesture::begin);
        voices.setParameter("species:birdweather:species:42", "gain", 1,
                            VoiceGesture::end);
        collector.collect("birdweather", "2026-11-07T01:08:00Z",
                          fetch(bird("one", "2026-11-07T01:06:30Z", "175") +
                                "," + bird("old", "2026-11-07T01:00:00Z") +
                                "," + bird("two", "2026-11-07T01:07:30Z")));
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"admitted\":2");
        audio.energy = 0;
        for (int i = 0; i < 80; ++i)
            audio.tick();
        check(audio.energy > .01 && audio.energy > quiet * 100,
              "authored species draft gain changes actual callback PCM");
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"admitted\":2");
        const auto deviceArgs = [&](bool stale) {
            const auto snapshot = audio.runtime.snapshot();
            return "{\"controlRevision\":\"" +
                   std::to_string(audio.runtime.audioDevices().controlRevision +
                                  (stale ? 1 : 0)) +
                   "\",\"preparationId\":\"audio-" +
                   std::to_string(snapshot.preparationId) +
                   "\",\"generation\":\"" +
                   std::to_string(snapshot.generation) +
                   "\",\"configuration\":{\"mode\":\"systemDefault\","
                   "\"deviceId\":null,\"bufferFrames\":128}}";
        };
        const auto stale =
            host.request("{\"protocolVersion\":1,\"requestId\":\"stale\","
                         "\"method\":\"setAudioDevice\",\"args\":" +
                         deviceArgs(true) + "}");
        contains(stale, "stale_generation");
        check(audio.runtime.snapshot().enabled,
              "stale device request preserves active live output");
        contains(request(host, "getLiveState"), "\"enabled\":true");
        request(host, "setAudioDevice", deviceArgs(false));
        check(!audio.runtime.snapshot().enabled,
              "valid device change closes live once");
        contains(request(host, "getLiveState"), "\"status\":\"off\"");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        request(host, "setLiveEnabled", "{\"enabled\":false}");
        audio.energy = 0;
        audio.tick();
        check(audio.energy == 0, "off silences active notes");
        collector.collect("birdweather", "2026-11-07T01:09:00Z",
                          fetch(bird("off", "2026-11-07T01:08:30Z")));
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"admitted\":0");
        sqlite3 *writer{};
        check(sqlite3_open(db.c_str(), &writer) == SQLITE_OK,
              "open disposable privacy writer");
        check(sqlite3_exec(writer,
                           "UPDATE metadata SET value='privacy-reset-fixture' "
                           "WHERE key='store_id'",
                           nullptr, nullptr, nullptr) == SQLITE_OK,
              "rotate disposable privacy identity");
        sqlite3_close(writer);
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"status\":\"error\"");
        check(!audio.runtime.snapshot().enabled,
              "privacy reset immediately silences live");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        request(host, "setAudioEnabled", "{\"enabled\":false}");
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"status\":\"off\"");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        audio.run([&] {
            return request(host, "setTransport", "{\"action\":\"panic\"}");
        });
        audio.run([&] { host.serviceLive(); });
        contains(request(host, "getLiveState"), "\"status\":\"off\"");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        const auto saved = freezeObservedPiece(db, {"fixture-piece",
                                                    "Fixture",
                                                    "2026-11-07T01:10:00Z",
                                                    "2026-11-07T01:00:00Z",
                                                    "2026-11-07T01:10:00Z",
                                                    {},
                                                    {},
                                                    0});
        request(host, "prepareReplay",
                "{\"sessionId\":\"" + saved.id +
                    "\",\"basis\":\"arrival\",\"speedNumerator\":\"1\","
                    "\"speedDenominator\":\"1\"}");
        check(!audio.runtime.snapshot().live, "replay exits live");
        contains(request(host, "getLiveState"), "\"enabled\":false");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        const auto external = temporary.path / "external.sqlite3";
        {
            Collector externalCollector(external);
            externalCollector.collect(
                "birdweather", "2026-11-07T01:05:00Z",
                fetch(bird("external", "2026-11-07T01:00:00Z")));
        }
        host.setLibrary(external);
        check(!audio.runtime.snapshot().enabled,
              "library switch silences live");
        check(request(host, "getCapabilities").find("setLiveEnabled") ==
                  std::string::npos,
              "external libraries cannot arm live");
        contains(request(host, "setLiveEnabled", "{\"enabled\":true}"),
                 "\"status\":\"error\"");
        host.setLibrary(db);
        std::vector<VoiceSpecies> fill;
        for (std::size_t i = 0; i < voiceDefinitionCapacity - 2; ++i)
            fill.push_back({"fixture:fill:" + std::to_string(i), "Fixture"});
        voices.registerSpecies(fill);
        check(voices.voices().size() == voiceDefinitionCapacity,
              "full live catalogue fixture");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        collector.collect(
            "birdweather", "2026-11-07T01:11:00Z",
            fetch(bird("known-full", "2026-11-07T01:10:10Z") + "," +
                  bird("new-species", "2026-11-07T01:10:30Z", "174", "99")));
        audio.run([&] { host.serviceLive(); });
        const auto fullState = request(host, "getLiveState");
        contains(fullState, "\"admitted\":2");
        contains(fullState, "\"enabled\":true");
        check(voices.voices().size() == voiceDefinitionCapacity,
              "unseen live species overflowed editable catalogue");
        audio.energy = 0;
        for (int i = 0; i < 80; ++i)
            audio.tick();
        check(audio.energy > .01,
              "full catalogue still generates callback PCM");
        std::cout << "Live UiHost callback integration checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
