#include "atlas_build_identity.hpp"
#include <atlas/playback.hpp>
#include <atlas/ui_host.hpp>
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
void serve(atlas::UiHost &host, atlas::PlaybackRuntime &playback) {
    // Raw reads avoid iostream prefetch obscuring pending complete requests
    // from poll.
    std::string line;
    bool overlong{};
    for (;;) {
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
    juce::ScopedJuceInitialiser_GUI initialise;
    int result{};
    std::thread worker([&] {
        try {
            atlas::PlaybackRuntime playback(atlas::makeJuceOutputDriver());
            atlas::UiHost host(std::filesystem::path(argv[3]), &playback);
            serve(host, playback);
        } catch (const std::exception &exception) {
            std::cerr << "audio host unavailable: " << exception.what() << '\n';
            result = 1;
        }
        juce::MessageManager::callAsync(
            [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();
    return result;
}
