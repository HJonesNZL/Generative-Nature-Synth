#pragma once
#include <array>
#include <atlas/core.hpp>

namespace atlas {
inline constexpr std::size_t spatialRegionCapacity = 8;
using RegionSlot = std::uint8_t;
struct RegionSettings {
    std::string id, name;
    double south{-90}, north{90}, west{-180}, east{180};
    double pan{}, gainDb{}, send{.25}, echoMix{.3}, delayMs{180}, feedback{.25},
        roomSeconds{1.2}, tone{.5};
    bool positionPan{};
};
struct SpatialSettings {
    bool enabled{};
    double amount{.25};
    std::vector<RegionSettings> regions;
};
bool validSpatialSettings(const SpatialSettings &) noexcept;
struct StereoSample {
    double left{}, right{};
};
// Numeric onset snapshot. Slot zero is unassigned. Disabled routes are exact
// dual mono; enabled routes use equal-power pan and attenuating region gain.
struct SpatialRoute {
    RegionSlot regionSlot{};
    double leftGain{1}, rightGain{1}, regionGain{1};
};
SpatialRoute prepareSpatialRoute(const SpatialSettings &,
                                 const std::optional<Position> &);
std::vector<SpatialRoute> prepareSpatialRoutes(const SpatialSettings &,
                                               std::span<const Event>);
struct SpatialRegionCoefficients {
    std::array<std::uint32_t, 6> delayFrames{};
    std::array<double, 4> roomFeedback{};
    double send{}, echoMix{}, echoFeedback{}, lowpassPole{}, leftGain{},
        rightGain{};
};
struct SpatialPlan {
    bool enabled{}, wetActive{};
    std::size_t regionCount{};
    double amount{}, wetAverage{}, outputGain{1};
    std::array<SpatialRegionCoefficients, spatialRegionCapacity> regions{};
};
// Worker-only validation/coefficient preparation. hasWetAction means an emitted
// action routes to a region with a positive send, not a fabricated observation.
SpatialPlan prepareSpatialPlan(const SpatialSettings &,
                               std::uint32_t sampleRate, bool hasWetAction);

// Construct/destroy on the worker. Runtime methods only touch fixed numeric
// resources; reset invalidates old samples without clearing delay storage.
class SpatialProcessor {
  public:
    explicit SpatialProcessor(const SpatialPlan &);
    SpatialProcessor(const SpatialProcessor &) = delete;
    SpatialProcessor &operator=(const SpatialProcessor &) = delete;
    StereoSample
    process(const std::array<double, spatialRegionCapacity> &) noexcept;
    void reset() noexcept;
    std::size_t allocatedSamples() const noexcept { return allocatedSamples_; }
    std::uint64_t faults() const noexcept { return faults_; }

  private:
    struct Delay {
        std::vector<double> samples;
        std::uint32_t index{}, valid{};
        double lowpass{};
    };
    struct Region {
        std::array<Delay, 6> delays;
    };
    SpatialPlan plan_;
    std::array<Region, spatialRegionCapacity> regions_;
    std::size_t allocatedSamples_{};
    std::uint64_t faults_{};
    static double tick(Delay &, double input, double feedback, double pole,
                       bool damped) noexcept;
};
} // namespace atlas
