#include <atlas/collection.hpp>
#include <atlas/storage.hpp>
#include <atlas/ui_host.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Temporary {
    fs::path path =
        fs::temp_directory_path() /
        ("atlas-piece-host-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { fs::create_directory(path); }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
// Independent SQLite JSON parsing; this connection never accesses a library.
class Json {
  public:
    Json() {
        check(sqlite3_open(":memory:", &db_) == SQLITE_OK, "JSON parser open");
    }
    ~Json() { sqlite3_close(db_); }
    std::string at(const std::string &text, const char *path) const {
        sqlite3_stmt *raw{};
        check(sqlite3_prepare_v2(db_, "SELECT json_extract(?1,?2)", -1, &raw,
                                 nullptr) == SQLITE_OK,
              "JSON prepare");
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(
            raw, sqlite3_finalize);
        check(sqlite3_bind_text(raw, 1, text.c_str(), -1, SQLITE_TRANSIENT) ==
                      SQLITE_OK &&
                  sqlite3_bind_text(raw, 2, path, -1, SQLITE_TRANSIENT) ==
                      SQLITE_OK &&
                  sqlite3_step(raw) == SQLITE_ROW,
              "Invalid host JSON");
        const auto *value = sqlite3_column_text(raw, 0);
        return value ? reinterpret_cast<const char *>(value) : "";
    }

  private:
    sqlite3 *db_{};
};
class Client {
  public:
    Client(atlas::UiHost &host, const Json &json) : host_(host), json_(json) {}
    std::string call(const std::string &method, const std::string &args = "{}",
                     bool success = true) {
        const auto reply =
            host_.request("{\"protocolVersion\":1,\"requestId\":\"piece-test-" +
                          std::to_string(++sequence_) + "\",\"method\":\"" +
                          method + "\",\"args\":" + args + "}");
        check(json_.at(reply, "$.ok") == (success ? "1" : "0"), reply);
        return reply;
    }
    std::string await(const std::string &id) {
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        for (;;) {
            auto reply = call("getPieceJob", "{\"jobId\":\"" + id + "\"}");
            check(json_.at(reply, "$.result.id") == id,
                  "Polling changed job identity");
            const auto processed =
                std::stoull(json_.at(reply, "$.result.processed"));
            const auto total = json_.at(reply, "$.result.total");
            check(total.empty() || processed <= std::stoull(total),
                  "Unbounded job progress");
            if (json_.at(reply, "$.result.status") != "running")
                return reply;
            check(std::chrono::steady_clock::now() < deadline,
                  "Piece job timed out");
            std::this_thread::sleep_for(1ms);
        }
    }

  private:
    atlas::UiHost &host_;
    const Json &json_;
    unsigned sequence_{};
};
void run(const fs::path &bundled) {
    Temporary temporary;
    const auto managed = temporary.path / "managed.sqlite3";
    {
        atlas::Collector collector(managed);
        collector.collect(
            "birdweather", "2026-11-07T01:01:00Z",
            [](const atlas::CollectionRequest &) {
                return R"({"data":{"detections":{"nodes":[{"id":"synthetic-one","timestamp":"2026-11-07T00:30:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"}],"pageInfo":{"hasNextPage":false,"endCursor":"end"}}}})";
            });
    }
    atlas::UiHostStorage storage;
    storage.managedLibrary = managed;
    atlas::UiHost host(managed, nullptr, nullptr, storage);
    Json json;
    Client client(host, json);
    check(client.call("getCapabilities").find("startPieceFreeze") !=
              std::string::npos,
          "Managed piece capability absent");
    check(json.at(client.call("getPieceJob"), "$.result").empty(),
          "Unexpected initial job");
    const auto invalidInterval = client.call(
        "startPieceFreeze",
        R"({"name":"Invalid","startUtc":"2026-11-07T01:00:00Z","endUtc":"2026-11-07T00:00:00Z","requestId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})");
    check(json.at(client.await(json.at(invalidInterval, "$.result.id")),
                  "$.result.status") == "failed",
          "Invalid interval did not fail visibly");
    client.call(
        "startPieceFreeze",
        R"({"name":"Invalid","startUtc":"2026-11-07T00:00:00Z","endUtc":"2026-11-07T01:00:00Z","requestId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","unexpected":true})",
        false);
    client.call("getPieceJob", R"({"unexpected":true})", false);
    const std::string freezeArgs =
        R"({"name":"Synthetic hour","startUtc":"2026-11-07T00:00:00Z","endUtc":"2026-11-07T01:00:00Z","sourceId":"birdweather","requestId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
    const auto first = client.call("startPieceFreeze", freezeArgs);
    const auto frozen = client.await(json.at(first, "$.result.id"));
    check(json.at(frozen, "$.result.status") == "completed", frozen);
    check(json.at(frozen, "$.result.piece.eventCount") == "1",
          "Freeze did not copy selected event");
    const auto frozenId = json.at(frozen, "$.result.piece.id");
    const auto retried = client.await(
        json.at(client.call("startPieceFreeze", freezeArgs), "$.result.id"));
    check(json.at(retried, "$.result.piece.id") == frozenId,
          "Freeze retry duplicated saved piece");
    auto opened =
        client.call("openManagedPiece", "{\"sessionId\":\"" + frozenId + "\"}");
    check(json.at(opened, "$.result.libraryChanged") == "0",
          "Same-library open requested replacement");
    check(json.at(opened, "$.result.session.id") == frozenId,
          "Wrong freeze open");
    client.call("openManagedPiece", R"({"sessionId":"missing"})", false);
    const auto importedId = std::string("d774207b-342c-49bd-a328-5649f9de84ce");
    host.setLibrary(bundled);
    const auto importArgs = "{\"sessionId\":\"" + importedId + "\"}";
    client.call("startPieceImport",
                R"({"sessionId":"latest","unexpected":true})", false);
    const auto imported = client.await(
        json.at(client.call("startPieceImport", importArgs), "$.result.id"));
    check(json.at(imported, "$.result.status") == "completed", imported);
    check(json.at(imported, "$.result.piece.id") == importedId &&
              json.at(imported, "$.result.piece.eventCount") == "224",
          "Bundled archive import identity/count changed");
    const auto retryImport = client.await(
        json.at(client.call("startPieceImport", importArgs), "$.result.id"));
    check(json.at(retryImport, "$.result.piece.id") == importedId,
          "Import retry identity changed");
    check(json.at(client.call("getPieceJob"), "$.result.id") ==
              json.at(retryImport, "$.result.id"),
          "Latest-job recovery failed");
    opened = client.call("openManagedPiece", importArgs);
    check(json.at(opened, "$.result.libraryChanged") == "1",
          "External-to-managed open did not signal replacement");
    const auto prepared = client.call(
        "prepareReplay", "{\"sessionId\":\"" + importedId +
                             "\",\"basis\":\"observed\",\"speedNumerator\":"
                             "\"60\",\"speedDenominator\":\"1\"}");
    check(json.at(prepared, "$.result.eventCount") == "224",
          "Imported piece cannot prepare normally");
    atlas::ReadOnlyLibrary reopened(managed);
    check(reopened.sessions().size() == 2,
          "Retries duplicated pieces in managed library");
    atlas::UiHost unavailable({}, nullptr, nullptr, {});
    Client noManaged(unavailable, json);
    noManaged.call("startPieceFreeze", freezeArgs, false);
    noManaged.call("startPieceImport", importArgs, false);
    noManaged.call("openManagedPiece", importArgs, false);
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Pass bundled ATN library path");
        run(argv[1]);
        std::cout << "piece host tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
