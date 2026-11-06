#include <algorithm>
#include <array>
#include <atlas/audio.hpp>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace atlas;
constexpr double pi = std::numbers::pi_v<double>;
// Frozen engineering targets; see ALIAS_PROBE.md before interpreting them.
constexpr double residualLimitDb = -60, convergenceLimitDb = -80;
constexpr double integerOracleLimit = 2e-7;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void fft(std::vector<std::complex<double>> &data, bool inverse) {
    const auto size = data.size();
    check(size >= 2 && (size & (size - 1)) == 0, "FFT power-of-two size");
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        auto bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length *= 2) {
        const auto angle = (inverse ? 2 : -2) * pi / static_cast<double>(length);
        const auto step = std::polar(1.0, angle);
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> rotation{1};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto even = data[start + j];
                const auto odd = rotation * data[start + j + length / 2];
                data[start + j] = even + odd;
                data[start + j + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
    if (inverse)
        for (auto &value : data)
            value /= static_cast<double>(size);
}
std::vector<double> lowpassDecimate(std::vector<std::complex<double>> data,
                                  unsigned factor) {
    check(data.size() % factor == 0, "Decimation alignment");
    const auto outputSize = data.size() / factor;
    fft(data, false);
    const auto boundary = outputSize / 2;
    for (std::size_t i = boundary; i <= data.size() - boundary; ++i)
        data[i] = 0;
    fft(data, true);
    std::vector<double> result(outputSize);
    for (std::size_t i = 0; i < outputSize; ++i)
        result[i] = data[i * factor].real();
    return result;
}
void verifyProjection() {
    constexpr std::size_t size = 256;
    constexpr unsigned factor = 8;
    std::vector<std::complex<double>> data(size * factor);
    for (std::size_t n = 0; n < data.size(); ++n) {
        const auto phase = 2 * pi * static_cast<double>(n) /
                           static_cast<double>(data.size());
        data[n] = std::sin(7 * phase) + .25 * std::sin(203 * phase);
    }
    auto restored = data;
    fft(restored, false);
    fft(restored, true);
    for (std::size_t n = 0; n < data.size(); ++n)
        check(std::abs(restored[n] - data[n]) < 1e-11,
              "FFT roundtrip reference failed");
    const auto filtered = lowpassDecimate(std::move(data), factor);
    for (std::size_t n = 0; n < size; ++n)
        check(std::abs(filtered[n] -
                       std::sin(2 * pi * 7 * static_cast<double>(n) / size)) <
                  1e-11,
              "Known pass/stop tone decimation failed");
}
struct Reference {
    VoiceCoefficients coefficients;
    double phaseStep{};
    std::array<double, voicePartialCapacity> phases{};
    explicit Reference(const AudioPlan &plan, const VoiceSettings &settings) {
        check(plan.actions().size() == 1 &&
                  plan.prepareVoiceUpdate(1, settings, coefficients) &&
                  !coefficients.legacyStruck && !coefficients.scaleQuantized,
              "Reference requires isolated nonlegacy unquantized voice");
        const auto &action = plan.actions().front();
        check(action.frame == 0 && action.midiNote == 88,
              "Highest historical mapped-pitch fixture changed");
        phaseStep = action.phaseStep * coefficients.pitchMultiplier;
        auto seed = action.phaseSeed;
        for (std::size_t i = 0; i < coefficients.partialCount; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            phases[i] = static_cast<double>(seed) / 4294967296.0 * 2 * pi *
                        coefficients.phaseSpread;
        }
    }
    std::size_t lifetime() const {
        return coefficients.attackFrames + coefficients.decayFrames +
               coefficients.releaseFrames;
    }
    double sample(double age) const {
        if (age < 0 || age >= static_cast<double>(lifetime()))
            return 0;
        const auto &c = coefficients;
        const auto attack = static_cast<double>(c.attackFrames);
        const auto releaseStart = attack + c.decayFrames;
        const auto decayAge = std::max(0.0, age - attack);
        auto envelope = std::exp(std::log(c.decayFactor) * decayAge);
        if (age < attack)
            envelope *= (1 - std::cos(pi * age / attack)) * .5;
        if (age >= releaseStart)
            envelope *= (1 + std::cos(pi * (age - releaseStart) /
                                      c.releaseFrames)) * .5;
        auto phaseIntegral = age;
        if (c.sweepFrames > 1) {
            const auto count =
                std::min(age, static_cast<double>(c.sweepFrames - 1));
            const auto logRatio = std::log(c.sweepFactor);
            phaseIntegral = std::expm1(logRatio * count) /
                                std::expm1(logRatio) +
                            (age - count) * c.sweepEnd;
        }
        double tone{};
        for (std::size_t i = 0; i < c.partialCount; ++i) {
            const auto &partial = c.partials[i];
            tone += partial.amplitude *
                    std::exp(std::log(partial.decayFactor) * age) *
                    std::sin(phases[i] + partial.ratio * phaseStep *
                                              phaseIntegral);
        }
        const auto pulse = c.pulsePhaseStep == 0
                               ? 1.0
                               : std::pow(.5 + .5 * std::cos(
                                                       c.pulsePhaseStep * age),
                                          2);
        const auto roughness =
            1 - .5 * c.roughnessDepth + .5 * c.roughnessDepth *
                                           std::cos(c.roughnessPhaseStep * age);
        return .05 * envelope * tone * pulse * roughness * c.gain;
    }
    std::vector<double> filtered(std::size_t size, std::size_t offset,
                                 unsigned factor) const {
        std::vector<std::complex<double>> data(size * factor);
        for (std::size_t n = 0; n < lifetime() * factor; ++n)
            data[offset * factor + n] =
                sample(static_cast<double>(n) / factor);
        return lowpassDecimate(std::move(data), factor);
    }
};
AudioPlan planFor(const VoiceSettings &voice, std::uint32_t rate) {
    Event event;
    event.id = "alias:fixture";
    event.sourceId = "alias";
    event.kind = "acoustic_species_detection";
    event.locationKind = "station_detection";
    event.stationId = "alias:station";
    event.speciesId = "tone:4";
    event.revisionSequence = 1;
    const std::array<VoiceProfile, 2> profiles{{{"", {}}, {"tone:4", voice}}};
    AudioSettings settings;
    settings.sampleRate = rate;
    settings.rootMidi = 72;
    return AudioPlan(Timeline({event}, TimeBasis::observed), settings, profiles,
                     true);
}
std::vector<double> runtime(const AudioPlan &plan, std::size_t size,
                            std::size_t offset, std::size_t lifetime) {
    AudioRenderer renderer(plan);
    check(renderer.play(renderer.generation()), "Runtime play failed");
    std::vector<double> result(size);
    std::array<float, 514> block{};
    for (std::size_t pos = 0; pos < lifetime;) {
        const auto count = std::min<std::size_t>(257, lifetime - pos);
        check(renderer.process(std::span(block).first(count * 2)),
              "Runtime render failed");
        for (std::size_t i = 0; i < count; ++i)
            result[offset + pos + i] = block[i * 2];
        pos += count;
    }
    check(renderer.statistics().faults == 0, "Runtime numerical fault");
    return result;
}
double relativeDb(std::span<const double> actual,
                  std::span<const double> reference) {
    check(actual.size() == reference.size(), "Reference shape mismatch");
    double error{}, energy{};
    for (std::size_t i = 0; i < actual.size(); ++i) {
        error += (actual[i] - reference[i]) * (actual[i] - reference[i]);
        energy += reference[i] * reference[i];
    }
    check(energy > 0 && std::isfinite(error), "Invalid comparison energy");
    return 10 * std::log10(std::max(error / energy, 1e-30));
}
} // namespace
int main() {
    try {
        verifyProjection();
        unsigned failures{};
        std::cout << std::fixed << std::setprecision(2);
        for (const auto rate : {22050u, 48000u}) {
            for (int character = 0; character < 6; ++character) {
                for (const auto sweep : {-12.0, 0.0, 12.0}) {
                    VoiceSettings voice;
                    voice.character = static_cast<VoiceCharacter>(character);
                    voice.attackSeconds = .002;
                    voice.decaySeconds = .100;
                    voice.brightness = voice.roughness = 1;
                    voice.pulseHz = 16;
                    voice.sweepSemitones = sweep;
                    const auto plan = planFor(voice, rate);
                    const Reference reference(plan, voice);
                    std::size_t size = 1;
                    while (size < reference.lifetime() * 4)
                        size *= 2;
                    const auto offset = size / 4;
                    const auto actual =
                        runtime(plan, size, offset, reference.lifetime());
                    double integerError{};
                    for (std::size_t n = 0; n < reference.lifetime(); ++n)
                        integerError = std::max(
                            integerError,
                            std::abs(actual[offset + n] -
                                     reference.sample(static_cast<double>(n))));
                    const auto at8 = reference.filtered(size, offset, 8);
                    const auto at16 = reference.filtered(size, offset, 16);
                    const auto extended =
                        reference.filtered(size * 2, offset, 16);
                    const auto residual = relativeDb(actual, at16);
                    const auto convergence = relativeDb(at8, at16);
                    const auto padding = relativeDb(
                        at16, std::span(extended).first(size));
                    const bool passed = residual <= residualLimitDb &&
                                        convergence <= convergenceLimitDb &&
                                        padding <= convergenceLimitDb &&
                                        integerError <= integerOracleLimit;
                    failures += passed ? 0u : 1u;
                    std::cout << (passed ? "PASS" : "FAIL") << " rate=" << rate
                              << " character=" << character
                              << " sweep=" << sweep << " partials="
                              << static_cast<unsigned>(
                                     reference.coefficients.partialCount)
                              << " residual_dBr=" << residual
                              << " convergence_dBr=" << convergence
                              << " padding_dBr=" << padding
                              << " integer_error=" << std::scientific
                              << integerError
                              << std::fixed << '\n';
                }
            }
        }
        return failures ? 1 : 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
