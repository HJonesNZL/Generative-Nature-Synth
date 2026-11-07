#include <algorithm>
#include <atlas/audio.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace atlas {
namespace {
constexpr double pi = std::numbers::pi_v<double>;
constexpr Microseconds maximumDurationUs = 1800LL * 1000000;
std::uint64_t frameAt(Microseconds us, std::uint32_t rate) {
    return (static_cast<std::uint64_t>(us) * rate + 999999) / 1000000;
}
std::uint32_t hashSpecies(std::string_view id) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const auto byte : id) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 16777619u;
    }
    return hash;
}
} // namespace

unsigned directSpeciesNote(unsigned rootMidi, std::string_view species) noexcept {
    constexpr std::array<unsigned, 8> offsets{0, 2, 4, 7, 9, 12, 14, 16};
    return rootMidi + (species.empty() ? 0 : offsets[hashSpecies(species) % offsets.size()]);
}

namespace {
bool validCharacter(VoiceCharacter character) noexcept {
    switch (character) {
    case VoiceCharacter::struck:
    case VoiceCharacter::whistle:
    case VoiceCharacter::reed:
    case VoiceCharacter::bell:
    case VoiceCharacter::air:
    case VoiceCharacter::pulse:
        return true;
    }
    return false;
}
std::uint32_t seedFor(const Event &event) noexcept {
    auto hash = hashSpecies(event.sourceId);
    hash *= 16777619u; // Delimiter byte zero.
    for (const auto byte : event.id) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 16777619u;
    }
    const auto revision = static_cast<std::uint64_t>(event.revisionSequence);
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= static_cast<std::uint32_t>((revision >> (8 * i)) & 255u);
        hash *= 16777619u;
    }
    return hash ? hash : 0x6d2b79f5u;
}
std::uint32_t nextPhaseSeed(std::uint32_t &seed) noexcept {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
} // namespace

bool validVoiceSettings(const VoiceSettings &settings) noexcept {
    return settings.tuningSemitones >= -12 && settings.tuningSemitones <= 12 &&
           std::isfinite(settings.attackSeconds) &&
           settings.attackSeconds >= .002 && settings.attackSeconds <= .100 &&
           std::isfinite(settings.decaySeconds) &&
           settings.decaySeconds >= .100 && settings.decaySeconds <= 3 &&
           std::isfinite(settings.gainDb) && settings.gainDb >= -60 &&
           settings.gainDb <= 0 && validCharacter(settings.character) &&
           std::isfinite(settings.brightness) && settings.brightness >= 0 &&
           settings.brightness <= 1 && std::isfinite(settings.roughness) &&
           settings.roughness >= 0 && settings.roughness <= 1 &&
           std::isfinite(settings.pulseHz) && settings.pulseHz >= 0 &&
           settings.pulseHz <= 16 && std::isfinite(settings.sweepSemitones) &&
           settings.sweepSemitones >= -12 && settings.sweepSemitones <= 12;
}

namespace {
VoiceCoefficients
coefficientsFor(const VoiceSettings &settings, std::uint32_t rate,
                unsigned rootMidi,
                const CompositionSettings &composition) noexcept {
    VoiceCoefficients result;
    result.attackFrames =
        static_cast<std::uint32_t>(std::ceil(settings.attackSeconds * rate));
    result.decayFrames =
        static_cast<std::uint32_t>(std::ceil(settings.decaySeconds * rate));
    result.releaseFrames = static_cast<std::uint32_t>(std::ceil(.020 * rate));
    result.pitchMultiplier = std::exp2(settings.tuningSemitones / 12.0);
    result.scaleQuantized = composition.enabled;
    if (composition.enabled) {
        const auto maximumBase = static_cast<int>(std::min(76u, rootMidi + 16));
        for (int pitchClass = 0; pitchClass < 12; ++pitchClass) {
            const auto maximumForClass =
                maximumBase - ((maximumBase - pitchClass + 12) % 12);
            const auto preferred = maximumForClass + settings.tuningSemitones;
            auto chosen = preferred;
            auto distance = 1000;
            for (int note = preferred - 12;
                 note <= std::min(88, preferred + 12); ++note) {
                if (compositionScaleContains(composition, note) &&
                    std::abs(note - preferred) < distance) {
                    chosen = note;
                    distance = std::abs(note - preferred);
                }
            }
            result.scalePitchMultipliers[static_cast<std::size_t>(pitchClass)] =
                std::exp2((chosen - maximumForClass) / 12.0);
        }
    }
    result.decayFactor = std::exp(-std::log(1000.0) / result.decayFrames);
    result.gain = std::pow(10.0, settings.gainDb / 20.0);
    result.legacyStruck = settings.character == VoiceCharacter::struck &&
                          settings.brightness == .5 &&
                          settings.roughness == 0 && settings.pulseHz == 0 &&
                          settings.sweepSemitones == 0;
    if (result.legacyStruck)
        return result;

    result.sweepEnd = std::exp2(settings.sweepSemitones / 12.0);
    if (settings.sweepSemitones != 0) {
        result.sweepFrames = result.attackFrames + result.decayFrames;
        result.sweepFactor = std::exp2(settings.sweepSemitones /
                                       (12.0 * (result.sweepFrames - 1)));
    }
    result.pulsePhaseStep = 2 * pi * settings.pulseHz / rate;
    result.roughnessDepth = settings.roughness;
    result.roughnessPhaseStep =
        settings.roughness == 0
            ? 0
            : 2 * pi * (29 + 31 * settings.brightness) / rate;
    result.phaseSpread = settings.character == VoiceCharacter::air ? 1 : 0;

    std::array<double, voicePartialCapacity> ratios{1, 2, 3, 4, 5, 6, 7, 8};
    std::array<double, voicePartialCapacity> weights{};
    switch (settings.character) {
    case VoiceCharacter::struck:
        weights = {1, .25, .08};
        break;
    case VoiceCharacter::whistle:
        weights = {1, .03 + .07 * settings.brightness,
                   .005 * settings.brightness};
        break;
    case VoiceCharacter::reed:
        ratios = {1, 3, 5, 7, 9, 11, 13, 15};
        weights = {1, .60, .40, .26, .18, .13, .09, .07};
        break;
    case VoiceCharacter::bell:
        ratios = {1, 1.503, 2.71, 4.11, 5.43, 6.79, 8.21, 9.67};
        weights = {.60, 1, .70, .50, .30, .20, .14, .10};
        break;
    case VoiceCharacter::air:
        ratios = {.73, 1.19, 1.71, 2.47, 3.13, 4.61, 6.17, 8.31};
        weights = {.12, .25, .45, .70, 1, .85, .60, .40};
        break;
    case VoiceCharacter::pulse:
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const auto harmonic = static_cast<double>(i + 1);
            weights[i] = std::sin(pi * .32 * harmonic) / harmonic;
        }
        break;
    }
    const auto maximumFrequency =
        440 *
        std::exp2(((composition.enabled ? 88.0
                                        : static_cast<double>(rootMidi) +
                                              settings.tuningSemitones + 16) -
                   69) /
                  12.0) *
        std::max(1.0, result.sweepEnd);
    const auto modulationHz =
        (result.roughnessPhaseStep + 2 * result.pulsePhaseStep) * rate /
        (2 * pi);
    const auto tilt = .25 + 1.5 * settings.brightness;
    double weightSum{};
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        if (weights[i] == 0 ||
            maximumFrequency * ratios[i] + modulationHz > .45 * rate)
            continue;
        const auto weight = weights[i] * (i ? tilt : 1);
        const auto modalDecay =
            settings.character == VoiceCharacter::bell
                ? std::exp(-std::log(1000.0) * .12 * static_cast<double>(i) /
                           result.decayFrames)
                : 1;
        result.partials[result.partialCount++] = {ratios[i], weight,
                                                  modalDecay};
        weightSum += std::abs(weight);
    }
    for (std::size_t i = 0; i < result.partialCount; ++i)
        result.partials[i].amplitude /= weightSum;
    return result;
}
std::uint32_t lifetime(const VoiceCoefficients &coefficients) noexcept {
    return coefficients.attackFrames + coefficients.decayFrames +
           coefficients.releaseFrames;
}
bool validCoefficients(
    const VoiceCoefficients &c, std::uint32_t rate, std::uint32_t releaseFrames,
    double maximumPhaseStep, double minimumPitchMultiplier,
    double maximumPitchMultiplier, bool compositionEnabled,
    const std::array<double, 12> &maximumScalePitchMultipliers) noexcept {
    if (c.attackFrames < (rate * 2 + 999) / 1000 ||
        c.attackFrames > (rate + 9) / 10 || c.decayFrames < (rate + 9) / 10 ||
        c.decayFrames > rate * 3 || c.releaseFrames != releaseFrames ||
        !std::isfinite(c.pitchMultiplier) || c.pitchMultiplier < .5 ||
        c.pitchMultiplier > 2 || c.pitchMultiplier < minimumPitchMultiplier ||
        c.pitchMultiplier > maximumPitchMultiplier ||
        !std::isfinite(c.decayFactor) || c.decayFactor <= 0 ||
        c.decayFactor > 1 || !std::isfinite(c.gain) || c.gain < .001 ||
        c.gain > 1 || !std::isfinite(c.sweepFactor) || c.sweepFactor < .999 ||
        c.sweepFactor > 1.001 || !std::isfinite(c.sweepEnd) ||
        c.sweepEnd < .5 || c.sweepEnd > 2 || !std::isfinite(c.pulsePhaseStep) ||
        c.pulsePhaseStep < 0 || c.pulsePhaseStep > 2 * pi * 16 / rate ||
        !std::isfinite(c.roughnessPhaseStep) || c.roughnessPhaseStep < 0 ||
        c.roughnessPhaseStep > 2 * pi * 60 / rate ||
        !std::isfinite(c.roughnessDepth) || c.roughnessDepth < 0 ||
        c.roughnessDepth > 1 || !std::isfinite(c.phaseSpread) ||
        c.phaseSpread < 0 || c.phaseSpread > 1 ||
        (c.sweepFrames != 0 &&
         c.sweepFrames != c.attackFrames + c.decayFrames) ||
        (c.sweepFrames == 0 && (c.sweepFactor != 1 || c.sweepEnd != 1)))
        return false;
    if (c.scaleQuantized != compositionEnabled)
        return false;
    if (compositionEnabled) {
        for (unsigned pitchClass = 0; pitchClass < 12; ++pitchClass) {
            const auto multiplier = c.scalePitchMultipliers[pitchClass];
            if (!std::isfinite(multiplier) || multiplier < .4 ||
                multiplier > 2.52 ||
                multiplier > maximumScalePitchMultipliers[pitchClass] + 1e-12)
                return false;
        }
    } else if (std::any_of(c.scalePitchMultipliers.begin(),
                           c.scalePitchMultipliers.end(),
                           [](double value) { return value != 0; })) {
        return false;
    }
    if (c.legacyStruck)
        return c.partialCount == 0 && c.sweepFrames == 0 &&
               c.pulsePhaseStep == 0 && c.roughnessDepth == 0 &&
               c.roughnessPhaseStep == 0 && c.phaseSpread == 0;
    if (c.partialCount == 0 || c.partialCount > voicePartialCapacity)
        return false;
    double weightSum{};
    for (std::size_t i = 0; i < c.partialCount; ++i) {
        const auto &p = c.partials[i];
        if (!std::isfinite(p.ratio) || p.ratio <= 0 ||
            !std::isfinite(p.amplitude) || !std::isfinite(p.decayFactor) ||
            p.decayFactor <= 0 || p.decayFactor > 1 ||
            maximumPhaseStep * (compositionEnabled ? 1 : c.pitchMultiplier) *
                        std::max(1.0, c.sweepEnd) * p.ratio +
                    c.roughnessPhaseStep + 2 * c.pulsePhaseStep >
                .9 * pi + 1e-12)
            return false;
        weightSum += std::abs(p.amplitude);
    }
    return weightSum > 0 && weightSum <= 1.000000000001;
}
} // namespace

VoiceSlot AudioPlan::voiceSlot(std::string_view speciesId) const noexcept {
    const auto found =
        std::find_if(voiceProfiles_.begin(), voiceProfiles_.end(),
                     [&](const VoiceProfile &profile) {
                         return profile.speciesId == speciesId;
                     });
    return found == voiceProfiles_.end()
               ? 0
               : static_cast<VoiceSlot>(found - voiceProfiles_.begin());
}
bool AudioPlan::prepareVoiceUpdate(
    VoiceSlot slot, const VoiceSettings &settings,
    VoiceCoefficients &coefficients) const noexcept {
    const auto root = static_cast<std::int64_t>(settings_.rootMidi) +
                      settings.tuningSemitones;
    if (!editableVoices_ || slot >= voiceProfiles_.size() ||
        !validVoiceSettings(settings) || root < 48 || root > 72)
        return false;
    coefficients = coefficientsFor(settings, settings_.sampleRate,
                                   settings_.rootMidi, settings_.composition);
    return true;
}

AudioPlan::AudioPlan(const Timeline &timeline, AudioSettings settings,
                     std::span<const VoiceProfile> profiles,
                     bool editableVoices)
    : settings_(settings), editableVoices_(editableVoices) {
    if (settings.sampleRate < 22050 || settings.sampleRate > 192000 ||
        settings.rootMidi < 48 || settings.rootMidi > 72 ||
        !std::isfinite(settings.attackSeconds) ||
        settings.attackSeconds < .002 || settings.attackSeconds > .100 ||
        !std::isfinite(settings.decaySeconds) || settings.decaySeconds < .100 ||
        settings.decaySeconds > 3.0 || !std::isfinite(settings.masterDb) ||
        settings.masterDb < -60 || settings.masterDb > 0 ||
        !validCompositionSettings(settings.composition) ||
        !validSpatialSettings(settings.spatial))
        throw std::invalid_argument("audio settings outside revision-1 limits");
    if (timeline.durationUs() < 0 || timeline.durationUs() > maximumDurationUs)
        throw std::invalid_argument(
            "audio preview is limited to 30 minutes before tails; choose a "
            "faster replay speed");
    const auto frames = [&](double seconds) {
        return static_cast<std::uint32_t>(
            std::ceil(seconds * settings.sampleRate));
    };
    attackFrames_ = frames(settings.attackSeconds);
    decayFrames_ = frames(settings.decaySeconds);
    releaseFrames_ = frames(.020);
    fadeFrames_ = frames(.005);
    decayFactor_ = std::exp(-std::log(1000.0) / decayFrames_);
    masterGain_ = std::pow(10.0, settings.masterDb / 20.0);
    maximumPhaseStep_ =
        2 * pi * 440 *
        std::exp2(((settings.composition.enabled
                        ? 88.0
                        : static_cast<double>(settings.rootMidi) + 16) -
                   69) /
                  12.0) /
        settings.sampleRate;
    minimumPitchMultiplier_ =
        std::max(.5, std::exp2((48.0 - settings.rootMidi) / 12));
    maximumPitchMultiplier_ =
        std::min(2.0, std::exp2((72.0 - settings.rootMidi) / 12));
    voiceBank_[0] = {
        attackFrames_, decayFrames_, releaseFrames_, 1, decayFactor_, 1};
    if (settings.composition.enabled) {
        const auto maximumBase = std::min(76u, settings.rootMidi + 16);
        for (unsigned pitchClass = 0; pitchClass < 12; ++pitchClass) {
            const auto maximumForClass =
                maximumBase - ((maximumBase - pitchClass + 12) % 12);
            maximumScalePitchMultipliers_[pitchClass] =
                std::exp2((88.0 - maximumForClass) / 12);
        }
        VoiceSettings fallback;
        fallback.attackSeconds = settings.attackSeconds;
        fallback.decaySeconds = settings.decaySeconds;
        voiceBank_[0] =
            coefficientsFor(fallback, settings.sampleRate, settings.rootMidi,
                            settings.composition);
    }
    if (profiles.size() > voiceSlotCapacity)
        throw std::invalid_argument(
            "voice bank is limited to 512 species profiles");
    voiceProfiles_.reserve(profiles.size());
    for (const auto &profile : profiles) {
        const auto root = static_cast<std::int64_t>(settings.rootMidi) +
                          profile.settings.tuningSemitones;
        if ((voiceProfiles_.empty() != profile.speciesId.empty()) ||
            !validVoiceSettings(profile.settings) || root < 48 || root > 72 ||
            std::any_of(voiceProfiles_.begin(), voiceProfiles_.end(),
                        [&](const VoiceProfile &existing) {
                            return existing.speciesId == profile.speciesId;
                        }))
            throw std::invalid_argument(
                "invalid or duplicate species voice profile");
        voiceBank_[voiceProfiles_.size()] =
            coefficientsFor(profile.settings, settings.sampleRate,
                            settings.rootMidi, settings.composition);
        voiceProfiles_.push_back(profile);
    }
    const auto maximumVoiceFrames = frames(.100) + frames(3.0) + releaseFrames_;
    totalFrames_ = frameAt(timeline.durationUs(), settings.sampleRate);
    if (settings.composition.enabled) {
        const auto compiled = compileComposition(timeline, settings.composition,
                                                 settings.rootMidi);
        omittedCompositionNotes_ = compiled.omittedNotes;
        actions_.reserve(compiled.notes.size());
        decisions_.reserve(compiled.decisions.size());
        for (const auto &decision : compiled.decisions) {
            decisions_.push_back(
                {decision.eventIndex,
                 decision.admitted ? Admission::accepted : Admission::density});
            const auto &event = timeline.events()[decision.eventIndex];
            if (decision.admitted &&
                (!event.speciesId || event.speciesId->empty()))
                ++missingSpecies_;
        }
        const auto limit = frameAt(maximumDurationUs, settings.sampleRate);
        for (const auto &note : compiled.notes) {
            const auto &event = timeline.events()[note.eventIndex];
            const auto slot =
                event.speciesId ? voiceSlot(*event.speciesId) : VoiceSlot{};
            const auto frame = frameAt(note.dueUs, settings.sampleRate);
            const auto gate = static_cast<std::uint32_t>(
                frameAt(note.gateUs, settings.sampleRate));
            const auto &coefficients = voiceBank_[slot];
            const auto tail =
                editableVoices_
                    ? maximumVoiceFrames
                    : std::min(coefficients.attackFrames +
                                   coefficients.decayFrames,
                               std::max(coefficients.attackFrames, gate)) +
                          coefficients.releaseFrames;
            if (frame > limit || tail > limit - frame)
                throw std::invalid_argument("composition including voice tails "
                                            "exceeds 30 minute limit");
            const auto frequency =
                440 * std::exp2((static_cast<double>(note.midiNote) - 69) / 12);
            actions_.push_back({frame, 2 * pi * frequency / settings.sampleRate,
                                note.eventIndex, note.midiNote, slot,
                                note.phaseSeed, note.velocity, gate});
            totalFrames_ = std::max(totalFrames_, frame + tail);
        }
    } else {
        actions_.reserve(timeline.schedule().size());
        decisions_.reserve(timeline.schedule().size());
        Microseconds window = -1;
        unsigned admitted{};
        for (const auto &item : timeline.schedule()) {
            const auto nextWindow = item.dueUs / 10000;
            if (nextWindow != window) {
                window = nextWindow;
                admitted = 0;
            }
            if (admitted == 4) {
                decisions_.push_back({item.eventIndex, Admission::density});
                continue;
            }
            ++admitted;
            const auto &event = timeline.events()[item.eventIndex];
            const bool hasSpecies =
                event.speciesId && !event.speciesId->empty();
            const auto voiceSlot =
                hasSpecies ? this->voiceSlot(*event.speciesId) : VoiceSlot{};
            if (!hasSpecies)
                ++missingSpecies_;
            const auto midi = directSpeciesNote(settings.rootMidi, hasSpecies ? std::string_view(*event.speciesId) : std::string_view{});
            const auto frequency =
                440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0);
            const auto frame = frameAt(item.dueUs, settings.sampleRate);
            actions_.push_back({frame, 2 * pi * frequency / settings.sampleRate,
                                item.eventIndex, midi, voiceSlot,
                                seedFor(event)});
            decisions_.push_back({item.eventIndex, Admission::accepted});
            totalFrames_ = std::max(
                totalFrames_,
                frame + (editableVoices_ ? maximumVoiceFrames
                                         : lifetime(voiceBank_[voiceSlot])));
        }
    }
    bool hasWetAction{};
    std::uint64_t lastVoiceEnd{};
    if (settings.spatial.enabled) {
        const auto routes =
            prepareSpatialRoutes(settings.spatial, timeline.events());
        for (auto &action : actions_) {
            action.route = routes[action.eventIndex];
            const auto region = action.route.regionSlot;
            hasWetAction =
                hasWetAction ||
                (region && settings.spatial.regions[region - 1].send > 0);
            const auto &coefficients = voiceBank_[action.voiceSlot];
            auto tail = lifetime(coefficients);
            if (action.gateFrames)
                tail = std::min(coefficients.attackFrames +
                                    coefficients.decayFrames,
                                std::max(coefficients.attackFrames,
                                         action.gateFrames)) +
                       coefficients.releaseFrames;
            lastVoiceEnd = std::max(
                lastVoiceEnd,
                action.frame + (editableVoices_ ? maximumVoiceFrames : tail));
        }
    }
    spatialPlan_ =
        prepareSpatialPlan(settings.spatial, settings.sampleRate, hasWetAction);
    if (spatialPlan_.wetActive) {
        totalFrames_ =
            std::max(totalFrames_, lastVoiceEnd + 12ULL * settings.sampleRate);
        spatialEndFadeFrames_ = frames(.020);
    }
    if (settings.spatial.enabled &&
        totalFrames_ > frameAt(maximumDurationUs, settings.sampleRate))
        throw std::invalid_argument("spatial plan including voice and effect "
                                    "tails exceeds 30 minute limit");
}

AudioRenderer::AudioRenderer(const AudioPlan &plan)
    : plan_(plan), spatial_(plan.spatialPlan_), voiceBank_(plan.voiceBank_) {}
bool AudioRenderer::updateVoice(
    VoiceSlot slot, const VoiceCoefficients &coefficients) noexcept {
    if (!plan_.editableVoices_ || slot >= plan_.voiceProfiles_.size())
        return false;
    if (!validCoefficients(
            coefficients, plan_.settings_.sampleRate, plan_.releaseFrames_,
            plan_.maximumPhaseStep_, plan_.minimumPitchMultiplier_,
            plan_.maximumPitchMultiplier_, plan_.settings_.composition.enabled,
            plan_.maximumScalePitchMultipliers_))
        return false;
    voiceBank_[slot] = coefficients;
    return true;
}
void AudioRenderer::clearVoices() noexcept {
    spatial_.reset();
    voices_ = {};
    retiring_ = {};
}
void AudioRenderer::clearToStart() noexcept {
    clearVoices();
    frame_ = 0;
    nextAction_ = 0;
    gain_ = 1;
    rampRemaining_ = 0;
    state_ = AudioState::ready;
}
void AudioRenderer::beginRamp(double target) noexcept {
    rampFrom_ = gain_;
    rampTo_ = target;
    rampRemaining_ = plan_.fadeFrames_;
}
bool AudioRenderer::play(std::uint64_t generation) noexcept {
    if (generation != generation_ ||
        (state_ != AudioState::ready && state_ != AudioState::paused))
        return false;
    if (state_ == AudioState::paused)
        beginRamp(1);
    state_ = AudioState::playing;
    return true;
}
bool AudioRenderer::pause(std::uint64_t generation) noexcept {
    if (generation != generation_ || state_ != AudioState::playing)
        return false;
    beginRamp(0);
    state_ = AudioState::pausing;
    return true;
}
bool AudioRenderer::stop(std::uint64_t generation) noexcept {
    if (generation != generation_)
        return false;
    ++generation_;
    if (state_ == AudioState::playing || state_ == AudioState::pausing ||
        state_ == AudioState::draining) {
        beginRamp(0);
        state_ = AudioState::draining;
    } else {
        clearToStart();
    }
    return true;
}
bool AudioRenderer::panic(std::uint64_t generation) noexcept {
    if (generation != generation_)
        return false;
    ++generation_;
    clearToStart();
    return true;
}
bool AudioRenderer::seek(std::uint64_t frame,
                         std::uint64_t generation) noexcept {
    if (generation != generation_ || frame > plan_.totalFrames_ ||
        (state_ != AudioState::ready && state_ != AudioState::paused &&
         state_ != AudioState::finished))
        return false;
    clearVoices();
    frame_ = frame;
    ++generation_;
    gain_ = 0;
    rampRemaining_ = 0;
    const auto it =
        std::lower_bound(plan_.actions_.begin(), plan_.actions_.end(), frame,
                         [](const MusicalAction &action, std::uint64_t value) {
                             return action.frame < value;
                         });
    nextAction_ = static_cast<std::size_t>(it - plan_.actions_.begin());
    state_ =
        frame == plan_.totalFrames_ ? AudioState::finished : AudioState::paused;
    return true;
}
bool AudioRenderer::triggerLive(VoiceSlot slot, unsigned midiNote,
                                double phaseStep, double velocity,
                                std::uint32_t gateFrames) noexcept {
    if (!live_ || state_ != AudioState::playing ||
        slot >= std::max(std::size_t{1}, plan_.voiceProfiles_.size()) ||
        midiNote < plan_.settings_.rootMidi ||
        midiNote > plan_.settings_.rootMidi + 16 ||
        !std::isfinite(phaseStep) || phaseStep <= 0 ||
        phaseStep > plan_.maximumPhaseStep_ ||
        !std::isfinite(velocity) || velocity <= 0 || velocity > 1 ||
        gateFrames > plan_.settings_.sampleRate * 4u)
        return false;
    MusicalAction action;
    action.frame = frame_;
    action.voiceSlot = slot;
    action.midiNote = midiNote;
    action.phaseStep = phaseStep;
    action.velocity = velocity;
    action.gateFrames = gateFrames;
    action.phaseSeed = static_cast<std::uint32_t>(frame_) ^ (static_cast<std::uint32_t>(slot) * 2654435761u);
    return trigger(action);
}
bool AudioRenderer::trigger(const MusicalAction &action) noexcept {
    auto slot = std::find_if(voices_.begin(), voices_.end(),
                             [](const Voice &voice) { return !voice.active; });
    if (slot == voices_.end()) {
        slot = std::min_element(voices_.begin(), voices_.end(),
                                [](const Voice &a, const Voice &b) {
                                    return a.started < b.started;
                                });
        const auto tail =
            std::find_if(retiring_.begin(), retiring_.end(),
                         [](const Voice &voice) { return !voice.active; });
        if (tail == retiring_.end()) {
            ++statistics_.faults;
            return false;
        }
        *tail = *slot;
        tail->fadeAge = 0;
        ++statistics_.stolen;
    }
    const auto &coefficients = voiceBank_[action.voiceSlot];
    *slot = {};
    slot->active = true;
    slot->started = action.frame;
    slot->phaseStep =
        action.phaseStep *
        (coefficients.scaleQuantized
             ? coefficients.scalePitchMultipliers[action.midiNote % 12]
             : coefficients.pitchMultiplier);
    slot->velocity = action.velocity;
    slot->route = action.route;
    slot->releaseStart = coefficients.attackFrames + coefficients.decayFrames;
    if (action.gateFrames)
        slot->releaseStart =
            std::min(slot->releaseStart,
                     std::max(coefficients.attackFrames, action.gateFrames));
    slot->endAge = slot->releaseStart + coefficients.releaseFrames;
    slot->coefficients = coefficients;
    slot->partialGains.fill(1);
    if (!coefficients.legacyStruck && coefficients.phaseSpread != 0) {
        auto seed = action.phaseSeed;
        for (std::size_t i = 0; i < coefficients.partialCount; ++i)
            slot->partialPhases[i] =
                (static_cast<double>(nextPhaseSeed(seed)) / 4294967296.0) *
                (2 * pi * coefficients.phaseSpread);
    }
    ++statistics_.triggered;
    return true;
}
double AudioRenderer::tick(Voice &voice, bool retiring) noexcept {
    if (!voice.active)
        return 0;
    const auto age = voice.age;
    const auto &coefficients = voice.coefficients;
    if (age >= voice.endAge ||
        (retiring && voice.fadeAge >= plan_.fadeFrames_)) {
        voice.active = false;
        return 0;
    }
    double envelope = voice.decay;
    if (age < coefficients.attackFrames)
        envelope *= (1 - std::cos(pi * age / coefficients.attackFrames)) * .5;
    const auto releaseStart = voice.releaseStart;
    if (age >= releaseStart)
        envelope *= (1 + std::cos(pi * (age - releaseStart) /
                                  coefficients.releaseFrames)) *
                    .5;
    if (retiring)
        envelope *= (1 + std::cos(pi * voice.fadeAge / plan_.fadeFrames_)) * .5;
    double sample{};
    if (coefficients.legacyStruck) {
        // Preserve the historical expression and update order exactly.
        sample = .05 * envelope *
                 (std::sin(voice.phase) + .25 * std::sin(2 * voice.phase) +
                  .08 * std::sin(3 * voice.phase)) /
                 1.33 * coefficients.gain;
    } else {
        double tone{};
        for (std::size_t i = 0; i < coefficients.partialCount; ++i) {
            const auto &partial = coefficients.partials[i];
            tone += partial.amplitude * voice.partialGains[i] *
                    std::sin(voice.partialPhases[i]);
            voice.partialPhases[i] +=
                voice.phaseStep * voice.sweep * partial.ratio;
            if (voice.partialPhases[i] >= 2 * pi)
                voice.partialPhases[i] -= 2 * pi;
            voice.partialGains[i] *= partial.decayFactor;
        }
        double pulse = 1;
        if (coefficients.pulsePhaseStep != 0) {
            const auto raisedCosine = .5 + .5 * std::cos(voice.pulsePhase);
            pulse = raisedCosine * raisedCosine;
            voice.pulsePhase += coefficients.pulsePhaseStep;
            if (voice.pulsePhase >= 2 * pi)
                voice.pulsePhase -= 2 * pi;
        }
        double roughness = 1;
        if (coefficients.roughnessDepth != 0) {
            roughness = 1 - .5 * coefficients.roughnessDepth +
                        .5 * coefficients.roughnessDepth *
                            std::cos(voice.roughnessPhase);
            voice.roughnessPhase += coefficients.roughnessPhaseStep;
            if (voice.roughnessPhase >= 2 * pi)
                voice.roughnessPhase -= 2 * pi;
        }
        sample = .05 * envelope * tone * pulse * roughness * coefficients.gain;
        if (coefficients.sweepFrames > 1 &&
            age < coefficients.sweepFrames - 1) {
            voice.sweep =
                age + 1 == coefficients.sweepFrames - 1
                    ? coefficients.sweepEnd
                    : std::clamp(voice.sweep * coefficients.sweepFactor,
                                 std::min(1.0, coefficients.sweepEnd),
                                 std::max(1.0, coefficients.sweepEnd));
        }
    }
    if (voice.velocity != 1)
        sample *= voice.velocity;
    if (!std::isfinite(sample)) {
        voice = {};
        ++statistics_.faults;
        return 0;
    }
    voice.phase += voice.phaseStep;
    if (voice.phase >= 2 * pi)
        voice.phase -= 2 * pi;
    if (age >= coefficients.attackFrames)
        voice.decay *= coefficients.decayFactor;
    ++voice.age;
    if (retiring)
        ++voice.fadeAge;
    // Release completed states before the next sample's admissions need their
    // slots.
    if (voice.age >= voice.endAge ||
        (retiring && voice.fadeAge >= plan_.fadeFrames_))
        voice.active = false;
    return sample;
}
bool AudioRenderer::process(std::span<float> output) noexcept {
    if (output.size() % 2 || output.size() > maxBlockFrames * 2)
        return false;
    for (std::size_t i = 0; i < output.size(); i += 2) {
        output[i] = output[i + 1] = 0;
        if (state_ == AudioState::ready || state_ == AudioState::paused ||
            state_ == AudioState::finished)
            continue;
        if (!live_ && frame_ >= plan_.totalFrames_) {
            if (state_ == AudioState::draining)
                clearToStart();
            else {
                clearVoices();
                state_ = AudioState::finished;
            }
            continue;
        }
        if (!live_ && state_ != AudioState::draining) {
            for (unsigned step = 0;
                 step < 8 && nextAction_ < plan_.actions_.size() &&
                 plan_.actions_[nextAction_].frame == frame_;
                 ++step)
                trigger(plan_.actions_[nextAction_++]);
        }
        double sample{};
        StereoSample stereo;
        if (plan_.spatialPlan_.enabled) {
            std::array<double, spatialRegionCapacity> buses{};
            const auto mixVoice = [&](Voice &voice, bool retiring) {
                const auto value =
                    tick(voice, retiring) * voice.route.regionGain;
                stereo.left += value * voice.route.leftGain;
                stereo.right += value * voice.route.rightGain;
                if (voice.route.regionSlot)
                    buses[voice.route.regionSlot - 1] += value;
            };
            for (auto &voice : voices_)
                mixVoice(voice, false);
            for (auto &voice : retiring_)
                mixVoice(voice, true);
            if (plan_.spatialPlan_.wetActive) {
                const auto faults = spatial_.faults();
                const auto wet = spatial_.process(buses);
                statistics_.faults += spatial_.faults() - faults;
                stereo.left =
                    (stereo.left + plan_.spatialPlan_.amount * wet.left) *
                    plan_.spatialPlan_.outputGain;
                stereo.right =
                    (stereo.right + plan_.spatialPlan_.amount * wet.right) *
                    plan_.spatialPlan_.outputGain;
                const auto remaining = plan_.totalFrames_ - frame_;
                if (!live_ && remaining <= plan_.spatialEndFadeFrames_) {
                    const auto fade = static_cast<double>(remaining - 1) /
                                      plan_.spatialEndFadeFrames_;
                    stereo.left *= fade;
                    stereo.right *= fade;
                }
            }
        } else {
            for (auto &voice : voices_)
                sample += tick(voice, false);
            for (auto &voice : retiring_)
                sample += tick(voice, true);
        }
        if (rampRemaining_) {
            --rampRemaining_;
            const auto fraction =
                1 - static_cast<double>(rampRemaining_) / plan_.fadeFrames_;
            gain_ = rampFrom_ +
                    (rampTo_ - rampFrom_) * (1 - std::cos(pi * fraction)) * .5;
        }
        if (plan_.spatialPlan_.enabled) {
            const auto outputGain = gain_ * plan_.masterGain_;
            stereo.left *= outputGain;
            stereo.right *= outputGain;
            if (!std::isfinite(stereo.left) || !std::isfinite(stereo.right) ||
                std::abs(stereo.left) > .800001 ||
                std::abs(stereo.right) > .800001) {
                stereo = {};
                clearVoices();
                ++statistics_.faults;
            }
            output[i] = static_cast<float>(stereo.left);
            output[i + 1] = static_cast<float>(stereo.right);
            statistics_.peak =
                std::max(statistics_.peak, std::max(std::abs(stereo.left),
                                                    std::abs(stereo.right)));
        } else {
            sample *= gain_ * plan_.masterGain_;
            if (!std::isfinite(sample) || std::abs(sample) > .800001) {
                sample = 0;
                clearVoices();
                ++statistics_.faults;
            }
            output[i] = output[i + 1] = static_cast<float>(sample);
            statistics_.peak = std::max(statistics_.peak, std::abs(sample));
        }
        ++frame_;
        if (!rampRemaining_ && state_ == AudioState::pausing)
            state_ = AudioState::paused;
        if (state_ == AudioState::draining &&
            (!rampRemaining_ || (!live_ && frame_ == plan_.totalFrames_))) {
            clearToStart();
        } else if (!live_ && frame_ == plan_.totalFrames_ &&
                   state_ != AudioState::paused) {
            clearVoices();
            state_ = AudioState::finished;
        }
    }
    return true;
}
} // namespace atlas
