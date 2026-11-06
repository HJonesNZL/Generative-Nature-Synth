#include <atlas/wav.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace atlas {
namespace {
void u16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value & 255), static_cast<char>((value >> 8) & 255)};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void u32(std::ostream& out, std::uint32_t value) {
    u16(out, static_cast<std::uint16_t>(value & 65535));
    u16(out, static_cast<std::uint16_t>(value >> 16));
}
struct Staging {
    std::filesystem::path directory;
    explicit Staging(std::filesystem::path path) : directory(std::move(path)) {
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("output staging path already exists; inspect it before retrying");
    }
    ~Staging() { std::error_code error; std::filesystem::remove_all(directory, error); }
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
};
}
AudioStatistics renderWave(const AudioPlan& plan, const std::filesystem::path& output,
                           std::string_view recipeJson, std::uint32_t blockFrames) {
    if (!blockFrames || blockFrames > AudioRenderer::maxBlockFrames || recipeJson.size() > 16384)
        throw std::invalid_argument("invalid render block size or recipe capacity");
    if (output.empty() || output.filename().empty() || std::filesystem::symlink_status(output).type() != std::filesystem::file_type::not_found)
        throw std::invalid_argument("output must be a new file; existing files and symlinks are never replaced");
    const auto dataBytes = plan.totalFrames() * 6;
    const auto commentBytes = static_cast<std::uint32_t>(recipeJson.size() + 1);
    const auto listBytes = 12u + commentBytes + commentBytes % 2;
    const auto fileBytes = 80 + dataBytes + 8 + listBytes;
    if (fileBytes - 8 > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("render exceeds RIFF capacity");
    const auto parent = output.has_parent_path() ? output.parent_path() : std::filesystem::path(".");
    if (std::filesystem::space(parent).available < fileBytes + 1024 * 1024) throw std::runtime_error("insufficient free space for render");
    auto stagingPath = output; stagingPath += ".rendering";
    Staging staging(stagingPath);
    const auto temporary = staging.directory / "audio.wav";
    std::ofstream out(temporary, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write("RIFF", 4); u32(out, static_cast<std::uint32_t>(fileBytes - 8)); out.write("WAVEfmt ", 8);
    u32(out, 40); u16(out, 0xfffe); u16(out, 2); u32(out, plan.settings().sampleRate);
    u32(out, plan.settings().sampleRate * 6); u16(out, 6); u16(out, 24); u16(out, 22); u16(out, 24); u32(out, 3);
    // PCM subtype GUID, explicitly little endian, independent of native struct layout.
    u32(out, 1); u16(out, 0); u16(out, 0x0010);
    const std::array<char, 8> guid{static_cast<char>(0x80), 0, 0, static_cast<char>(0xaa), 0, 0x38, static_cast<char>(0x9b), 0x71};
    out.write(guid.data(), static_cast<std::streamsize>(guid.size()));
    out.write("fact", 4); u32(out, 4); u32(out, static_cast<std::uint32_t>(plan.totalFrames()));
    out.write("data", 4); u32(out, static_cast<std::uint32_t>(dataBytes));
    std::array<float, AudioRenderer::maxBlockFrames * 2> audio{};
    std::array<char, AudioRenderer::maxBlockFrames * 6> pcm{};
    AudioRenderer renderer(plan);
    if (!renderer.play(renderer.generation())) throw std::runtime_error("could not start render");
    for (std::uint64_t frame = 0; frame < plan.totalFrames();) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(blockFrames, plan.totalFrames() - frame));
        if (!renderer.process(std::span(audio).first(count * 2))) throw std::runtime_error("invalid audio render block");
        for (std::size_t i = 0; i < count * 2; ++i) {
            if (!std::isfinite(audio[i]) || std::abs(audio[i]) > 1) throw std::runtime_error("invalid output sample");
            const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(static_cast<double>(audio[i]) * 8388607)));
            for (unsigned byte = 0; byte < 3; ++byte) pcm[i * 3 + byte] = static_cast<char>((value >> (byte * 8)) & 255);
        }
        out.write(pcm.data(), static_cast<std::streamsize>(count * 6));
        frame += count;
    }
    if (renderer.statistics().faults) throw std::runtime_error("audio engine reported a fault; output not published");
    out.write("LIST", 4); u32(out, listBytes); out.write("INFOICMT", 8); u32(out, commentBytes);
    out.write(recipeJson.data(), static_cast<std::streamsize>(recipeJson.size())); out.put('\0');
    if (commentBytes % 2) out.put('\0');
    out.flush(); out.close();
    // Atomic no-replace publication on the same filesystem; failures leave existing output intact.
    std::filesystem::create_hard_link(temporary, output);
    return renderer.statistics();
}
} // namespace atlas
