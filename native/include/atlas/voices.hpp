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
inline constexpr std::array<VoiceParameterSpec, 4> voiceParameterSpecs{{
    {"tuning", "Transpose", "st", "linear", -12, 12, 0, 1},
    {"attack", "Attack", "s", "log", .002, .100, .008, 0},
    {"decay", "Decay (T60)", "s", "log", .100, 3.000, .650, 0},
    {"gain", "Voice gain", "dB", "linear", -60, 0, 0, 0},
}};
inline constexpr std::array<std::string_view, 4> voiceParameterIds{
    voiceParameterSpecs[0].id, voiceParameterSpecs[1].id, voiceParameterSpecs[2].id, voiceParameterSpecs[3].id};
using VoiceValues = std::array<double, 4>;
enum class VoiceGesture { begin, change, end };
enum class VoiceStoreAccess { readWrite, readOnly };
enum class VoiceStoreErrorCode {
    invalidArgument, notFound, gestureActive, conflict, readOnly, full, corrupt, unsupportedSchema, busy, storage
};
class VoiceStoreError : public std::runtime_error {
public:
    VoiceStoreError(VoiceStoreErrorCode code, std::string message);
    VoiceStoreErrorCode code() const noexcept { return code_; }
private:
    VoiceStoreErrorCode code_;
};
struct VoiceSpecies { std::string speciesId, name; };
struct VoiceSnapshot {
    std::string id, name;
    std::optional<std::string> speciesId;
    VoiceValues normalizedValues{};
    VoiceSettings settings;
    std::uint64_t savedRevision{}, changeRevision{};
    bool dirty{}, gestureActive{};
};

// Confined to one UI worker. All methods may allocate/perform SQLite I/O; never call from audio.
// Each host owns its drafts. Only explicit save writes the app-owned SQLite sidecar.
class VoiceStore {
public:
    VoiceStore(const std::filesystem::path&, std::span<const VoiceSpecies> = {}, VoiceStoreAccess = VoiceStoreAccess::readWrite);
    ~VoiceStore();
    VoiceStore(const VoiceStore&) = delete;
    VoiceStore& operator=(const VoiceStore&) = delete;
    void registerSpecies(std::span<const VoiceSpecies>);
    std::vector<VoiceSnapshot> voices() const;
    VoiceSnapshot getVoice(std::string_view id) const;
    VoiceSnapshot previewParameter(std::string_view id, std::string_view parameterId, double normalizedValue, VoiceGesture) const;
    VoiceSnapshot setParameter(std::string_view id, std::string_view parameterId, double normalizedValue, VoiceGesture);
    VoiceSnapshot save(std::string_view id, std::uint64_t expectedSavedRevision);
    VoiceSnapshot revert(std::string_view id);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas
