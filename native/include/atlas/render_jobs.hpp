#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace atlas {
enum class RenderJobState { running, completed, cancelled, failed };
struct RenderJobSnapshot {
    std::string id;
    RenderJobState state{RenderJobState::running};
    std::uint64_t framesWritten{}, totalFrames{};
    std::uint32_t sampleRate{};
    std::string outputFilename, error;
    bool cancelRequested{};
};
enum class RenderJobErrorCode { invalidArgument, busy, notFound, storage };
class RenderJobError : public std::runtime_error {
  public:
    RenderJobError(RenderJobErrorCode, std::string);
    RenderJobErrorCode code() const noexcept { return code_; }

  private:
    RenderJobErrorCode code_;
};
// Control/UI worker API. One independent offline jthread, one active job,
// sixteen retained terminal snapshots. No method belongs on an audio callback.
class RenderJobs {
  public:
    RenderJobs(std::filesystem::path outputDirectory,
               std::string engineSourceSha256);
    ~RenderJobs();
    RenderJobs(const RenderJobs &) = delete;
    RenderJobs &operator=(const RenderJobs &) = delete;
    // IDs are caller-generated opaque ASCII [A-Za-z0-9_-], 1..64 bytes.
    // File loading, verification, plan compilation and rendering run
    // off-thread.
    RenderJobSnapshot start(const std::filesystem::path &scenePath,
                            std::string expectedSceneHash, std::string jobId);
    RenderJobSnapshot get(std::string_view jobId) const;
    // Requests cancellation. Completion may win at the atomic publish boundary;
    // only a terminal snapshot determines whether a complete artifact exists.
    RenderJobSnapshot cancel(std::string_view jobId);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
