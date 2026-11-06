// Test-only protocol host: real portable engine, synthetic clocked output, no audio hardware.
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
class FakeOutputDriver final : public atlas::OutputDriver {
public:
    ~FakeOutputDriver() override { stop(); }
    std::string start(atlas::PlaybackRuntime& runtime, std::uint32_t rate) override {
        stop();
        runtime.deviceStarted(rate, 64, 2);
        running_ = true;
        callbacks_ = std::thread([this, &runtime, rate] {
            std::array<float, 64> left{}, right{};
            std::array<float*, 2> outputs{left.data(), right.data()};
            const auto period = std::chrono::duration<double>(64.0 / rate);
            auto next = std::chrono::steady_clock::now();
            while (running_.load()) {
                runtime.process(outputs.data(), 2, 64);
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
                std::this_thread::sleep_until(next);
            }
        });
        return {};
    }
    void stop() noexcept override {
        running_ = false;
        if (callbacks_.joinable()) callbacks_.join();
    }
private:
    std::atomic<bool> running_{};
    std::thread callbacks_;
};
}
int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string_view(argv[1]) != "ui-host" || std::string_view(argv[2]) != "--db")
            throw std::invalid_argument("fixture usage: ui-host --db DISPOSABLE_LIBRARY");
        atlas::PlaybackRuntime playback(std::make_unique<FakeOutputDriver>());
        atlas::UiHost host(std::filesystem::path(argv[3]), &playback);
        std::string request;
        request.reserve(16385);
        for (;;) {
            playback.service();
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const auto ready = poll(&input, 1, 100);
            if (ready < 0) throw std::runtime_error("fixture stdin poll failed");
            if (!ready) continue;
            std::array<char, 4096> bytes{};
            const auto count = read(STDIN_FILENO, bytes.data(), bytes.size());
            if (count <= 0) break;
            for (ssize_t index = 0; index < count; ++index) {
                const auto value = bytes[static_cast<std::size_t>(index)];
                if (value == '\n') {
                    std::cout << host.request(request) << '\n' << std::flush;
                    request.clear();
                } else if (request.size() < 16385) request += value;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
