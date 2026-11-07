#pragma once

#include <atlas/piece_writer.hpp>
#include <atomic>
#include <mutex>
#include <thread>

namespace atlas {
struct PieceJob {
    std::string id, operation, status, error, stage;
    std::size_t processed{};
    std::optional<std::size_t> total;
    std::optional<SavedSession> piece;
};

// Control methods have one caller (the bridge). The worker owns its SQLite
// connections and publishes only counters or one immutable result.
class PieceJobs {
  public:
    using Work = std::function<SavedSession(const PieceProgress &)>;
    ~PieceJobs();
    PieceJob start(std::string id, std::string operation, std::string key,
                   Work work);
    std::optional<PieceJob> get(std::string_view id) const;
    PieceJob cancel(std::string_view id);

  private:
    mutable std::mutex mutex_;
    std::optional<PieceJob> current_;
    std::string key_;
    std::atomic_bool cancelled_{};
    std::thread worker_;
};
} // namespace atlas
