#include <atlas/playback.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
thread_local bool inCallback{};
std::atomic<unsigned> callbackAllocations{}, callbackFrees{};
void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
    if (inCallback) ++callbackAllocations;
    void* result{};
    if (alignment > alignof(std::max_align_t)) {
        const auto size = bytes ? bytes : 1;
        if (size > std::numeric_limits<std::size_t>::max() - alignment + 1) throw std::bad_alloc{};
        result = std::aligned_alloc(alignment, ((size + alignment - 1) / alignment) * alignment);
    } else result = std::malloc(bytes ? bytes : 1);
    if (!result) throw std::bad_alloc{};
    return result;
}
void release(void* pointer) noexcept {
    if (pointer && inCallback) ++callbackFrees;
    std::free(pointer);
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class ManualDriver final : public atlas::OutputDriver {
public:
    std::atomic<bool> running{};
    std::atomic<unsigned> active{}, starts{}, stops{};
    bool failStart{}, failAfterCallback{}, wrongRate{};
    std::string start(atlas::PlaybackRuntime& runtime, std::uint32_t rate) override {
        ++starts;
        if (failStart) return "synthetic start failure";
        runtime.deviceStarted(wrongRate ? static_cast<double>(rate) + 1 : rate, 64, 2);
        running = true;
        if (failAfterCallback) return "synthetic partial start failure";
        return {};
    }
    void stop() noexcept override {
        ++stops; running = false;
        while (active.load() != 0) std::this_thread::yield();
    }
    bool pump(atlas::PlaybackRuntime& runtime, float* const* outputs, int channels = 2, int frames = 64) {
        if (!running.load()) return false;
        ++active;
        if (!running.load()) { --active; return false; }
        inCallback = true;
        runtime.process(outputs, channels, frames);
        inCallback = false;
        --active;
        return true;
    }
};
struct Fixture {
    ManualDriver* driver{};
    atlas::PlaybackRuntime runtime;
    std::array<float, 8192> left{}, right{};
    std::array<float*, 2> outputs{left.data(), right.data()};
    Fixture() : Fixture(std::make_unique<ManualDriver>()) {}
    explicit Fixture(std::unique_ptr<ManualDriver> next) : driver(next.get()), runtime(std::move(next)) {}
    bool pump(int frames = 64) { return driver->pump(runtime, outputs.data(), 2, frames); }
    atlas::PlaybackResult command(atlas::PlaybackCommand command) {
        runtime.heartbeat();
        auto pending = std::async(std::launch::async, [&] { return runtime.command(command); });
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            pump();
            check(std::chrono::steady_clock::now() < end, "command acknowledgement deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pending.get();
    }
    atlas::PlaybackResult command(atlas::PlaybackCommandKind kind, std::uint64_t frame = 0) {
        const auto s = runtime.snapshot();
        return command({kind, s.preparationId, s.generation, frame});
    }
};
atlas::Event event(int index, atlas::Microseconds at) {
    atlas::Event value;
    value.id = "playback:" + std::to_string(index); value.sourceId = "playback";
    value.kind = "acoustic_species_detection"; value.locationKind = "station_detection";
    value.stationId = "playback:station"; value.observedAt = at; value.firstSeenAt = at;
    value.revisionSequence = index;
    return value;
}
atlas::Timeline material(atlas::Microseconds end = 1000000) {
    return atlas::Timeline({event(1, 0), event(2, 100000)}, atlas::TimeBasis::observed,
                           {}, std::pair<atlas::Microseconds, atlas::Microseconds>{0, end});
}
void prepare(Fixture& f) {
    check(f.runtime.prepare(material()).ok, "prepare");
    check(f.runtime.enable(true).ok, "enable");
}
bool silent(const Fixture& f, std::size_t count = 64) {
    return std::all_of(f.left.begin(), f.left.begin() + static_cast<std::ptrdiff_t>(count), [](float v) { return v == 0; }) &&
           std::all_of(f.right.begin(), f.right.begin() + static_cast<std::ptrdiff_t>(count), [](float v) { return v == 0; });
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocate(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocate(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }

int main() {
    unsigned failures{};
    const auto test = [&](const char* name, auto body) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("preparation requires deliberate enable and never autoplays", [] {
        Fixture f;
        check(!f.runtime.enable(true).ok, "enabled without plan");
        const auto p = f.runtime.prepare(material()); check(p.ok, "prepare");
        check(p.snapshot.prepared && !p.snapshot.enabled && p.snapshot.positionFrames == 0, "prepared state");
        check(!f.command(atlas::PlaybackCommandKind::play).ok, "disabled playback accepted");
        check(f.runtime.enable(true).ok, "enable");
        f.left.fill(1); f.right.fill(1); f.pump();
        check(silent(f) && f.runtime.snapshot().positionFrames == 0, "enable started audio");
    });
    test("callback output equals existing renderer across device partitions", [] {
        Fixture f; prepare(f);
        // A seek gives an exact known phase boundary after the acknowledged play command.
        check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        check(f.command(atlas::PlaybackCommandKind::pause).ok, "pause");
        while (f.runtime.snapshot().state == atlas::AudioState::pausing) f.pump();
        check(f.command(atlas::PlaybackCommandKind::seek, 0).ok, "seek");
        atlas::AudioSettings settings; settings.masterDb = -18;
        const atlas::AudioPlan plan(material(), settings); atlas::AudioRenderer reference(plan);
        check(reference.seek(0, reference.generation()), "reference seek");
        check(reference.play(reference.generation()), "reference play");
        f.runtime.heartbeat();
        auto pending = std::async(std::launch::async, [&] {
            const auto s = f.runtime.snapshot();
            return f.runtime.command({atlas::PlaybackCommandKind::play, s.preparationId, s.generation, 0});
        });
        std::array<float, 8192> expected{};
        bool began{};
        for (unsigned block = 0; block < 1000; ++block) {
            const int frames = block % 3 == 0 ? 17 : block % 3 == 1 ? 257 : 64;
            f.pump(frames);
            if (f.runtime.snapshot().state == atlas::AudioState::playing) began = true;
            if (began) {
                check(reference.process(std::span<float>(expected).first(static_cast<std::size_t>(frames) * 2)), "reference process");
                for (int i = 0; i < frames; ++i) {
                    check(f.left[static_cast<std::size_t>(i)] == expected[static_cast<std::size_t>(i) * 2], "left PCM drift");
                    check(f.right[static_cast<std::size_t>(i)] == expected[static_cast<std::size_t>(i) * 2 + 1], "right PCM drift");
                }
                if (reference.position() > 12000) break;
            }
            if (!began) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else std::this_thread::yield();
        }
        check(began && pending.get().ok && reference.position() > 12000, "playback never began");
    });
    test("pause freezes callback position; stop and panic advance generations", [] {
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        check(f.command(atlas::PlaybackCommandKind::pause).ok, "pause");
        for (unsigned i = 0; i < 10; ++i) f.pump();
        const auto paused = f.runtime.snapshot(); check(paused.state == atlas::AudioState::paused, "pause endpoint");
        f.left.fill(1); f.right.fill(1); f.pump();
        check(silent(f) && f.runtime.snapshot().positionFrames == paused.positionFrames, "paused playback advances");
        check(f.command(atlas::PlaybackCommandKind::seek, 1234).ok, "seek");
        const auto sought = f.runtime.snapshot(); check(sought.positionFrames == 1234 && sought.generation > paused.generation, "seek generation or position");
        const auto stale = f.command({atlas::PlaybackCommandKind::play, paused.preparationId, paused.generation, 0});
        check(!stale.ok && stale.error == atlas::PlaybackError::staleGeneration, "stale command accepted");
        check(f.command(atlas::PlaybackCommandKind::stop).ok, "stop");
        check(f.runtime.snapshot().positionFrames == 0 && f.runtime.snapshot().generation > sought.generation, "stop result");
        check(f.command(atlas::PlaybackCommandKind::play).ok, "resume");
        const auto generation = f.runtime.snapshot().generation;
        check(f.command({atlas::PlaybackCommandKind::panic, 0, 0, 0}).ok, "unconditional panic");
        f.left.fill(1); f.right.fill(1); f.pump();
        check(silent(f) && f.runtime.snapshot().positionFrames == 0 && f.runtime.snapshot().generation > generation, "panic did not clear");
    });
    test("plan replacement retires off callback and invalidates old preparation", [] {
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        const auto old = f.runtime.snapshot();
        const auto replacement = f.runtime.prepare(material(2000000)); check(replacement.ok, "replace");
        check(!replacement.snapshot.enabled && replacement.snapshot.positionFrames == 0 && replacement.snapshot.preparationId != old.preparationId, "replacement state");
        check(f.driver->active == 0 && !f.driver->running && f.driver->stops > 0, "driver not quiescent");
        check(f.runtime.enable(true).ok, "reenable");
        const auto stale = f.command({atlas::PlaybackCommandKind::play, old.preparationId, old.generation, 0});
        check(!stale.ok && stale.error == atlas::PlaybackError::stalePreparation, "old plan command accepted");
        for (unsigned i = 0; i < 25; ++i) {
            check(f.runtime.prepare(material(i % 2 ? 1000000 : 2000000)).ok, "repeated replace");
            check(f.runtime.enable(true).ok, "repeated enable");
            check(f.command(atlas::PlaybackCommandKind::play).ok, "repeated play");
        }
    });
    test("failed preparation retains identity and disables audio", [] {
        Fixture f; prepare(f); const auto before = f.runtime.snapshot();
        atlas::AudioSettings invalid; invalid.masterDb = std::numeric_limits<double>::quiet_NaN();
        bool rejected{};
        try { rejected = !f.runtime.prepare(material(), invalid).ok; } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid settings accepted");
        const auto after = f.runtime.snapshot();
        check(after.preparationId == before.preparationId && after.totalFrames == before.totalFrames, "failed preparation replaced plan");
        check(!after.enabled && !f.driver->running, "failed preparation left output enabled");
    });
    test("partial start failure and disable are recoverable and quiescent", [] {
        Fixture f; check(f.runtime.prepare(material()).ok, "prepare");
        f.driver->failAfterCallback = true;
        check(!f.runtime.enable(true).ok, "partial start accepted");
        check(!f.driver->running && !f.runtime.snapshot().enabled && f.driver->stops > 0, "failed start not retired");
        f.driver->failAfterCallback = false;
        check(f.runtime.enable(true).ok, "start retry");
        check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        check(f.runtime.enable(false).ok, "disable");
        check(!f.driver->running && f.driver->active == 0 && !f.runtime.snapshot().enabled, "disable left callbacks");
        check(f.runtime.enable(true).ok && f.runtime.enable(false).ok, "repeat lifecycle");
    });
    test("device fault and invalid callback produce silence", [] {
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        f.left.fill(1); f.right.fill(1);
        f.driver->pump(f.runtime, f.outputs.data(), 2, 4097);
        check(silent(f, 4097), "oversized callback leaked output");
        f.runtime.service();
        check(!f.runtime.snapshot().enabled, "callback fault still enabled");
        check(f.runtime.enable(true).ok, "callback fault recovery");
        f.runtime.deviceFailed(); f.left.fill(1); f.right.fill(1); f.pump();
        check(silent(f), "device loss leaked sound"); f.runtime.service();
        check(!f.runtime.snapshot().enabled, "device loss still enabled");
    });
    test("missing callbacks time out safely and can restart", [] {
        Fixture f; prepare(f);
        const auto s = f.runtime.snapshot();
        const auto failed = f.runtime.command({atlas::PlaybackCommandKind::play, s.preparationId, s.generation, 0});
        check(!failed.ok && failed.error == atlas::PlaybackError::commandTimeout, "missing callback not reported");
        check(!f.driver->running && !f.runtime.snapshot().enabled, "timeout failed to quiesce");
        check(f.runtime.enable(true).ok && f.command(atlas::PlaybackCommandKind::play).ok, "timeout recovery");
    });
    test("callback lease expires without worker heartbeat", [] {
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        for (unsigned i = 0; i < 60; ++i) f.pump(4096);
        const auto expired = f.runtime.snapshot();
        check(expired.error == atlas::PlaybackError::leaseExpired && !expired.enabled && silent(f, 4096), "lease did not silence");
        f.runtime.service(); check(!f.driver->running, "expired lease not closed");
        check(f.runtime.enable(true).ok, "lease recovery");
    });
    test("unsupported device and complete start failure remain observable", [] {
        Fixture f; check(f.runtime.prepare(material()).ok, "prepare");
        f.driver->failStart = true;
        check(f.runtime.enable(true).error == atlas::PlaybackError::deviceUnavailable, "missing device error");
        f.driver->failStart = false; f.driver->wrongRate = true;
        check(f.runtime.enable(true).error == atlas::PlaybackError::unsupportedDevice, "sample-rate mismatch accepted");
        check(!f.driver->running && !f.runtime.snapshot().enabled, "unsupported device left running");
        f.driver->wrongRate = false; check(f.runtime.enable(true).ok, "device retry");
    });
    test("concurrent callback replacement establishes quiescence", [] {
        Fixture f; prepare(f);
        std::atomic<bool> done{};
        std::thread callbacks([&] {
            std::array<float, 128> left{}, right{};
            std::array<float*, 2> outputs{left.data(), right.data()};
            while (!done.load()) {
                f.driver->pump(f.runtime, outputs.data());
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
        bool valid = true;
        for (unsigned i = 0; i < 100; ++i) {
            const auto previous = f.runtime.snapshot().preparationId;
            const auto replaced = f.runtime.prepare(material(i % 2 ? 1000000 : 2000000));
            if (!replaced.ok || replaced.snapshot.preparationId == previous || replaced.snapshot.enabled || f.driver->active != 0)
                valid = false;
            if (!f.runtime.enable(true).ok) valid = false;
            const auto s = f.runtime.snapshot();
            if (!f.runtime.command({atlas::PlaybackCommandKind::play, s.preparationId, s.generation, 0}).ok) valid = false;
        }
        f.runtime.enable(false); done = true; callbacks.join();
        check(valid && f.driver->active == 0 && !f.driver->running, "replacement crossed active callback");
    });
    test("coherent snapshot reader while callback progresses", [] {
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        const auto identity = f.runtime.snapshot();
        std::atomic<bool> done{}, invalid{};
        std::thread observer([&] {
            std::uint64_t previous{};
            while (!done.load()) {
                const auto s = f.runtime.snapshot();
                if (!s.prepared || s.preparationId != identity.preparationId || s.generation != identity.generation ||
                    s.sampleRate != 48000 || s.totalFrames != identity.totalFrames || s.positionFrames > s.totalFrames || s.positionFrames < previous)
                    invalid = true;
                previous = s.positionFrames;
            }
        });
        for (unsigned i = 0; i < 2000; ++i) { f.runtime.heartbeat(); f.pump(); }
        done = true; observer.join(); check(!invalid.load(), "snapshot fields incoherent");
    });
    test("callback has no C++ allocation or deallocation", [] {
        const auto allocationsBefore = callbackAllocations.load(), freesBefore = callbackFrees.load();
        Fixture f; prepare(f); check(f.command(atlas::PlaybackCommandKind::play).ok, "play");
        for (unsigned i = 0; i < 1000; ++i) { f.runtime.heartbeat(); f.pump(); }
        check(f.command(atlas::PlaybackCommandKind::stop).ok, "stop");
        check(f.command(atlas::PlaybackCommandKind::panic).ok, "panic");
        check(callbackAllocations == allocationsBefore && callbackFrees == freesBefore, "callback allocated or destroyed ownership");
    });
    return failures ? 1 : 0;
}
