#include "piece_jobs.hpp"

namespace atlas {
PieceJobs::~PieceJobs() {
    cancelled_ = true;
    if (worker_.joinable())
        worker_.join();
}
PieceJob PieceJobs::start(std::string id, std::string operation,
                          std::string key, Work work) {
    {
        std::lock_guard lock(mutex_);
        if (current_ && current_->status == "running") {
            if (key_ == key && current_->operation == operation)
                return *current_;
            throw std::invalid_argument(
                "Another saved piece operation is running.");
        }
    }
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    cancelled_ = false;
    current_ = PieceJob{std::move(id),
                        std::move(operation),
                        "running",
                        {},
                        "starting",
                        0,
                        {},
                        {}};
    key_ = std::move(key);
    try {
        worker_ = std::thread([this, work = std::move(work)] {
            try {
                auto saved = work([this](std::string_view stage,
                                         std::size_t done, std::size_t total) {
                    if (cancelled_)
                        return false;
                    std::lock_guard progressLock(mutex_);
                    current_->stage = stage;
                    current_->processed = done;
                    current_->total =
                        total ? std::optional{total} : std::nullopt;
                    return !cancelled_.load();
                });
                std::lock_guard resultLock(mutex_);
                current_->processed = saved.eventCount;
                current_->total = saved.eventCount;
                current_->piece = std::move(saved);
                current_->status = "completed";
                current_->stage = "completed";
            } catch (const PieceCancelled &) {
                std::lock_guard resultLock(mutex_);
                current_->status = "cancelled";
            } catch (const std::exception &error) {
                std::lock_guard resultLock(mutex_);
                current_->status = "failed";
                current_->error = error.what();
            }
        });
    } catch (...) {
        current_.reset();
        throw;
    }
    return *current_;
}
std::optional<PieceJob> PieceJobs::get(std::string_view id) const {
    std::lock_guard lock(mutex_);
    if (current_ && (id.empty() || current_->id == id))
        return current_;
    return {};
}
PieceJob PieceJobs::cancel(std::string_view id) {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->id != id)
        throw std::invalid_argument("Unknown saved piece job.");
    if (current_->status == "running")
        cancelled_ = true;
    return *current_;
}
} // namespace atlas
