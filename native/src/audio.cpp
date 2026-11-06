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
std::uint32_t hashSpecies(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const auto byte : id) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 16777619u;
    }
    return hash;
}
} // namespace

bool validVoiceSettings(const VoiceSettings &settings) noexcept {
    return settings.tuningSemitones >= -12 && settings.tuningSemitones <= 12 &&
           std::isfinite(settings.attackSeconds) &&
           settings.attackSeconds >= .002 && settings.attackSeconds <= .100 &&
           std::isfinite(settings.decaySeconds) &&
           settings.decaySeconds >= .100 && settings.decaySeconds <= 3 &&
           std::isfinite(settings.gainDb) && settings.gainDb >= -60 &&
           settings.gainDb <= 0;
}

namespace {
VoiceCoefficients coefficientsFor(const VoiceSettings &settings,
                                  std::uint32_t rate) noexcept {
    VoiceCoefficients result;
    result.attackFrames =
        static_cast<std::uint32_t>(std::ceil(settings.attackSeconds * rate));
    result.decayFrames =
        static_cast<std::uint32_t>(std::ceil(settings.decaySeconds * rate));
    result.releaseFrames = static_cast<std::uint32_t>(std::ceil(.020 * rate));
    result.pitchMultiplier = std::exp2(settings.tuningSemitones / 12.0);
    result.decayFactor = std::exp(-std::log(1000.0) / result.decayFrames);
    result.gain = std::pow(10.0, settings.gainDb / 20.0);
    return result;
}
std::uint32_t lifetime(const VoiceCoefficients &coefficients) noexcept {
    return coefficients.attackFrames + coefficients.decayFrames +
           coefficients.releaseFrames;
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
    coefficients = coefficientsFor(settings, settings_.sampleRate);
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
        settings.masterDb < -60 || settings.masterDb > 0)
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
    voiceBank_[0] = {
        attackFrames_, decayFrames_, releaseFrames_, 1, decayFactor_, 1};
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
            coefficientsFor(profile.settings, settings.sampleRate);
        voiceProfiles_.push_back(profile);
    }
    const auto maximumVoiceFrames = frames(.100) + frames(3.0) + releaseFrames_;
    totalFrames_ = frameAt(timeline.durationUs(), settings.sampleRate);
    actions_.reserve(timeline.schedule().size());
    decisions_.reserve(timeline.schedule().size());
    constexpr std::array<unsigned, 8> offsets{0, 2, 4, 7, 9, 12, 14, 16};
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
        const bool hasSpecies = event.speciesId && !event.speciesId->empty();
        const auto voiceSlot =
            hasSpecies ? this->voiceSlot(*event.speciesId) : VoiceSlot{};
        const auto offset =
            hasSpecies ? offsets[hashSpecies(*event.speciesId) % offsets.size()]
                       : 0;
        if (!hasSpecies)
            ++missingSpecies_;
        const auto midi = settings.rootMidi + offset;
        const auto frequency =
            440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0);
        const auto frame = frameAt(item.dueUs, settings.sampleRate);
        actions_.push_back({frame, 2 * pi * frequency / settings.sampleRate,
                            item.eventIndex, midi, voiceSlot});
        decisions_.push_back({item.eventIndex, Admission::accepted});
        totalFrames_ = std::max(
            totalFrames_,
            frame + (editableVoices_ ? maximumVoiceFrames
                                     : lifetime(voiceBank_[voiceSlot])));
    }
}

AudioRenderer::AudioRenderer(const AudioPlan &plan) noexcept
    : plan_(plan), voiceBank_(plan.voiceBank_) {}
bool AudioRenderer::updateVoice(
    VoiceSlot slot, const VoiceCoefficients &coefficients) noexcept {
    if (!plan_.editableVoices_ || slot >= plan_.voiceProfiles_.size())
        return false;
    const auto rate = plan_.settings_.sampleRate;
    if (coefficients.attackFrames < (rate * 2 + 999) / 1000 ||
        coefficients.attackFrames > (rate + 9) / 10 ||
        coefficients.decayFrames < (rate + 9) / 10 ||
        coefficients.decayFrames > rate * 3 ||
        coefficients.releaseFrames != plan_.releaseFrames_ ||
        !std::isfinite(coefficients.pitchMultiplier) ||
        coefficients.pitchMultiplier < .5 || coefficients.pitchMultiplier > 2 ||
        !std::isfinite(coefficients.decayFactor) ||
        coefficients.decayFactor <= 0 || coefficients.decayFactor > 1 ||
        !std::isfinite(coefficients.gain) || coefficients.gain < .001 ||
        coefficients.gain > 1)
        return false;
    voiceBank_[slot] = coefficients;
    return true;
}
void AudioRenderer::clearVoices() noexcept {
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
void AudioRenderer::trigger(const MusicalAction &action) noexcept {
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
            return;
        }
        *tail = *slot;
        tail->fadeAge = 0;
        ++statistics_.stolen;
    }
    const auto &coefficients = voiceBank_[action.voiceSlot];
    *slot = Voice{true, action.frame,
                  0,    0,
                  0,    action.phaseStep * coefficients.pitchMultiplier,
                  1,    coefficients};
    ++statistics_.triggered;
}
double AudioRenderer::tick(Voice &voice, bool retiring) noexcept {
    if (!voice.active)
        return 0;
    const auto age = voice.age;
    const auto &coefficients = voice.coefficients;
    if (age >= lifetime(coefficients) ||
        (retiring && voice.fadeAge >= plan_.fadeFrames_)) {
        voice.active = false;
        return 0;
    }
    double envelope = voice.decay;
    if (age < coefficients.attackFrames)
        envelope *= (1 - std::cos(pi * age / coefficients.attackFrames)) * .5;
    const auto releaseStart =
        coefficients.attackFrames + coefficients.decayFrames;
    if (age >= releaseStart)
        envelope *= (1 + std::cos(pi * (age - releaseStart) /
                                  coefficients.releaseFrames)) *
                    .5;
    if (retiring)
        envelope *= (1 + std::cos(pi * voice.fadeAge / plan_.fadeFrames_)) * .5;
    const auto sample =
        .05 * envelope *
        (std::sin(voice.phase) + .25 * std::sin(2 * voice.phase) +
         .08 * std::sin(3 * voice.phase)) /
        1.33 * coefficients.gain;
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
    if (voice.age >= lifetime(coefficients) ||
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
        if (frame_ >= plan_.totalFrames_) {
            if (state_ == AudioState::draining)
                clearToStart();
            else {
                clearVoices();
                state_ = AudioState::finished;
            }
            continue;
        }
        if (state_ != AudioState::draining) {
            for (unsigned step = 0;
                 step < 8 && nextAction_ < plan_.actions_.size() &&
                 plan_.actions_[nextAction_].frame == frame_;
                 ++step)
                trigger(plan_.actions_[nextAction_++]);
        }
        double sample{};
        for (auto &voice : voices_)
            sample += tick(voice, false);
        for (auto &voice : retiring_)
            sample += tick(voice, true);
        if (rampRemaining_) {
            --rampRemaining_;
            const auto fraction =
                1 - static_cast<double>(rampRemaining_) / plan_.fadeFrames_;
            gain_ = rampFrom_ +
                    (rampTo_ - rampFrom_) * (1 - std::cos(pi * fraction)) * .5;
        }
        sample *= gain_ * plan_.masterGain_;
        if (!std::isfinite(sample) || std::abs(sample) > .800001) {
            sample = 0;
            clearVoices();
            ++statistics_.faults;
        }
        output[i] = output[i + 1] = static_cast<float>(sample);
        statistics_.peak = std::max(statistics_.peak, std::abs(sample));
        ++frame_;
        if (!rampRemaining_ && state_ == AudioState::pausing)
            state_ = AudioState::paused;
        if (state_ == AudioState::draining &&
            (!rampRemaining_ || frame_ == plan_.totalFrames_)) {
            clearToStart();
        } else if (frame_ == plan_.totalFrames_ &&
                   state_ != AudioState::paused) {
            clearVoices();
            state_ = AudioState::finished;
        }
    }
    return true;
}
} // namespace atlas
