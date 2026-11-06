#include <atlas/core.hpp>
#include <array>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
std::atomic<std::size_t> allocations{};
void* allocate(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
void expect(bool result) { if (!result) throw std::runtime_error("assertion failed"); }
template <class F> void rejects(F&& call) {
    bool thrown{};
    try { call(); } catch (const std::exception&) { thrown = true; }
    expect(thrown);
}
atlas::Event makeEvent(std::int64_t seq, atlas::Microseconds observed = 0, atlas::Microseconds arrival = 0) {
    atlas::Event value;
    value.id = "test:" + std::to_string(seq); value.sourceId = "test";
    value.kind = "acoustic_species_detection"; value.locationKind = "station_detection";
    value.stationId = "test:station:one";
    value.revisionSequence = seq; value.observedAt = observed; value.firstSeenAt = arrival;
    return value;
}
}

int main() {
    unsigned passed{}, failed{};
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("explicit UTC and timezone equivalence", [] {
        expect(atlas::parseUtc("1970-01-01T00:00:00Z") == 0);
        expect(atlas::parseUtc("2026-11-06T13:00:00+13:00") == atlas::parseUtc("2026-11-06T00:00:00Z"));
        expect(atlas::parseUtc("2026-11-05T19:00:00-05:00") == atlas::parseUtc("2026-11-06T00:00:00Z"));
    });
    test("fractional and pre-epoch timestamps", [] {
        expect(atlas::parseUtc("1970-01-01T00:00:00.000001Z") == 1);
        expect(atlas::parseUtc("1969-12-31T23:59:59.9Z") == -100000);
        expect(atlas::parseUtc("2000-02-29T00:00:00Z") > 0);
    });
    test("historical and future dates avoid 32-bit day multiplication", [] {
        expect(atlas::parseUtc("1900-01-01T00:00:00Z") == -2208988800000000LL);
        expect(atlas::parseUtc("2040-01-01T00:00:00Z") == 2208988800000000LL);
        expect(atlas::parseUtc("0001-01-01T00:00:00Z") == -62135596800000000LL);
        expect(atlas::parseUtc("9999-12-31T23:59:59.999999Z") == 253402300799999999LL);
        rejects([] { atlas::parseUtc("0001-01-01T00:00:00+12:00"); });
        rejects([] { atlas::parseUtc("9999-12-31T23:59:59-12:00"); });
    });
    test("invalid calendar and timezone rejection", [] {
        for (const auto* value : {"2026-02-29T00:00:00Z", "2026-11-06T24:00:00Z", "2026-11-06T00:00:00",
                                  "2026-11-06T00:00:00.0000001Z", "2026-11-06T00:00:00+24:00", "0000-01-01T00:00:00Z"})
            rejects([&] { atlas::parseUtc(value); });
    });
    test("station identity and position validation", [] {
        auto event = makeEvent(1); event.validate();
        event.position = {0, 180}; event.validate();
        event.position = {91, 0}; rejects([&] { event.validate(); });
        event.position = {std::numeric_limits<double>::quiet_NaN(), 0}; rejects([&] { event.validate(); });
        event.position.reset(); event.individualId = "invented-bird"; rejects([&] { event.validate(); });
    });
    test("bounded rational speeds", [] {
        expect(atlas::Speed::parse("60").scale(14400000000LL) == 240000000);
        expect(atlas::Speed::parse("0.5").scale(3) == 6);
        expect(atlas::Speed::parse("1/3").scale(2) == 6);
        expect(atlas::Speed::parse("0.01").scale(1) == 100);
        for (const auto* value : {"0", "-1", "NaN", "inf", "10001", "1/0", "0.001", "1.1234567"})
            rejects([&] { atlas::Speed::parse(value); });
    });
    test("four hours into four minutes", [] {
        atlas::Timeline timeline({makeEvent(1), makeEvent(2, 14400000000LL)}, atlas::TimeBasis::observed, atlas::Speed::parse("60"));
        expect(timeline.schedule()[0].dueUs == 0);
        expect(timeline.schedule()[1].dueUs == 240000000);
        expect(timeline.durationUs() == 240000001);
    });
    test("late upload changes observation versus arrival ordering", [] {
        const std::vector<atlas::Event> events{makeEvent(1, 200, 10), makeEvent(2, 100, 20)};
        atlas::Timeline observed(events, atlas::TimeBasis::observed);
        atlas::Timeline arrival(events, atlas::TimeBasis::arrival);
        expect(observed.events().front().revisionSequence == 2);
        expect(arrival.events().front().revisionSequence == 1);
    });
    test("simultaneous stable order and bounded omission", [] {
        atlas::Timeline timeline({makeEvent(3), makeEvent(1), makeEvent(2)}, atlas::TimeBasis::observed);
        std::array<atlas::BlockTrigger, 2> out{};
        const auto result = timeline.block(0, 64, 48000, out);
        expect(result.valid && result.written == 2 && result.omitted == 1 && result.total == 3);
        expect(out[0].revisionSequence == 1 && out[1].revisionSequence == 2);
    });
    test("sample positions invariant across partitions", [] {
        atlas::Timeline timeline({makeEvent(1), makeEvent(2, 1), makeEvent(3, 1000), makeEvent(4, 10000), makeEvent(5, 25000)}, atlas::TimeBasis::observed);
        for (const unsigned rate : {44100U, 48000U, 96000U}) {
            for (const unsigned block : {1U, 17U, 64U, 257U, 2048U}) {
                std::vector<std::pair<std::int64_t, std::int64_t>> actual;
                for (std::int64_t frame = 0; frame < rate / 10; frame += block) {
                    std::array<atlas::BlockTrigger, 8> out{};
                    const auto result = timeline.block(frame, block, rate, out);
                    expect(result.valid && result.omitted == 0);
                    for (std::size_t i = 0; i < result.written; ++i) actual.emplace_back(frame + out[i].sampleOffset, out[i].revisionSequence);
                }
                expect(actual.size() == timeline.schedule().size());
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    expect(actual[i].first == timeline.schedule()[i].dueUs * rate / 1000000);
                    expect(actual[i].second == timeline.schedule()[i].revisionSequence);
                }
            }
        }
    });
    test("seek and half-open block boundary", [] {
        atlas::Timeline timeline({makeEvent(1), makeEvent(2, 1000000)}, atlas::TimeBasis::observed);
        std::array<atlas::BlockTrigger, 8> out{};
        expect(timeline.block(47936, 64, 48000, out).written == 0);
        expect(timeline.block(48000, 64, 48000, out).written == 1);
        expect(out[0].sampleOffset == 0 && out[0].revisionSequence == 2);
    });
    test("recording bounds retain leading and trailing silence", [] {
        atlas::Timeline timeline({makeEvent(1, 100, 200)}, atlas::TimeBasis::arrival, {}, {{100, 1000}});
        expect(timeline.schedule()[0].dueUs == 100 && timeline.durationUs() == 900);
    });
    test("empty timeline and zero-output capacity", [] {
        atlas::Timeline empty({}, atlas::TimeBasis::observed);
        expect(empty.durationUs() == 0 && empty.peakEventsPerSecond() == 0);
        expect(empty.block(0, 64, 48000, {}).valid);
        atlas::Timeline one({makeEvent(1)}, atlas::TimeBasis::observed);
        expect(one.block(0, 64, 48000, {}).omitted == 1);
    });
    test("invalid/overflow block inputs fail without throwing", [] {
        atlas::Timeline timeline({}, atlas::TimeBasis::observed);
        expect(!timeline.block(-1, 64, 48000, {}).valid);
        expect(!timeline.block(0, 0, 48000, {}).valid);
        expect(!timeline.block(0, 64, 1, {}).valid);
        expect(!timeline.block(std::numeric_limits<std::int64_t>::max(), 64, 48000, {}).valid);
    });
    test("duration overflow is explicit", [] {
        rejects([] { atlas::Speed::parse("0.01").scale(std::numeric_limits<std::int64_t>::max()); });
        rejects([] { atlas::Timeline invalid({}, atlas::TimeBasis::observed, {}, {{100, 99}}); });
    });
    test("sliding one-second density", [] {
        atlas::Timeline timeline({makeEvent(1), makeEvent(2, 999999), makeEvent(3, 1000000)}, atlas::TimeBasis::observed);
        expect(timeline.peakEventsPerSecond() == 2);
    });
    test("timeline sorting avoids per-comparison ID allocations", [] {
        constexpr std::size_t count = 1000;
        std::vector<atlas::Event> events;
        events.reserve(count);
        for (std::size_t i = count; i > 0; --i) {
            auto value = makeEvent(static_cast<std::int64_t>(i));
            value.id += std::string(128, 'x');
            events.push_back(std::move(value));
        }
        const auto before = allocations.load();
        atlas::Timeline timeline(std::move(events), atlas::TimeBasis::observed);
        const auto used = allocations.load() - before;
        // Leave room for library bookkeeping without allowing copies at each comparison.
        expect(used < count);
        expect(timeline.events().front().revisionSequence == 1);
        expect(timeline.events().back().revisionSequence == static_cast<std::int64_t>(count));
    });
    test("prepared block lookup performs no C++ allocations", [] {
        atlas::Timeline timeline({makeEvent(1), makeEvent(2, 1000)}, atlas::TimeBasis::observed);
        std::array<atlas::BlockTrigger, 8> out{};
        const auto before = allocations.load();
        const auto result = timeline.block(0, 64, 48000, out);
        const auto after = allocations.load();
        expect(result.valid && result.written == 2 && before == after);
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
