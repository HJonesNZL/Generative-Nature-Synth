#pragma once
#include <atlas/storage.hpp>
#include <functional>
namespace atlas {
struct VerifiedPieceEvent {
    std::int64_t originalSequence{};
    std::string eventJson;
};
struct VerifiedPiece {
    std::string id, name, state, createdAt, origin, optionsJson;
    std::optional<std::string> endedAt, intendedEndAt;
    std::int64_t startSequence{}, endSequence{};
    std::vector<VerifiedPieceEvent> events;
    std::string contentHash, envelopeJson, envelopeHash;
    int envelopeVersion{};
};
struct FreezePieceRequest {
    std::string id, name, createdAt, observedFrom, observedTo;
    std::optional<std::string> sourceId, speciesId;
    std::int64_t seed{};
};
using PieceProgress =
    std::function<bool(std::string_view, std::size_t, std::size_t)>;
class PieceCancelled : public std::runtime_error {
  public:
    PieceCancelled() : std::runtime_error("piece operation cancelled") {}
};
// Worker-only synchronous bounded writes to an existing managed library. All
// changes commit atomically; retries reuse an identical sealed ID or fail.
SavedSession saveVerifiedPiece(const std::filesystem::path &,
                               const VerifiedPiece &,
                               const PieceProgress & = {});
SavedSession freezeObservedPiece(const std::filesystem::path &,
                                 const FreezePieceRequest &,
                                 const PieceProgress & = {});
} // namespace atlas
