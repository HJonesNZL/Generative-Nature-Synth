#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace atlas {
class PlaybackRuntime;
// One worker owns this bounded presentation adapter. Optional playback is
// borrowed. Library access stays read-only. Never call on an audio thread.
class UiHost {
  public:
    explicit UiHost(const std::filesystem::path &library,
                    PlaybackRuntime *playback = nullptr);
    ~UiHost();
    UiHost(const UiHost &) = delete;
    UiHost &operator=(const UiHost &) = delete;
    std::string request(std::string_view envelope);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
