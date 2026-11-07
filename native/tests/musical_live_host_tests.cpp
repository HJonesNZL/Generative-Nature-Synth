#include <algorithm>
#include <array>
#include <atlas/collection.hpp>
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
using namespace atlas;
namespace {
void check(bool v, const std::string &message) {
    if (!v)
        throw std::runtime_error(message);
}
struct Temporary {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("atlas-musical-synthetic-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directory(path); }
    ~Temporary() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
struct Driver : OutputDriver {
    AudioDeviceCatalog audioDevices() override {
        AudioDeviceCatalog c;
        c.available = true;
        c.devices.push_back({"fixture",
                             "Synthetic fixture output",
                             "Fixture",
                             true,
                             2,
                             {48000},
                             {480},
                             480});
        return c;
    }
    std::string start(PlaybackRuntime &r, std::uint32_t rate) override {
        r.deviceStarted(rate, 480, 2);
        return {};
    }
    void stop() noexcept override {}
};
struct Audio {
    PlaybackRuntime runtime{std::make_unique<Driver>()};
    std::vector<float> pcm;
    double energy{}, stereoDifference{}, peak{}, meterPeak{};
    void tick() {
        std::array<float, 480> l{}, r{};
        float *out[]{l.data(), r.data()};
        runtime.process(out, 2, 480);
        const auto meter = runtime.snapshot();
        meterPeak =
            std::max({meterPeak, meter.outputPeakLeft, meter.outputPeakRight});
        for (std::size_t i = 0; i < l.size(); ++i) {
            check(std::isfinite(l[i]) && std::isfinite(r[i]), "finite PCM");
            energy += std::abs(l[i]) + std::abs(r[i]);
            stereoDifference += std::abs(l[i] - r[i]);
            peak = std::max(peak, static_cast<double>(std::max(
                                      std::abs(l[i]), std::abs(r[i]))));
            pcm.push_back(l[i]);
            pcm.push_back(r[i]);
        }
    }
    template <class F> auto run(F f) {
        auto job = std::async(std::launch::async, f);
        while (job.wait_for(std::chrono::milliseconds(10)) !=
               std::future_status::ready)
            tick();
        return job.get();
    }
    void clear() {
        pcm.clear();
        energy = stereoDifference = peak = meterPeak = 0;
    }
};
std::string request(UiHost &h, const std::string &method,
                    const std::string &args = "{}") {
    auto response = h.request(
        "{\"protocolVersion\":1,\"requestId\":\"synthetic\",\"method\":\"" +
        method + "\",\"args\":" + args + "}");
    check(response.find("\"ok\":true") != std::string::npos, response);
    return response;
}
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
std::string node(const std::string &id, int species,
                 const std::string &at = "2026-11-07T01:06:30Z") {
    return "{\"id\":\"" + id + "\",\"timestamp\":\"" + at +
           "\",\"coords\":{\"lat\":-41,"
           "\"lon\":" +
           std::to_string(std::array<int, 5>{
               -74, 174, 10, 55,
               -140}[static_cast<std::size_t>(species - 42) % 5]) +
           "},\"species\":{\"id\":" + std::to_string(species) +
           ",\"scientificName\":\"Synthetic taxon " + std::to_string(species) +
           "\",\"commonName\":\"Synthetic audition bird " +
           std::to_string(species) +
           "\"},\"station\":{\"id\":\"synthetic-fixture\"}}";
}
auto fetch(std::string nodes) {
    return [nodes](const CollectionRequest &) {
        return "{\"data\":{\"detections\":{\"nodes\":[" + nodes +
               "],\"pageInfo\":{\"hasNextPage\":false,\"endCursor\":null}}}}";
    };
}
void wav(const std::filesystem::path &path, const std::vector<float> &pcm,
         double monitoringGain = 1.0) {
    std::ofstream f(path, std::ios::binary);
    check(bool(f), "open audition");
    const auto word = [&](std::uint32_t v, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i)
            f.put(static_cast<char>((v >> (8 * i)) & 255));
    };
    f.write("RIFF", 4);
    word(36 + static_cast<std::uint32_t>(pcm.size() * 2), 4);
    f.write("WAVEfmt ", 8);
    word(16, 4);
    word(1, 2);
    word(2, 2);
    word(48000, 4);
    word(192000, 4);
    word(4, 2);
    word(16, 2);
    f.write("data", 4);
    word(static_cast<std::uint32_t>(pcm.size() * 2), 4);
    for (float v : pcm)
        word(
            static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(
                std::clamp(static_cast<double>(v) * monitoringGain, -1.0, 1.0) *
                32767))),
            2);
    check(bool(f), "write audition");
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::filesystem::path auditions;
        if (argc == 3 && std::string(argv[1]) == "--auditions") {
            auditions = argv[2];
            std::filesystem::create_directories(auditions / "listening");
        }
        Temporary temp;
        auto db = temp.path / "live.sqlite3";
        Collector collector(db);
        collector.collect("birdweather", "2026-11-07T01:05:00Z",
                          fetch(node("baseline", 42, "2026-11-07T01:00:00Z")));
        std::array<VoiceSpecies, 4> species{
            {{"birdweather:species:42", "Synthetic whistle"},
             {"birdweather:species:43", "Synthetic reed"},
             {"birdweather:species:44", "Synthetic bell"},
             {"birdweather:species:45", "Synthetic pulse"}}};
        VoiceStore voices(temp.path / "voices.sqlite3", species);
        for (std::size_t i = 0; i < species.size(); ++i) {
            const auto id = "species:" + species[i].speciesId;
            voices.setParameter(id, "brightness", double(i) / 3,
                                VoiceGesture::begin);
            voices.setParameter(id, "brightness", double(i) / 3,
                                VoiceGesture::end);
        }
        std::vector<VoiceProfile> profiles;
        const std::array<VoiceCharacter, 4> characters{
            VoiceCharacter::whistle, VoiceCharacter::reed, VoiceCharacter::bell,
            VoiceCharacter::pulse};
        for (std::size_t i = 0; i < species.size(); ++i) {
            auto settings =
                voices.getVoice("species:" + species[i].speciesId).settings;
            settings.character = characters[i];
            profiles.push_back({species[i].speciesId, settings});
        }
        check(voices.commitProfiles(voices.stageProfiles(profiles)),
              "stage synthetic timbre contrasts");
        Audio audio;
        UiHost host(db, &audio.runtime, &voices, {{}, {}, {}, db});
        request(host, "setLiveSettings",
                "{\"mode\":\"musical\",\"density\":4}");
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        audio.run([&] {
            return request(
                host, "setLiveSettings",
                R"({"mode":"musical","density":4,"spatial":{"enabled":true,"amount":0.35,"regions":[{"id":"synthetic-pacific","name":"Synthetic Pacific","south":-55,"north":-30,"west":150,"east":180,"pan":-0.65,"gainDb":-1,"send":0.35,"echoMix":0.3,"delayMs":180,"feedback":0.25,"roomSeconds":1.2,"tone":0.5,"positionPan":false}]}})");
        });
        collector.collect("birdweather", "2026-11-07T01:07:00Z",
                          fetch(node("sparse", 42)));
        audio.clear();
        audio.run([&] { host.serviceLive(); });
        for (int i = 0; i < 180; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            audio.run([&] { host.serviceLive(); });
            audio.tick();
        }
        check(audio.energy > .01 && audio.peak > 0 && audio.meterPeak > 0,
              "sparse callback PCM and meter");
        check(number(request(host, "getLiveState"), "admitted") == 1,
              "sparse admitted once");
        if (!auditions.empty()) {
            wav(auditions / "sparse.wav", audio.pcm);
            wav(auditions / "listening" / "sparse.wav", audio.pcm,
                std::pow(10.0, 30.0 / 20.0));
        }
        std::array<std::string, 2> pages;
        for (int i = 0; i < 500; ++i) {
            auto &nodes = pages[static_cast<std::size_t>(i / 250)];
            if (i % 250)
                nodes += ",";
            nodes +=
                node("dense-" + std::to_string(i),
                     i < 480 ? 42 : 43 + (i - 480) / 5, "2026-11-07T01:07:30Z");
        }
        std::size_t page = 0;
        collector.collect(
            "birdweather", "2026-11-07T01:08:00Z",
            [&](const CollectionRequest &) {
                check(page < pages.size(), "bounded synthetic pagination");
                const bool more = page == 0;
                return "{\"data\":{\"detections\":{\"nodes\":[" +
                       pages[page++] + "],\"pageInfo\":{\"hasNextPage\":" +
                       (more ? "true" : "false") + ",\"endCursor\":" +
                       (more ? "\"synthetic-page-two\"" : "null") + "}}}}";
            });
        audio.clear();
        audio.run([&] { host.serviceLive(); });
        for (int i = 0; i < 240; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            audio.run([&] { host.serviceLive(); });
            audio.tick();
        }
        const auto state = request(host, "getLiveState");
        const auto admitted = number(state, "admitted"),
                   grouped = number(state, "grouped"),
                   dropped = number(state, "dropped");
        check(number(state, "received") == 501,
              "500 genuine synthetic incoming records accounted");
        check(admitted >= 3 && admitted <= 9, "bounded phrase onsets");
        check(admitted + grouped + dropped == 501,
              "every incoming record has outcome");
        check(audio.energy > .01, "dense actual callback PCM");
        check(audio.stereoDifference > .01,
              "explicit region pan affects stereo callback PCM");
        std::cout << "Synthetic burst: admitted=" << admitted
                  << " grouped=" << grouped << " dropped=" << dropped
                  << " peak=" << audio.peak
                  << " stereo difference=" << audio.stereoDifference << '\n';
        bool sawSounded = false, sawGrouped = false, sawSkipped = false;
        for (int i = 0; i < 500; ++i) {
            const auto trace = request(host, "getLiveOutcome",
                                       "{\"eventId\":\"birdweather:dense-" +
                                           std::to_string(i) + "\"}");
            sawSounded = sawSounded || trace.find("\"status\":\"sounded\"") !=
                                           std::string::npos;
            sawSkipped = sawSkipped || trace.find("\"status\":\"skipped\"") !=
                                           std::string::npos;
            if (trace.find("\"status\":\"grouped\"") != std::string::npos) {
                sawGrouped = true;
                check(trace.find("\"representativeId\":\"birdweather:") !=
                          std::string::npos,
                      "grouped trace retains genuine representative identity");
            }
        }
        check(sawSounded && sawGrouped && sawSkipped,
              "sounded, grouped and skipped outcomes inspectable");
        const auto outcome = request(host, "getLiveOutcome",
                                     "{\"eventId\":\"birdweather:dense-499\"}");
        check(outcome.find("dense-499") != std::string::npos,
              "finite per-event trace identity");
        if (!auditions.empty()) {
            wav(auditions / "dense.wav", audio.pcm);
            wav(auditions / "listening" / "dense.wav", audio.pcm,
                std::pow(10.0, 30.0 / 20.0));
            std::ofstream notes(auditions / "README.txt");
            notes << "Synthetic fixture animal records, not real animal "
                     "observations.\n"
                     "sparse.wav and dense.wav: original 48 kHz stereo "
                     "callback PCM.\n"
                     "listening/sparse.wav and listening/dense.wav: the same "
                     "float callback captures\n"
                     "with one fixed +30 dB monitoring gain applied before "
                     "PCM16 quantization.\n"
                     "No app/engine gain changes or audio-device playback were "
                     "performed.\n";
        }
        request(host, "setLiveEnabled", "{\"enabled\":false}");
        audio.clear();
        for (int i = 0; i < 10; ++i) {
            audio.run([&] { host.serviceLive(); });
            audio.tick();
        }
        check(audio.energy == 0, "stop clears phrase and all tails");
        check(request(host, "getLiveOutcome",
                      "{\"eventId\":\"birdweather:dense-499\"}")
                      .find("dense-499") != std::string::npos,
              "last trace remains inspectable after stop");
        const auto traceFor = [&](const std::string &id) {
            return request(host, "getLiveOutcome",
                           "{\"eventId\":\"birdweather:" + id + "\"}");
        };
        const auto settle = [&] {
            for (int i = 0; i < 100; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                audio.run([&] { host.serviceLive(); });
                audio.tick();
            }
        };
        const auto collectPhrase = [&](const std::string &prefix,
                                       const std::string &now,
                                       const std::string &observed) {
            std::string records;
            for (int i = 0; i < 4; ++i) {
                if (i)
                    records += ",";
                records += node(prefix + std::to_string(i), 42 + i, observed);
            }
            collector.collect("birdweather", now, fetch(records));
            audio.run([&] { host.serviceLive(); });
        };
        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        collectPhrase("cancel-", "2026-11-07T01:09:00Z",
                      "2026-11-07T01:08:30Z");
        std::vector<std::string> cancelled;
        for (int i = 0; i < 4; ++i) {
            const auto id = "cancel-" + std::to_string(i);
            if (traceFor(id).find("\"status\":\"queued\"") != std::string::npos)
                cancelled.push_back(id);
        }
        check(cancelled.size() >= 2,
              "mid-phrase stop fixture has genuine future onsets");
        const auto soundedBeforeStop =
            number(request(host, "getLiveState"), "admitted");
        request(host, "setLiveEnabled", "{\"enabled\":false}");
        audio.clear();
        settle();
        check(audio.energy == 0,
              "mid-phrase stop silences callback past all queued deadlines");
        check(number(request(host, "getLiveState"), "admitted") ==
                  soundedBeforeStop,
              "cancelled future notes never increment sounded count");
        for (const auto &id : cancelled)
            check(traceFor(id).find("\"status\":\"skipped\"") !=
                      std::string::npos,
                  "queued representative becomes skipped on stop");

        audio.run([&] {
            return request(host, "setLiveEnabled", "{\"enabled\":true}");
        });
        collectPhrase("revise-", "2026-11-07T01:10:00Z",
                      "2026-11-07T01:09:30Z");
        std::vector<int> pending;
        for (int i = 0; i < 4; ++i)
            if (traceFor("revise-" + std::to_string(i))
                    .find("\"status\":\"queued\"") != std::string::npos)
                pending.push_back(i);
        check(pending.size() >= 2,
              "revision fixture has two representatives awaiting onset");
        const auto correctedId = "revise-" + std::to_string(pending[0]);
        auto correction =
            node(correctedId, 42 + pending[0], "2026-11-07T01:09:30Z");
        const auto name = correction.find("Synthetic taxon");
        check(name != std::string::npos, "synthetic correction field exists");
        correction.replace(name, std::string("Synthetic taxon").size(),
                           "Corrected synthetic taxon");
        const auto correctionResult = collector.collect(
            "birdweather", "2026-11-07T01:10:10Z", fetch(correction));
        check(correctionResult.corrected == 1,
              "collector commits genuine superseding correction");
        const auto withdrawnId = "revise-" + std::to_string(pending[1]);
        sqlite3 *withdrawal{};
        check(sqlite3_open(db.c_str(), &withdrawal) == SQLITE_OK,
              "open disposable withdrawal connection");
        const auto erase =
            "DELETE FROM events WHERE id='birdweather:" + withdrawnId + "'";
        const auto erased =
            sqlite3_exec(withdrawal, erase.c_str(), nullptr, nullptr, nullptr);
        const auto erasedRows = sqlite3_changes(withdrawal);
        sqlite3_close(withdrawal);
        check(erased == SQLITE_OK && erasedRows == 1,
              "withdraw queued representative in disposable store");
        settle();
        check(traceFor(correctedId).find("\"status\":\"skipped\"") !=
                  std::string::npos,
              "corrected representative is rechecked and never sounded");
        check(traceFor(withdrawnId).find("\"outcome\":null") !=
                  std::string::npos,
              "withdrawn representative is not exposed as sounded");
        check(number(request(host, "getLiveState"), "admitted") == 2,
              "only two unchanged representatives sounded after correction and "
              "withdrawal");
        request(host, "setLiveEnabled", "{\"enabled\":false}");
        std::cout << "Synthetic musical Live callback integration passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
