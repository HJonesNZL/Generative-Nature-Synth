#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace atlas {
struct UiHostStorage {
    std::filesystem::path soundscapes, exports;
    std::string engineSourceSha256;
};
class PlaybackRuntime;
class VoiceStore;
// One worker owns this bounded presentation adapter. Optional playback is
// borrowed. Library access stays read-only. Never call on an audio thread.
class UiHost {
  public:
    explicit UiHost(const std::filesystem::path &library,
                    PlaybackRuntime *playback = nullptr,
                    VoiceStore *voices = nullptr, UiHostStorage storage = {});
    ~UiHost();
    UiHost(const UiHost &) = delete;
    UiHost &operator=(const UiHost &) = delete;
    // Replaces library/selection state while preserving independently running
    // exports.
    void setLibrary(const std::filesystem::path &library);
    std::string request(std::string_view envelope);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
