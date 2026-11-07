#include <atlas/playback.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace atlas;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
class Driver final : public OutputDriver {
public:
    std::string start(PlaybackRuntime &runtime, std::uint32_t rate) override { runtime.deviceStarted(rate, 128, 2); return {}; }
    void stop() noexcept override {}
};
struct Harness {
    PlaybackRuntime runtime{std::make_unique<Driver>()};
    std::array<float, 128> left{}, right{};
    double energy{};
    void tick() {
        float *outputs[]{left.data(), right.data()};
        runtime.process(outputs, 2, 128);
        for (auto sample : left) { check(std::isfinite(sample), "finite output"); energy += std::abs(sample); }
    }
    template<class Function> PlaybackResult deliver(Function function) {
        auto result = std::async(std::launch::async, function);
        // Control worker and callback run separately as in the actual driver.
        while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) tick();
        return result.get();
    }
    void start(AudioSettings settings = {48000, 60, .008, .650, -18}) {
        auto prepared = runtime.prepareLive(settings);
        check(prepared.ok && prepared.snapshot.live, "prepare continuous live");
        const auto enabled = runtime.enable(true);
        check(enabled.ok, "enable fake output");
        check(deliver([&] { return runtime.command({PlaybackCommandKind::play, enabled.snapshot.preparationId, enabled.snapshot.generation}); }).ok, "play continuous live");
    }
};
double sound(VoiceCharacter character) {
    Harness h; h.start();
    VoiceSettings voice; voice.character = character;
    check(h.deliver([&] { return h.runtime.submitLive(voice, 60); }).ok, "fresh species onset accepted");
    for (int i=0;i<80;++i) h.tick();
    check(h.runtime.snapshot().state == AudioState::playing, "continuous renderer survives empty timeline end");
    check(h.energy > .01, "live callback produces PCM");
    return h.energy;
}
}
int main() {
    try {
        const auto struck = sound(VoiceCharacter::struck);
        const auto bell = sound(VoiceCharacter::bell);
        check(std::abs(struck-bell) > .01, "resolved species character changes PCM");
        for (unsigned tonic = 0; tonic < 12; ++tonic) {
            for (auto scale : {CompositionScale::majorPentatonic,
                               CompositionScale::minorPentatonic,
                               CompositionScale::major, CompositionScale::naturalMinor}) {
                CompositionSettings music;
                music.tonicPitchClass = tonic;
                music.scale = scale;
                for (auto species : {"", "bird-a", "bird-b", "whale", "fox"}) {
                    const auto pitch = musicalSpeciesNote(60, species, music);
                    check(pitch >= 60 && pitch <= 76 && compositionScaleContains(music, static_cast<int>(pitch)),
                          "live species pitch follows every key and scale");
                }
            }
        }
        Harness spatial;
        AudioSettings settings{48000, 60, .008, .650, -18};
        settings.spatial.enabled = true;
        settings.spatial.amount = .8;
        RegionSettings region;
        region.id = "west"; region.name = "West"; region.pan = -.7;
        region.send = .9;
        settings.spatial.regions.push_back(region);
        settings.composition.enabled = true;
        spatial.start(settings);
        check(spatial.runtime.snapshot().wetActive, "empty live plan preallocates regional tails");
        VoiceSettings shortVoice;
        shortVoice.decaySeconds = .1;
        check(spatial.deliver([&] { return spatial.runtime.submitLive(shortVoice, 60, 1, 0, Position{0, 0}); }).ok,
              "live onset accepts geographic routing and musical settings");
        double leftEnergy{}, rightEnergy{}, tailEnergy{};
        double largestLeftPeak{};
        for (int block = 0; block < 800; ++block) {
            spatial.runtime.heartbeat();
            spatial.tick();
            for (std::size_t i = 0; i < spatial.left.size(); ++i) {
                leftEnergy += std::abs(spatial.left[i]);
                rightEnergy += std::abs(spatial.right[i]);
                if (block > 80) tailEnergy += std::abs(spatial.left[i]) + std::abs(spatial.right[i]);
            }
            const auto meter = spatial.runtime.snapshot();
            const auto actualLeftPeak = std::abs(*std::max_element(spatial.left.begin(), spatial.left.end(),
                [](float a, float b) { return std::abs(a) < std::abs(b); }));
            const auto actualRightPeak = std::abs(*std::max_element(spatial.right.begin(), spatial.right.end(),
                [](float a, float b) { return std::abs(a) < std::abs(b); }));
            check(meter.outputPeakLeft >= actualLeftPeak && meter.outputPeakRight >= actualRightPeak,
                  "stereo meter covers actual output PCM peaks");
            largestLeftPeak = std::max(largestLeftPeak, meter.outputPeakLeft);
            if (block == 200)
                check(spatial.runtime.snapshot().outputPeakLeft >= largestLeftPeak * .99,
                      "meter holds a short transient for one-second polling");
        }
        check(leftEnergy > rightEnergy && rightEnergy > 0 && tailEnergy > .001,
              "regional live note has stereo pan and audible-length wet tail");
        check(spatial.runtime.snapshot().outputPeakLeft < largestLeftPeak * .8,
              "output peak decays after hold");
        spatial.runtime.enable(false);
        check(spatial.runtime.snapshot().outputPeakLeft == 0 && spatial.runtime.snapshot().outputPeakRight == 0,
              "mute clears held output peaks immediately");
        // One callback owns a fixed eight-voice bank and eight retirement tails.
        AudioPlan plan(Timeline({}, TimeBasis::arrival));
        AudioRenderer renderer(plan);
        renderer.setLive(true);
        check(renderer.play(renderer.generation()), "start bounded renderer");
        const double phaseStep = 2 * 3.14159265358979323846 * 261.6255653005986 / 48000;
        for (unsigned i = 0; i < 16; ++i)
            check(renderer.triggerLive(0, 60, phaseStep, 1), "bounded onset accepted");
        check(!renderer.triggerLive(0, 60, phaseStep, 1), "overload rejected at fixed capacity");
        check(renderer.statistics().stolen == 8, "voice stealing is bounded");
        Harness immediate;
        immediate.start();
        for (std::uint64_t id = 1; id <= AudioRenderer::voiceCapacity + 1; ++id)
            check(immediate
                      .deliver([&] {
                          return immediate.runtime.submitLive(
                              VoiceSettings{}, 60, 1, 4800, {}, id);
                      })
                      .ok,
                  "synchronous live onset accepted");
        std::array<LiveVoiceStart, 9> receipts{};
        check(immediate.runtime.drainLiveStarts(receipts) == receipts.size(),
              "all immediate onsets reported");
        for (std::size_t i = 0; i < receipts.size(); ++i)
            check(receipts[i].token == i + 1 &&
                      receipts[i].endUs - receipts[i].onsetUs == 120000,
                  "actual gate and release determine the displayed duration");
        check(receipts.back().endedToken == receipts.front().token &&
                  receipts.back().shortenedEndUs - receipts.back().onsetUs ==
                      5000,
              "voice stealing shortens the correct token after its retirement "
              "fade");
        Harness timing;
        timing.start();
        LiveNoteInput timed;
        timed.id = 1;
        timed.midiNote = 60;
        timed.notBeforeFrame = timing.runtime.snapshot().positionFrames + 1000;
        check(
            timing.runtime
                    .submitLiveBatch(std::span<const LiveNoteInput>(&timed, 1))
                    .accepted == 1,
            "sample scheduled onset submitted without callback round trip");
        for (int i = 0; i < 7; ++i)
            timing.tick();
        check(timing.runtime.snapshot().liveAcknowledgedId == 0,
              "no onset before exact sample target");
        std::array<LiveVoiceStart, 16> starts{};
        check(timing.runtime.drainLiveStarts(starts) == 0,
              "queued note has no visual onset");
        timing.tick();
        check(timing.runtime.drainLiveStarts(starts) == 1 &&
                  starts[0].token == 1 &&
                  starts[0].endUs - starts[0].onsetUs == 678000 &&
                  starts[0].endedToken == 0,
              "callback publishes rendered note and exact dry voice end");
        check(timing.runtime.drainLiveStarts(starts) == 0,
              "confirmed onsets consumed once");
        check(timing.runtime.snapshot().liveAcknowledgedId == 1 &&
                  timing.runtime.snapshot().liveTriggeredNotes == 1,
              "onset starts inside callback block containing exact sample "
              "target");
        timing.runtime.deviceFailed();
        LiveNoteInput rejected = timed;
        rejected.id = 2;
        check(timing.runtime
                      .submitLiveBatch(
                          std::span<const LiveNoteInput>(&rejected, 1))
                      .accepted == 0,
              "device failure rejects asynchronous handoff");
        timing.runtime.service();
        check(timing.runtime.snapshot().error == PlaybackError::deviceChanged &&
                  timing.runtime.snapshot().liveAcknowledgedId == 1,
              "device failure stays observable and retains actual onset "
              "acknowledgement");
        Harness batch;
        batch.start();
        std::array<LiveNoteInput, 500> notes;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            notes[i].id = i + 1;
            notes[i].midiNote = 60;
            notes[i].voice.attackSeconds = .002;
        }
        auto submitted = batch.runtime.submitLiveBatch(notes);
        check(submitted.accepted > 0 && submitted.accepted < notes.size(),
              "batch handoff is bounded");
        check(batch.runtime.snapshot().liveAcknowledgedId == 0,
              "submission is not onset acknowledgement");
        std::size_t sent = submitted.accepted;
        unsigned blocks{};
        while (batch.runtime.snapshot().liveAcknowledgedId != notes.size() &&
               blocks++ < 5000) {
            batch.runtime.heartbeat();
            batch.tick();
            const auto next = batch.runtime.submitLiveBatch(
                std::span<const LiveNoteInput>(notes).subspan(sent));
            check(next.error == PlaybackError::none,
                  "full queue retries remain valid");
            sent += next.accepted;
        }
        check(sent == 500 && batch.runtime.snapshot().liveTriggeredNotes == 500,
              "500 submitted detections each actually trigger without drops");
        check(batch.energy > 1, "burst produces finite audible PCM");
        const auto acknowledged = batch.runtime.snapshot().liveAcknowledgedId;
        LiveNoteInput future;
        future.id = 501;
        future.midiNote = 60;
        future.notBeforeFrame = batch.runtime.snapshot().positionFrames + 48000;
        check(
            batch.runtime
                    .submitLiveBatch(std::span<const LiveNoteInput>(&future, 1))
                    .accepted == 1,
            "future note queued");
        for (int i = 0; i < 5; ++i)
            batch.tick();
        check(batch.runtime.snapshot().liveAcknowledgedId == acknowledged,
              "future onset respects sample position");
        check(batch
                  .deliver([&] {
                      return batch.runtime.command({PlaybackCommandKind::stop});
                  })
                  .ok,
              "stop cancels queued notes");
        check(batch.runtime.snapshot().liveAcknowledgedId == acknowledged,
              "stop retains actual onset acknowledgement");
        for (int i = 0;
             i < 16 && batch.runtime.snapshot().state == AudioState::draining;
             ++i)
            batch.tick();
        auto stopped = batch.runtime.snapshot();
        check(batch
                  .deliver([&] {
                      return batch.runtime.command({PlaybackCommandKind::play,
                                                    stopped.preparationId,
                                                    stopped.generation});
                  })
                  .ok,
              "restart after cancellation");
        for (int i = 0; i < 400; ++i) {
            batch.runtime.heartbeat();
            batch.tick();
        }
        check(batch.runtime.snapshot().liveTriggeredNotes == 500,
              "cancelled note cannot leak after restart");
        batch.runtime.enable(false);
        check(batch.runtime.snapshot().liveAcknowledgedId == acknowledged,
              "mute retains previous acknowledgements");
        batch.runtime.prepareLive();
        check(batch.runtime.snapshot().liveAcknowledgedId == 0,
              "new preparation resets acknowledgement domain");
        Harness muted;
        muted.start();
        VoiceSettings voice;
        check(muted.deliver([&] { return muted.runtime.submitLive(voice, 60); }).ok, "onset before mute");
        muted.runtime.enable(false); muted.energy = 0; muted.tick();
        check(muted.energy == 0, "mute clears an active live voice");
        Harness replay; replay.start();
        check(replay.deliver([&] { return replay.runtime.submitLive(voice, 60); }).ok, "onset before replay");
        replay.runtime.prepare(Timeline({}, TimeBasis::arrival));
        replay.energy = 0; replay.tick();
        check(replay.energy == 0 && !replay.runtime.snapshot().live, "replay clears an active live voice");
        Harness h; h.start();
        check(!h.deliver([&] { return h.runtime.submitLive(voice, 200); }).ok, "reject unsafe pitch");
        check(h.deliver([&] { return h.runtime.submitLive(voice, 60); }).ok, "accept valid onset after rejected input");
        check(h.deliver([&] { return h.runtime.command({PlaybackCommandKind::panic}); }).ok, "panic always accepted");
        h.energy=0; for(int i=0;i<4;++i) h.tick(); check(h.energy==0, "panic clears live tails");
        check(!h.runtime.submitLive(voice,60).ok,"panic requires explicit restart");
        h.runtime.enable(false); h.energy=0; h.tick(); check(h.energy==0,"mute is silent");
        h.runtime.prepare(Timeline({}, TimeBasis::arrival));
        check(!h.runtime.snapshot().live,"replay preparation exits live");
        check(!h.runtime.submitLive(voice,60).ok,"live note cannot leak into replay");
        Harness failed; failed.start();
        check(failed.deliver([&] { return failed.runtime.submitLive(voice,60); }).ok,"onset before device fault");
        failed.runtime.deviceFailed(); failed.energy=0; failed.tick(); check(failed.energy==0,"device fault silences active voices");
        failed.runtime.service(); check(!failed.runtime.snapshot().enabled,"fault closes output");
        std::cout << "live playback tests passed\n";
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
