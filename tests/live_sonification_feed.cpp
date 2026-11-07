#include <atlas/collection.hpp>
#include <atlas/core.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string nowUtc() {
    return atlas::formatUtc(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
std::string detection(const std::string &id, const std::string &observed,
                      const std::string &confidence = "0.87",
                      unsigned species = 424242) {
    return "{\"id\":\"" + id + "\",\"timestamp\":\"" + observed +
           R"(","coords":{"lat":-41,"lon":174},"species":{"id":)" + std::to_string(species) +
           R"(,"scientificName":"Fixture species","commonName":"Fixture live bird"},"station":{"id":"fixture-live-station"},"confidence":)" +
           confidence +
           R"(,"score":9.6,"probability":0.99,"certainty":"almost_certain"})";
}
atlas::CollectionResult feedPages(atlas::Collector &collector,
                                  const std::vector<std::string> &pages) {
    std::size_t next{};
    const auto result = collector.collect(
        "birdweather", nowUtc(), [&](const atlas::CollectionRequest &request) {
            require(request.url == "https://app.birdweather.com/graphql" &&
                        !request.body.empty() && next < pages.size(),
                    "Unexpected injected fixture request");
            const auto index = next++;
            const bool more = next < pages.size();
            return R"({"data":{"detections":{"nodes":[)" + pages[index] +
                   R"(],"pageInfo":{"hasNextPage":)" + (more ? "true" : "false") +
                   R"(,"endCursor":)" + (more ? "\"fixture-page-" + std::to_string(next) + "\"" : "null") +
                   "}}}}";
        });
    require(next == pages.size(), "Collector did not consume all fixture pages");
    return result;
}
atlas::CollectionResult feed(atlas::Collector &collector,
                             const std::string &nodes) {
    return feedPages(collector, {nodes});
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2,
                "Usage: atlas-live-collection-fixture NEW_STATE_DIRECTORY");
        const std::filesystem::path state(argv[1]);
        const auto database = state / "live.sqlite3";
        require(!std::filesystem::exists(database) &&
                    !std::filesystem::exists(database.string() + "-wal") &&
                    !std::filesystem::exists(database.string() + "-shm"),
                "Fixture refuses an existing database or SQLite sidecar");
        std::filesystem::create_directories(state);
        atlas::Collector collector(database);
        const auto observedBeforeCollect = nowUtc();
        const auto baseline =
            detection("fixture-baseline", "2020-01-01T00:00:00Z");
        const auto initial = feed(collector, baseline);
        require(initial.inserted == 1 && initial.corrected == 0 &&
                    initial.invalidRecords == 0,
                "Baseline fixture was not inserted exactly once");
        std::cout << "{\"status\":\"ready\",\"events\":1,\"revisions\":1}"
                  << std::endl;
        bool inserted{}, quiet{}, dense{};
        std::string current, command;
        while (std::getline(std::cin, command)) {
            if (command == "quit")
                break;
            if (command == "live") {
                require(!inserted, "Live fixture may be inserted only once");
                current = nowUtc();
                const auto result =
                    feed(collector, detection("fixture-current", current));
                require(result.inserted == 1 && result.corrected == 0 &&
                            result.invalidRecords == 0,
                        "Current fixture was not inserted exactly once");
                inserted = true;
                std::cout
                    << "{\"status\":\"live\",\"events\":2,\"revisions\":2}"
                    << std::endl;
            } else if (command == "duplicates") {
                require(inserted && !quiet,
                        "Duplicate phase requires one admitted live phase");
                const auto duplicate =
                    feed(collector, detection("fixture-current", current));
                require(duplicate.duplicates == 1 && duplicate.inserted == 0 &&
                            duplicate.corrected == 0,
                        "Exact duplicate created a revision");
                const auto correction = feed(
                    collector, detection("fixture-current", current, "0.93"));
                require(correction.corrected == 1 && correction.inserted == 0,
                        "Correction did not retain canonical identity");
                const auto backfill =
                    feed(collector, baseline + ',' +
                                        detection("fixture-backfill",
                                                  observedBeforeCollect, "0.87", 818181));
                require(backfill.duplicates == 1 && backfill.inserted == 1 &&
                            backfill.corrected == 0,
                        "Baseline/backfill canonical counts differ");
                quiet = true;
                std::cout << "{\"status\":\"duplicates\",\"events\":3,"
                             "\"revisions\":4,\"duplicates\":2,\"corrected\":1}"
                          << std::endl;
            } else if (command == "dense") {
                require(quiet && !dense, "Dense phase requires completed sparse duplicate checks");
                const auto observed = nowUtc();
                std::vector<std::string> pages(2);
                for (unsigned i = 0; i < 500; ++i) {
                    auto &nodes = pages[i / 250];
                    if (i % 250) nodes += ',';
                    nodes += detection("fixture-dense-" + std::to_string(i), observed,
                                       "0.87", 424242 + i % 8);
                }
                const auto result = feedPages(collector, pages);
                require(result.inserted == 500 && result.corrected == 0 &&
                            result.invalidRecords == 0,
                        "Dense fixture must commit exactly 500 genuine fixture observations");
                dense = true;
                std::cout << "{\"status\":\"dense\",\"events\":503,"
                             "\"revisions\":504,\"inserted\":500,\"speciesGroups\":8}"
                          << std::endl;
            } else {
                throw std::invalid_argument("Unknown fixture command");
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
