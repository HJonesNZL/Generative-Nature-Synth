#pragma once
#include <atlas/audio.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace atlas {
inline constexpr std::size_t voiceDefinitionCapacity = 513;
struct VoiceParameterSpec {
    std::string_view id, label, unit, curve;
    double min, max, defaultValue, step;
};
inline constexpr std::array<VoiceParameterSpec, 8> voiceParameterSpecs{{
    {"tuning", "Transpose", "st", "linear", -12, 12, 0, 1},
    {"attack", "Attack", "s", "log", .002, .100, .008, 0},
    {"decay", "Decay (T60)", "s", "log", .100, 3.000, .650, 0},
    {"gain", "Voice gain", "dB", "linear", -60, 0, 0, 0},
    {"brightness", "Brightness", "ratio", "linear", 0, 1, .5, 0},
    {"roughness", "Roughness", "ratio", "linear", 0, 1, 0, 0},
    {"pulseHz", "Pulse rate", "Hz", "linear", 0, 16, 0, 0},
    {"sweepSemitones", "Pitch sweep", "st", "linear", -12, 12, 0, 0},
}};
inline constexpr std::array<std::string_view, 8> voiceParameterIds{
    voiceParameterSpecs[0].id, voiceParameterSpecs[1].id,
    voiceParameterSpecs[2].id, voiceParameterSpecs[3].id,
    voiceParameterSpecs[4].id, voiceParameterSpecs[5].id,
    voiceParameterSpecs[6].id, voiceParameterSpecs[7].id};
inline constexpr std::array<std::string_view, 6> voiceCharacterIds{
    "struck", "whistle", "reed", "bell", "air", "pulse"};
using VoiceValues = std::array<double, 8>;
enum class VoiceGesture { begin, change, end };
enum class VoiceStoreAccess { readWrite, readOnly };
enum class VoiceStoreErrorCode {
    invalidArgument,
    notFound,
    gestureActive,
    conflict,
    readOnly,
    full,
    corrupt,
    unsupportedSchema,
    busy,
    storage
};
class VoiceStoreError : public std::runtime_error {
  public:
    VoiceStoreError(VoiceStoreErrorCode code, std::string message);
    VoiceStoreErrorCode code() const noexcept { return code_; }

  private:
    VoiceStoreErrorCode code_;
};
struct VoiceSpecies {
    std::string speciesId, name;
};
struct VoiceSnapshot {
    std::string id, name;
    std::optional<std::string> speciesId;
    VoiceValues normalizedValues{};
    VoiceSettings settings;
    std::uint64_t savedRevision{}, changeRevision{};
    bool dirty{}, gestureActive{};
};

// Confined to one UI worker. All methods may allocate/perform SQLite I/O; never
// call from audio. Each host owns its drafts. Only explicit save writes the
// app-owned SQLite sidecar.
class VoiceStore {
  public:
    class DraftUpdate {
      public:
        ~DraftUpdate();
        DraftUpdate(DraftUpdate &&) noexcept;
        DraftUpdate &operator=(DraftUpdate &&) noexcept;

      private:
        struct State;
        explicit DraftUpdate(std::unique_ptr<State>);
        std::unique_ptr<State> state_;
        friend class VoiceStore;
    };
    // Stage every allocation/validation before preparing audio, then publish by
    // noexcept swap on this same worker. No durable presets are overwritten.
    // Accepts a subset; fallback must be first when present. Any intervening
    // draft/catalogue mutation makes a
    // staged update stale; a token belongs to exactly one store lifetime.
    DraftUpdate stageProfiles(std::span<const VoiceProfile>,
                              std::span<const VoiceSpecies> = {}) const;
    bool commitProfiles(DraftUpdate &&) noexcept;
    VoiceStore(const std::filesystem::path &,
               std::span<const VoiceSpecies> = {},
               VoiceStoreAccess = VoiceStoreAccess::readWrite);
    ~VoiceStore();
    VoiceStore(const VoiceStore &) = delete;
    VoiceStore &operator=(const VoiceStore &) = delete;
    void registerSpecies(std::span<const VoiceSpecies>);
    // Serial worker: current draft first, then saved/exact candidate settings.
    // New editable entries are optional; a full catalogue or a valid provider
    // ID beyond the editable 256-byte limit never interrupts Live playback.
    VoiceSettings liveSettings(std::string_view speciesId,
                               std::string_view name = {});
    std::vector<VoiceSnapshot> voices() const;
    VoiceSnapshot getVoice(std::string_view id) const;
    VoiceSnapshot previewParameter(std::string_view id,
                                   std::string_view parameterId,
                                   double normalizedValue, VoiceGesture) const;
    VoiceSnapshot setParameter(std::string_view id,
                               std::string_view parameterId,
                               double normalizedValue, VoiceGesture);
    VoiceSnapshot save(std::string_view id,
                       std::uint64_t expectedSavedRevision);
    VoiceSnapshot revert(std::string_view id);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
