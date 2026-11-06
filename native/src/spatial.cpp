#include <algorithm>
#include <atlas/spatial.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace atlas {
namespace {
constexpr double pi = std::numbers::pi_v<double>;
bool finiteRange(double value, double low, double high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
bool validId(std::string_view id) noexcept {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](unsigned char byte) {
               return (byte >= 'a' && byte <= 'z') ||
                      (byte >= 'A' && byte <= 'Z') ||
                      (byte >= '0' && byte <= '9') || byte == '_' ||
                      byte == '-';
           });
}
bool validName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128)
        return false;
    for (std::size_t i = 0; i < name.size();) {
        const auto first = static_cast<unsigned char>(name[i++]);
        std::uint32_t code{};
        unsigned continuation{};
        if (first < 0x80) {
            code = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            code = first & 0x1fu;
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            code = first & 0x0fu;
            continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code = first & 0x07u;
            continuation = 3;
        } else {
            return false;
        }
        if (continuation > name.size() - i)
            return false;
        for (unsigned j = 0; j < continuation; ++j) {
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            code = (code << 6) | (next & 0x3fu);
        }
        if ((continuation == 1 && code < 0x80) ||
            (continuation == 2 && code < 0x800) ||
            (continuation == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code < 0x20 ||
            (code >= 0x7f && code <= 0x9f))
            return false;
    }
    return true;
}
double canonicalLongitude(double longitude) noexcept {
    return longitude == 180 ? -180 : longitude;
}
double longitudeWidth(const RegionSettings &region) noexcept {
    const auto width = region.east - region.west;
    return width < 0 ? width + 360 : width;
}
bool contains(const RegionSettings &region, const Position &position) noexcept {
    if (position.latitude < region.south || position.latitude > region.north)
        return false;
    if (region.west == -180 && region.east == 180)
        return true;
    const auto longitude = canonicalLongitude(position.longitude);
    const auto west = canonicalLongitude(region.west);
    auto distance = longitude - west;
    if (distance < 0)
        distance += 360;
    return distance <= longitudeWidth(region);
}
StereoSample panGains(double pan) noexcept {
    if (pan == -1)
        return {1, 0};
    if (pan == 1)
        return {0, 1};
    const auto theta = (pan + 1) * pi / 4;
    return {std::cos(theta), std::sin(theta)};
}
double removeTiny(double value) noexcept {
    return std::abs(value) < 1e-30 ? 0 : value;
}
} // namespace
bool validSpatialSettings(const SpatialSettings &settings) noexcept {
    if (!finiteRange(settings.amount, 0, 1) ||
        settings.regions.size() > spatialRegionCapacity)
        return false;
    for (std::size_t i = 0; i < settings.regions.size(); ++i) {
        const auto &region = settings.regions[i];
        if (!validId(region.id) || !validName(region.name) ||
            !finiteRange(region.south, -90, 90) ||
            !finiteRange(region.north, -90, 90) ||
            region.south >= region.north ||
            !finiteRange(region.west, -180, 180) ||
            !finiteRange(region.east, -180, 180) ||
            region.west == region.east ||
            (region.west == 180 && region.east == -180) ||
            !finiteRange(region.pan, -1, 1) ||
            !finiteRange(region.gainDb, -60, 0) ||
            !finiteRange(region.send, 0, 1) ||
            !finiteRange(region.echoMix, 0, 1) ||
            !finiteRange(region.delayMs, 20, 500) ||
            !finiteRange(region.feedback, 0, .5) ||
            !finiteRange(region.roomSeconds, .2, 2.5) ||
            !finiteRange(region.tone, 0, 1))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (settings.regions[j].id == region.id)
                return false;
    }
    return true;
}
namespace {
SpatialRoute routeUnchecked(const SpatialSettings &settings,
                            const std::optional<Position> &position) {
    SpatialRoute route;
    if (!settings.enabled)
        return route;
    auto gains = panGains(0);
    route.leftGain = gains.left;
    route.rightGain = gains.right;
    if (!position || !finiteRange(position->latitude, -90, 90) ||
        !finiteRange(position->longitude, -180, 180))
        return route;
    for (std::size_t i = 0; i < settings.regions.size(); ++i) {
        const auto &region = settings.regions[i];
        if (!contains(region, *position))
            continue;
        auto pan = region.pan;
        if (region.positionPan) {
            auto distance = canonicalLongitude(position->longitude) -
                            canonicalLongitude(region.west);
            if (distance < 0)
                distance += 360;
            pan = std::clamp(2 * distance / longitudeWidth(region) - 1, -1.0,
                             1.0);
        }
        gains = panGains(pan);
        route.regionSlot = static_cast<RegionSlot>(i + 1);
        route.leftGain = gains.left;
        route.rightGain = gains.right;
        route.regionGain = std::pow(10.0, region.gainDb / 20);
        return route;
    }
    return route;
}
} // namespace
SpatialRoute prepareSpatialRoute(const SpatialSettings &settings,
                                 const std::optional<Position> &position) {
    if (!validSpatialSettings(settings))
        throw std::invalid_argument("invalid spatial settings");
    return routeUnchecked(settings, position);
}
std::vector<SpatialRoute> prepareSpatialRoutes(const SpatialSettings &settings,
                                               std::span<const Event> events) {
    if (!validSpatialSettings(settings))
        throw std::invalid_argument("invalid spatial settings");
    std::vector<SpatialRoute> routes;
    routes.reserve(events.size());
    for (const auto &event : events)
        routes.push_back(routeUnchecked(settings, event.position));
    return routes;
}
SpatialPlan prepareSpatialPlan(const SpatialSettings &settings,
                               std::uint32_t rate, bool hasWetAction) {
    if (!validSpatialSettings(settings) || rate < 22050 || rate > 192000)
        throw std::invalid_argument("invalid spatial plan settings");
    SpatialPlan result;
    result.enabled = settings.enabled;
    result.regionCount = settings.regions.size();
    result.amount = settings.amount;
    result.wetActive =
        settings.enabled && settings.amount > 0 && hasWetAction &&
        std::any_of(settings.regions.begin(), settings.regions.end(),
                    [](const auto &region) { return region.send > 0; });
    if (!result.wetActive)
        return result;
    result.wetAverage = 1.0 / static_cast<double>(result.regionCount);
    result.outputGain = 1.0 / (1 + settings.amount);
    constexpr std::array<double, 4> roomSeconds{.041, .059, .089, .127};
    const auto frames = [rate](double seconds) {
        return static_cast<std::uint32_t>(std::llround(seconds * rate));
    };
    for (std::size_t i = 0; i < settings.regions.size(); ++i) {
        const auto &region = settings.regions[i];
        auto &compiled = result.regions[i];
        const auto echoSeconds = region.delayMs / 1000;
        compiled.delayFrames[0] = frames(echoSeconds);
        compiled.delayFrames[1] =
            frames(echoSeconds + std::min(.017, .1 * echoSeconds));
        for (std::size_t j = 0; j < roomSeconds.size(); ++j) {
            const auto delay = frames(roomSeconds[j]);
            compiled.delayFrames[j + 2] = delay;
            compiled.roomFeedback[j] =
                std::pow(10.0, -3.0 * delay / (rate * region.roomSeconds));
        }
        const auto cutoff =
            200 + region.tone * (std::min(12000.0, .45 * rate) - 200);
        compiled.lowpassPole = std::exp(-2 * pi * cutoff / rate);
        compiled.echoMix = region.echoMix;
        compiled.echoFeedback = region.feedback;
        compiled.send = region.send;
        const auto pan = panGains(region.pan);
        compiled.leftGain = pan.left;
        compiled.rightGain = pan.right;
    }
    return result;
}
SpatialProcessor::SpatialProcessor(const SpatialPlan &plan) : plan_(plan) {
    if (!plan.wetActive)
        return;
    // This boundary accepts only the finite fixed-capacity numeric plan
    // prepared above. Defend direct callers before allocating or entering the
    // callback.
    if (!plan.enabled || plan.regionCount == 0 ||
        plan.regionCount > spatialRegionCapacity ||
        !finiteRange(plan.amount, 0, 1) ||
        !finiteRange(plan.wetAverage, 0, 1) ||
        !finiteRange(plan.outputGain, .5, 1) ||
        plan.wetAverage != 1.0 / static_cast<double>(plan.regionCount) ||
        plan.outputGain != 1.0 / (1 + plan.amount))
        throw std::invalid_argument("invalid numeric spatial plan");
    constexpr std::array<std::uint32_t, 6> maximumFrames{96000, 99264, 7872,
                                                         11328, 17088, 24384};
    for (std::size_t i = 0; i < plan.regionCount; ++i) {
        const auto &coefficients = plan.regions[i];
        if (!finiteRange(coefficients.send, 0, 1) ||
            !finiteRange(coefficients.echoMix, 0, 1) ||
            !finiteRange(coefficients.echoFeedback, 0, .5) ||
            !finiteRange(coefficients.lowpassPole, 0, 1) ||
            coefficients.lowpassPole == 1 ||
            !finiteRange(coefficients.leftGain, 0, 1) ||
            !finiteRange(coefficients.rightGain, 0, 1))
            throw std::invalid_argument("invalid spatial coefficients");
        for (std::size_t j = 0; j < coefficients.delayFrames.size(); ++j) {
            const auto length = coefficients.delayFrames[j];
            if (!length || length > maximumFrames[j] ||
                (j >= 2 &&
                 (!finiteRange(coefficients.roomFeedback[j - 2], 0, .9) ||
                  coefficients.roomFeedback[j - 2] == 1)))
                throw std::invalid_argument("invalid spatial delay capacity");
            regions_[i].delays[j].samples.resize(length);
            allocatedSamples_ += length;
        }
    }
}
void SpatialProcessor::reset() noexcept {
    for (std::size_t i = 0; i < plan_.regionCount && i < spatialRegionCapacity;
         ++i) {
        for (auto &delay : regions_[i].delays) {
            delay.index = delay.valid = 0;
            delay.lowpass = 0;
        }
    }
}
double SpatialProcessor::tick(Delay &delay, double input, double feedback,
                              double pole, bool damped) noexcept {
    const auto length = static_cast<std::uint32_t>(delay.samples.size());
    const auto delayed = delay.valid == length ? delay.samples[delay.index] : 0;
    auto feedbackSample = delayed;
    if (damped) {
        delay.lowpass = removeTiny((1 - pole) * delayed + pole * delay.lowpass);
        feedbackSample = delay.lowpass;
    }
    const auto written =
        removeTiny((1 - feedback) * input + feedback * feedbackSample);
    if (!std::isfinite(delayed) || !std::isfinite(written) ||
        !std::isfinite(delay.lowpass))
        return std::numeric_limits<double>::quiet_NaN();
    delay.samples[delay.index] = written;
    if (++delay.index == length)
        delay.index = 0;
    if (delay.valid < length)
        ++delay.valid;
    return delayed;
}
StereoSample SpatialProcessor::process(
    const std::array<double, spatialRegionCapacity> &input) noexcept {
    StereoSample result;
    if (!plan_.wetActive)
        return result;
    constexpr std::array<double, 4> leftWeights{.4, .3, .2, .1};
    constexpr std::array<double, 4> rightWeights{.1, .2, .3, .4};
    for (std::size_t i = 0; i < plan_.regionCount; ++i) {
        if (!std::isfinite(input[i])) {
            reset();
            ++faults_;
            return {};
        }
        const auto &coefficients = plan_.regions[i];
        auto &state = regions_[i];
        const auto excitation = input[i] * coefficients.send;
        const auto echoLeft = tick(state.delays[0], excitation,
                                   coefficients.echoFeedback, 0, false);
        const auto echoRight = tick(state.delays[1], excitation,
                                    coefficients.echoFeedback, 0, false);
        StereoSample room;
        for (std::size_t j = 0; j < 4; ++j) {
            const auto value = tick(state.delays[j + 2], excitation,
                                    coefficients.roomFeedback[j],
                                    coefficients.lowpassPole, true);
            room.left += leftWeights[j] * value;
            room.right += rightWeights[j] * value;
        }
        result.left += (coefficients.echoMix * echoLeft +
                        (1 - coefficients.echoMix) * room.left) *
                       coefficients.leftGain;
        result.right += (coefficients.echoMix * echoRight +
                         (1 - coefficients.echoMix) * room.right) *
                        coefficients.rightGain;
    }
    result.left *= plan_.wetAverage;
    result.right *= plan_.wetAverage;
    if (!std::isfinite(result.left) || !std::isfinite(result.right)) {
        reset();
        ++faults_;
        return {};
    }
    return result;
}
} // namespace atlas
