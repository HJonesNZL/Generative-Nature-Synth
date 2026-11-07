#pragma once
#include <atlas/storage.hpp>
#include <functional>
namespace atlas {
// Worker-only bounded import. False cancels before the atomic write. One
// durable staging file per managed destination retains committed ordinal chunks
// across restart/cancellation. Resume revalidates source integrity and compares
// retained rows before copying missing rows. No partial piece enters the saved
// library. Capacity: 50k events, 64 MiB payload, 80 MiB stage, 512 MiB
// library+stage budget. Only originally v1-sealed metadata is importable;
// legacy pieces remain replayable read-only. If post-commit checkpoint cleanup
// fails, progress receives saved_checkpoint_retained and the successful save is
// returned; cancellation applies only before publication.
using PieceImportProgress =
    std::function<bool(std::string_view, std::size_t, std::size_t)>;
SavedSession
importSavedPiece(const std::filesystem::path &attachedReadOnlySource,
                 const std::filesystem::path &managedDestination,
                 std::string_view sessionId,
                 const PieceImportProgress &progress = {});
} // namespace atlas
