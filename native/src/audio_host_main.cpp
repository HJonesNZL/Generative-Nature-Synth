#include "atlas_build_identity.hpp"
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <juce_events/juce_events.h>
#include <string>
#include <thread>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#else
#error "atlas-audio-host currently requires POSIX bounded stdin polling"
#endif

namespace {
void serve(atlas::UiHost &host, atlas::PlaybackRuntime &playback,
           const std::atomic<bool> &closing) {
    // Raw reads avoid iostream prefetch obscuring pending complete requests
    // from poll.
    std::string line;
    bool overlong{};
    while (!closing.load(std::memory_order_acquire)) {
        playback.service();
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const auto ready = ::poll(&input, 1, 100);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ready == 0)
            continue;
        char buffer[4096];
        const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            if (count < 0 && errno == EINTR)
                continue;
            break;
        }
        for (ssize_t i = 0; i < count; ++i) {
            const auto byte = buffer[i];
            if (byte == '\n') {
                std::cout << host.request(overlong ? std::string(16385, ' ')
                                                   : line)
                          << '\n'
                          << std::flush;
                line.clear();
                overlong = false;
            } else if (!overlong) {
                if (line.size() == 16384) {
                    line.clear();
                    overlong = true;
                } else
                    line.push_back(byte);
            }
        }
    }
}
class AudioHostApplication final : public juce::JUCEApplicationBase {
  public:
    const juce::String getApplicationName() override {
        return "Animal Atlas Audio Host";
    }
    const juce::String getApplicationVersion() override { return "0.3.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String &) override {
        const auto parameters = getCommandLineParameterArray();
        const auto library = std::filesystem::path(parameters[2].toRawUTF8());
        worker_ = std::thread([this, library] {
            try {
                atlas::PlaybackRuntime playback(atlas::makeJuceOutputDriver());
                atlas::UiHost host(library, &playback);
                serve(host, playback, closing_);
            } catch (const std::exception &exception) {
                std::cerr << "audio host unavailable: " << exception.what()
                          << '\n';
                exitCode_.store(1, std::memory_order_release);
            }
            // Runtime and driver are already destroyed while the message loop
            // is alive.
            juce::MessageManager::callAsync(
                [] { juce::JUCEApplicationBase::quit(); });
        });
    }
    void shutdown() override {
        if (worker_.joinable())
            worker_.join();
        setApplicationReturnValue(exitCode_.load(std::memory_order_acquire));
    }
    void anotherInstanceStarted(const juce::String &) override {}
    void systemRequestedQuit() override {
        closing_.store(true, std::memory_order_release);
    }
    void suspended() override {}
    void resumed() override {}
    void unhandledException(const std::exception *, const juce::String &,
                            int) override {
        exitCode_.store(1, std::memory_order_release);
        closing_.store(true, std::memory_order_release);
    }

  private:
    std::thread worker_;
    std::atomic<bool> closing_{};
    std::atomic<int> exitCode_{};
};
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "version") {
        std::cout
            << "{\"engine\":\"animal-atlas-native-audio\",\"source_sha256\":\""
            << atlas::nativeSourceSha256 << "\"}\n";
        return 0;
    }
    if (argc != 4 || std::string_view(argv[1]) != "ui-host" ||
        std::string_view(argv[2]) != "--db") {
        std::cerr << "usage: atlas-audio-host version | ui-host --db PATH\n";
        return 2;
    }
    juce::JUCEApplicationBase::createInstance =
        []() -> juce::JUCEApplicationBase * {
        return new AudioHostApplication();
    };
    const char *arguments[]{argv[0], argv[1], argv[2], argv[3]};
    // Public JUCE startup creates NSApplication on macOS before running its
    // message loop.
    return juce::JUCEApplicationBase::main(argc, arguments);
}
