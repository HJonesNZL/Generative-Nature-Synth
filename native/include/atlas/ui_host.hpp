#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace atlas {
// One worker owns this bounded, read-only presentation adapter. Never call on an audio thread.
class UiHost {
public:
    explicit UiHost(const std::filesystem::path& library);
    ~UiHost();
    UiHost(const UiHost&) = delete;
    UiHost& operator=(const UiHost&) = delete;
    std::string request(std::string_view envelope);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
