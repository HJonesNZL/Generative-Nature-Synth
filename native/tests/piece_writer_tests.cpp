#include <atlas/collection.hpp>
#include <atlas/piece_writer.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed{};
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "operation should fail");
}
} // namespace
int main() {
    const auto dir =
        std::filesystem::temp_directory_path() /
        ("atlas-freeze-test-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{dir};
    try {
        const auto path = dir / "live.sqlite";
        atlas::Collector collector(path);
        atlas::FreezePieceRequest request;
        request.id = "empty-history";
        request.name = "Empty historical window";
        request.createdAt = "2026-11-07T02:00:00Z";
        request.observedFrom = "2026-11-07T00:00:00Z";
        request.observedTo = "2026-11-07T01:00:00Z";
        const auto result = atlas::freezeObservedPiece(path, request);
        require(result.eventCount == 0, "empty freeze count");
        atlas::ReadOnlyLibrary reader(path);
        atlas::SavedSession verified;
        (void)reader.load(result.id, atlas::TimeBasis::observed, {}, &verified);
        require(verified.metadataIntegrity == "sha256-envelope-v1-verified",
                "sealed empty window");
        require(verified.observedBounds->first ==
                    atlas::parseUtc(request.observedFrom),
                "preserve requested start");
        require(verified.observedBounds->second ==
                    atlas::parseUtc(request.observedTo),
                "preserve requested end");
        request.createdAt = "2026-11-07T03:00:00Z";
        require(atlas::freezeObservedPiece(path, request).id == result.id,
                "retry ignores host clock");
        request.speciesId = "species-different";
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        request.speciesId.reset();
        request.seed = 7;
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        request.seed = 0;
        request.id = "cancelled";
        rejects([&] {
            atlas::freezeObservedPiece(
                path, request,
                [](auto stage, auto, auto) { return stage != "committing"; });
        });
        rejects([&] { reader.session(request.id); });
        request.id = "material";
        collector.collect(
            "birdweather", "2026-11-07T01:01:00Z",
            [](const atlas::CollectionRequest &) {
                return R"({"data":{"detections":{"nodes":[{"id":"inside","timestamp":"2026-11-07T00:59:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"},{"id":"upper","timestamp":"2026-11-07T01:00:00Z","coords":{"lat":-41,"lon":174},"species":{"id":42,"scientificName":"Synthetic species","commonName":"Synthetic bird"},"station":{"id":"fixture"},"confidence":0.87,"score":9.6,"probability":0.99,"certainty":"almost_certain"}],"pageInfo":{"hasNextPage":false,"endCursor":"end"}}}})";
            });
        const auto material = atlas::freezeObservedPiece(path, request);
        require(material.eventCount == 1,
                "half-open end excludes exact boundary");
        (void)reader.load(material.id, atlas::TimeBasis::observed);
        require(reader.session("empty-history").eventCount == 0,
                "earlier piece remains immutable");
        request.observedTo = request.observedFrom;
        rejects([&] { atlas::freezeObservedPiece(path, request); });
        std::cout << "piece writer tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
