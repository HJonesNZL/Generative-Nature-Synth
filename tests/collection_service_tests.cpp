#include "juce_collection.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <juce_core/juce_core.h>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>

using atlas::standalone::CollectionService;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("atlas-collection-service-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
juce::var state(const CollectionService &service) {
    const auto value = juce::JSON::parse(juce::String(service.stateJson()));
    require(value.isObject(), "Collection status is not JSON");
    return value;
}
juce::var source(const CollectionService &service, int index) {
    return state(service)["sources"][index];
}
template <class Predicate>
void waitFor(Predicate predicate, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(message);
        std::this_thread::sleep_for(5ms);
    }
}
std::string birdResponse() {
    return R"({"data":{"detections":{"nodes":[{"id":"synthetic-bird","timestamp":"2020-01-01T00:00:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"synthetic-station"},"confidence":0.87}],"pageInfo":{"hasNextPage":false,"endCursor":null}}}})";
}
std::int64_t databaseInteger(const std::filesystem::path &path,
                             const char *sql) {
    sqlite3 *db{};
    require(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY,
                            nullptr) == SQLITE_OK,
            "Open disposable service result");
    sqlite3_stmt *query{};
    if (sqlite3_prepare_v2(db, sql, -1, &query, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("Prepare disposable service count");
    }
    const auto step = sqlite3_step(query);
    const auto count = sqlite3_column_int64(query, 0);
    sqlite3_finalize(query);
    sqlite3_close(db);
    require(step == SQLITE_ROW, "Read disposable service count");
    return count;
}
std::int64_t eventCount(const std::filesystem::path &path) {
    return databaseInteger(path, "SELECT count(*) FROM events");
}
void scheduling(const std::filesystem::path &path) {
    std::atomic<int> calls{};
    const auto caller = std::this_thread::get_id();
    {
        CollectionService service(
            path, [&](const atlas::CollectionRequest &request) {
                require(std::this_thread::get_id() != caller,
                        "Collector ran on app caller thread");
                ++calls;
                if (request.url == "https://app.birdweather.com/graphql") {
                    require(!request.body.empty(),
                            "Bird GraphQL request lost POST body");
                    return birdResponse();
                }
                require(request.url ==
                            "https://map.sustainableoceansociety.co.nz/public/"
                            "bootstrap.json",
                        "Unexpected source endpoint");
                throw atlas::CollectionFetchError("http_429", 7200);
            });
        require(service.databasePath() == path,
                "Managed database identity lost");
        require(!static_cast<bool>(state(service)["collecting"]) &&
                    calls.load() == 0,
                "Service started network before explicit enable");
        require(source(service, 0)["lastSuccessUtc"].isVoid() &&
                    source(service, 0)["lastObservationUtc"].isVoid(),
                "Unpolled source fabricated freshness");
        bool rejected{};
        try {
            service.configure({"unknown"});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected && calls.load() == 0,
                "Unknown source selection mutated scheduling");
        rejected = false;
        try {
            service.configure({"birdweather", "birdweather"});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "Duplicate source selection accepted");
        rejected = false;
        try { service.configure({"birdweather", "birdweather_world"}); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected && calls.load() == 0, "Conflicting bird coverage scheduled twice");
        require(!service.session() && state(service)["session"].isVoid(),
                "Stopped service exposed active session");
        service.configure({"birdweather", "nzsharks"});
        const auto session = service.session();
        require(session && !session->id.empty() && !session->observedFromUtc.empty() &&
                session->baselineRevision == 0 && !session->storeId.empty(),
                "Collect did not capture exact initial frontier");
        service.configure({"birdweather", "nzsharks"});
        require(service.session()->id == session->id,
                "Active source edit renewed session");
        waitFor(
            [&] {
                return source(service, 1)["error"].toString() == "http_429" &&
                       !static_cast<bool>(source(service, 1)["polling"]);
            },
            "Sources did not complete independent polling");
        const auto bird = source(service, 0), shark = source(service, 1);
        require(bird["records"].toString() == "1" &&
                    !bird["lastSuccessUtc"].isVoid(),
                "Successful source did not retain genuine records/success");
        require(bird["lastObservationUtc"].toString().startsWith("2020-01-01"),
                "Observation freshness replaced with polling time");
        require(static_cast<bool>(bird["complete"]) && bird["error"].isVoid(),
                "Successful complete source lost status");
        require(shark["lastSuccessUtc"].isVoid() &&
                    shark["lastObservationUtc"].isVoid(),
                "Failed source fabricated success/freshness");
        require(static_cast<int>(shark["nextPollSeconds"]) >= 7198 &&
                    static_cast<int>(shark["nextPollSeconds"]) <= 7200,
                "Provider Retry-After was not respected");
        require(static_cast<int>(bird["nextPollSeconds"]) >= 58 &&
                    static_cast<int>(bird["nextPollSeconds"]) <= 60,
                "Bird minimum interval lost");
        require(calls.load() == 2,
                "Unexpected requests in first collection cycle");
        service.configure({});
        require(!service.session(), "Stopped service retained active boundary");
        service.configure({"birdweather", "nzsharks"});
        require(service.session() && service.session()->id != session->id &&
                service.session()->baselineRevision >= 1,
                "Collect restart reused previous session frontier");
        sqlite3 *resetDb{};
        require(sqlite3_open_v2(path.string().c_str(), &resetDb,
                               SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK,
                "Open disposable privacy reset");
        require(sqlite3_exec(resetDb, "UPDATE metadata SET value='privacy-reset' "
                                    "WHERE key='store_id'", nullptr, nullptr, nullptr) == SQLITE_OK,
                "Rotate disposable library identity");
        sqlite3_close(resetDb);
        require(!service.verifySession() && !service.session() &&
                !static_cast<bool>(state(service)["collecting"]) &&
                state(service)["sessionError"].toString().contains("privacy"),
                "Privacy reset left collection silently active");
        service.configure({"birdweather", "nzsharks"});
        require(service.session() && service.session()->storeId == "privacy-reset" &&
                state(service)["sessionError"].isVoid(),
                "Explicit Collect restart did not recover fresh privacy frontier");
        std::this_thread::sleep_for(40ms);
        require(calls.load() == 2,
                "Reconfiguration bypassed poll intervals or busy-looped");
        service.configure({});
        require(!static_cast<bool>(state(service)["collecting"]) &&
                    !static_cast<bool>(source(service, 0)["enabled"]),
                "Disable left collector running");
    }
    require(eventCount(path) == 1,
            "Managed collection did not persist the genuine fixture record");
    std::atomic<int> restartedCalls{};
    {
        CollectionService restarted(path,
                                    [&](const atlas::CollectionRequest &) {
                                        ++restartedCalls;
                                        return birdResponse();
                                    });
        const auto cached = source(restarted, 0);
        require(cached["records"].toString() == "1" &&
                    !cached["lastSuccessUtc"].isVoid(),
                "Restart lost durable source status");
        require(!static_cast<bool>(cached["enabled"]) &&
                    !static_cast<bool>(state(restarted)["collecting"]),
                "Restart enabled collection without instruction");
        restarted.configure({"birdweather_world"});
        std::this_thread::sleep_for(40ms);
        require(restartedCalls.load() == 0 && static_cast<int>(source(restarted, 2)["nextPollSeconds"]) > 0,
                "Switching to worldwide after restart bypassed the shared provider interval");
        restarted.configure({"birdweather", "nzsharks"});
        std::this_thread::sleep_for(40ms);
        require(restartedCalls.load() == 0 &&
                    static_cast<int>(source(restarted, 0)["nextPollSeconds"]) >
                        0,
                "Restart bypassed minimum source interval");
        require(static_cast<int>(source(restarted, 1)["nextPollSeconds"]) >=
                    7195,
                "Restart bypassed persisted provider Retry-After");
    }
}
void cancellation(const std::filesystem::path &path) {
    std::mutex mutex;
    std::condition_variable wake;
    bool entered{}, release{};
    {
        CollectionService service(path, [&](const atlas::CollectionRequest &) {
            std::unique_lock lock(mutex);
            entered = true;
            wake.notify_all();
            wake.wait_for(lock, 2s, [&] { return release; });
            return birdResponse();
        });
        service.configure({"birdweather"});
        {
            std::unique_lock lock(mutex);
            require(wake.wait_for(lock, 2s, [&] { return entered; }),
                    "Synthetic fetch did not start");
        }
        service.configure({});
        {
            std::lock_guard lock(mutex);
            release = true;
        }
        wake.notify_all();
        waitFor(
            [&] { return !static_cast<bool>(source(service, 0)["polling"]); },
            "Disabled fetch did not retire");
        require(eventCount(path) == 0, "Disabled fetch response was ingested");
        const auto stopped = source(service, 0);
        require(stopped["lastSuccessUtc"].isVoid(),
                "Cancelled source fabricated success");
        require(stopped["error"].isVoid(),
                "Expected pause became a source fault");
        require(!stopped["lastAttemptUtc"].isVoid(),
                "Pause lost genuine attempt time");
        require(databaseInteger(
                    path, "SELECT "
                          "COALESCE(json_extract(status,'$.consecutive_errors')"
                          ",0) FROM sources WHERE id='birdweather'") == 0,
                "Expected pause incremented persistent source errors");
    }
    std::atomic<int> restartedCalls{};
    CollectionService restarted(path, [&](const atlas::CollectionRequest &) {
        ++restartedCalls;
        return birdResponse();
    });
    require(source(restarted, 0)["error"].isVoid(),
            "Restart exposed a false pause fault");
    restarted.configure({"birdweather"});
    std::this_thread::sleep_for(40ms);
    require(restartedCalls.load() == 0 &&
                static_cast<int>(source(restarted, 0)["nextPollSeconds"]) >= 58,
            "Pause/restart bypassed the source minimum poll interval");
}
void boundedResponse(const std::filesystem::path &path) {
    CollectionService service(path, [](const atlas::CollectionRequest &) {
        return std::string(4U * 1024U * 1024U + 1U, 'x');
    });
    service.configure({"birdweather"});
    waitFor(
        [&] {
            return source(service, 0)["error"].toString() ==
                       "response_too_large" &&
                   !static_cast<bool>(source(service, 0)["polling"]);
        },
        "Oversized response did not fail observably");
    require(eventCount(path) == 0 &&
                static_cast<int>(source(service, 0)["nextPollSeconds"]) >= 58,
            "Failed response was ingested or retried immediately");
}
} // namespace
int main() {
    try {
        Directory directory;
        scheduling(directory.path / "managed.sqlite3");
        cancellation(directory.path / "cancel.sqlite3");
        boundedResponse(directory.path / "bounded.sqlite3");
        std::cout << "collection service tests passed (disposable/injected "
                     "HTTP only)\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
