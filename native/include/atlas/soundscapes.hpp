#pragma once
#include <atlas/audio.hpp>
#include <atlas/storage.hpp>
#include <filesystem>
#include <stdexcept>

namespace atlas {
inline constexpr unsigned soundscapeSchemaRevision = 3;
inline constexpr unsigned soundscapeSynthesisRevision = 4;
inline constexpr unsigned soundscapeMappingRevision = 3;
struct SoundscapeCapture {
    std::string name, sourceLibraryLabel;
    SavedSession session;
    TimeBasis basis{TimeBasis::observed};
    Speed speed;
    std::optional<std::pair<Microseconds, Microseconds>> bounds;
    AudioSettings audio;
    // Explicit fallback first (empty species ID), then independent species.
    std::vector<VoiceProfile> profiles;
    unsigned schemaRevision{soundscapeSchemaRevision};
    unsigned synthesisRevision{soundscapeSynthesisRevision};
    unsigned mappingRevision{soundscapeMappingRevision};
};
enum class SoundscapeErrorCode {
    invalidArgument,
    notFound,
    alreadyExists,
    unsupportedSchema,
    corrupt,
    capacity,
    storage
};
class SoundscapeError : public std::runtime_error {
  public:
    SoundscapeError(SoundscapeErrorCode, std::string);
    SoundscapeErrorCode code() const noexcept { return code_; }

  private:
    SoundscapeErrorCode code_;
};
// Worker-owned immutable copied material; no source connection survives
// capture.
class Soundscape {
  public:
    const SoundscapeCapture &capture() const noexcept { return capture_; }
    const Timeline &timeline() const noexcept { return timeline_; }
    const std::string &contentHash() const noexcept { return contentHash_; }

  private:
    Soundscape(SoundscapeCapture, Timeline, std::string);
    SoundscapeCapture capture_;
    Timeline timeline_;
    std::string contentHash_;
    friend Soundscape captureSoundscape(const Timeline &, SoundscapeCapture);
    friend Soundscape loadSoundscape(const std::filesystem::path &);
};
struct SoundscapeSummary {
    std::filesystem::path path;
    std::string name, sourceSessionId, contentHash, error;
    std::size_t eventCount{};
    Microseconds durationUs{};
    TimeBasis basis{TimeBasis::observed};
    Speed speed;
    bool valid{};
    // Listing verifies sealed header metadata, not the entire event material.
    bool contentVerified{};
    SoundscapeErrorCode errorCode{SoundscapeErrorCode::corrupt};
};
// Capture requires the SavedSession returned by ReadOnlyLibrary::load, not an
// unverified catalogue entry. All operations allocate/perform I/O off audio.
Soundscape captureSoundscape(const Timeline &, SoundscapeCapture);
void saveSoundscape(const std::filesystem::path &, const Soundscape &);
Soundscape loadSoundscape(const std::filesystem::path &);
// Sealed metadata only; full load still required before recall/render.
SoundscapeSummary inspectSoundscape(const std::filesystem::path &);
// At most 100 results and 1000 directory entries; capacity failures observable.
std::vector<SoundscapeSummary> listSoundscapes(const std::filesystem::path &,
                                               std::size_t limit = 100);
} // namespace atlas
