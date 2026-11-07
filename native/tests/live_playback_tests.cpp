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
    void start() {
        auto prepared = runtime.prepareLive();
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
        Harness muted; muted.start();
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
