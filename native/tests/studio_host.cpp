#include "atlas_build_identity.hpp"
// Test-only studio protocol host: disposable sidecar, synthetic output, no
// hardware.
#include <algorithm>
#include <array>
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atlas/voices.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
class FakeOutputDriver final : public atlas::OutputDriver {
  public:
    explicit FakeOutputDriver(std::filesystem::path startsPath)
        : startsPath_(std::move(startsPath)) {}
    ~FakeOutputDriver() override { stop(); }
    std::string start(atlas::PlaybackRuntime &runtime,
                      std::uint32_t rate) override {
        stop();
        if (!startsPath_.empty()) {
            std::ofstream counter(startsPath_);
            counter << ++starts_;
            if (!counter)
                throw std::runtime_error("fixture counter write failed");
        }
        stalled_ = false;
        runtime.deviceStarted(rate, 64, 2);
        running_ = true;
        callbacks_ = std::thread([this, &runtime, rate] {
            std::array<float, 64> left{}, right{};
            std::array<float *, 2> outputs{left.data(), right.data()};
            const auto period = std::chrono::duration<double>(64.0 / rate);
            auto next = std::chrono::steady_clock::now();
            while (running_.load()) {
                if (!stalled_.load()) {
                    runtime.process(outputs.data(), 2, 64);
                    float blockPeak{};
                    for (std::size_t frame = 0; frame < left.size(); ++frame) {
                        blockPeak = std::max(blockPeak, std::abs(left[frame]));
                        blockPeak = std::max(blockPeak, std::abs(right[frame]));
                    }
                    // One callback writer; the worker resets only with playback
                    // stopped.
                    if (blockPeak > peak_.load())
                        peak_.store(blockPeak);
                }
                next += std::chrono::duration_cast<
                    std::chrono::steady_clock::duration>(period);
                std::this_thread::sleep_until(next);
            }
        });
        return {};
    }
    void stall() noexcept { stalled_ = true; }
    void resetPeak() noexcept { peak_ = 0; }
    float peak() const noexcept { return peak_.load(); }
    void stop() noexcept override {
        running_ = false;
        if (callbacks_.joinable())
            callbacks_.join();
    }

  private:
    std::filesystem::path startsPath_;
    std::uint64_t starts_{};
    static_assert(std::atomic<float>::is_always_lock_free);
    std::atomic<float> peak_{};
    std::atomic<bool> running_{}, stalled_{};
    std::thread callbacks_;
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 4 || std::string_view(argv[1]) != "ui-host" ||
            std::string_view(argv[2]) != "--db")
            throw std::invalid_argument(
                "fixture usage: ui-host --db DISPOSABLE_LIBRARY");
        const auto *sidecar = std::getenv("ATLAS_VOICE_LIBRARY");
        if (!sidecar || !*sidecar)
            throw std::invalid_argument(
                "ATLAS_VOICE_LIBRARY must name a disposable preset sidecar");
        std::error_code pathError_;
        if (std::filesystem::equivalent(std::filesystem::path(argv[3]),
                                        std::filesystem::path(sidecar),
                                        pathError_) &&
            !pathError_)
            throw std::invalid_argument(
                "fixture library and voice sidecar must be separate");
        const auto *starts = std::getenv("ATLAS_VOICE_OUTPUT_STARTS");
        atlas::VoiceStore voices{std::filesystem::path(sidecar)};
        auto output = std::make_unique<FakeOutputDriver>(
            starts ? std::filesystem::path(starts) : std::filesystem::path{});
        auto *fakeOutput = output.get();
        atlas::PlaybackRuntime playback(std::move(output));
        const auto *scenes = std::getenv("ATLAS_SOUNDSCAPE_DIRECTORY");
        const auto *exports = std::getenv("ATLAS_EXPORT_DIRECTORY");
        atlas::UiHostStorage storage;
        storage.soundscapes =
            scenes && *scenes
                ? std::filesystem::path(scenes)
                : std::filesystem::path(argv[3]).parent_path() / "soundscapes";
        storage.exports =
            exports && *exports
                ? std::filesystem::path(exports)
                : std::filesystem::path(argv[3]).parent_path() / "exports";
        storage.engineSourceSha256 = atlas::nativeSourceSha256;
        atlas::UiHost host(std::filesystem::path(argv[3]), &playback, &voices,
                           std::move(storage));
        std::string request;
        request.reserve(16385);
        for (;;) {
            playback.service();
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const auto ready = poll(&input, 1, 100);
            if (ready < 0)
                throw std::runtime_error("fixture stdin poll failed");
            if (!ready)
                continue;
            std::array<char, 4096> bytes{};
            const auto count = read(STDIN_FILENO, bytes.data(), bytes.size());
            if (count <= 0)
                break;
            for (ssize_t index = 0; index < count; ++index) {
                const auto value = bytes[static_cast<std::size_t>(index)];
                if (value == '\n') {
                    if (request == "fixture:stall") {
                        fakeOutput->stall();
                        std::cout << "{\"fixture\":\"stalled\"}\n"
                                  << std::flush;
                    } else if (request == "fixture:reset-peak") {
                        fakeOutput->resetPeak();
                        std::cout << "{\"fixture\":\"peak-reset\"}\n"
                                  << std::flush;
                    } else if (request == "fixture:peak") {
                        std::cout << "{\"peak\":" << fakeOutput->peak() << "}\n"
                                  << std::flush;
                    } else
                        std::cout << host.request(request) << '\n'
                                  << std::flush;
                    request.clear();
                } else if (request.size() < 16385)
                    request += value;
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
