#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace atlas::detail {
// Worker-side incremental SHA-256 for saved-content checks, not authentication.
class Sha256 {
public:
    void update(std::string_view bytes);
    std::string hexDigest() const;
private:
    void compress();
    std::array<std::uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bytes_{};
};
}
