#include <atlas/core.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace atlas {
namespace {
constexpr auto maxInt = std::numeric_limits<std::int64_t>::max();

std::int64_t digits(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("expected digits");
    for (const char c : text) if (c < '0' || c > '9') throw std::invalid_argument("expected digits");
    std::int64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("integer out of range");
    return result;
}

// Exact floor(a * factor / divisor) without compiler-specific 128-bit integers.
bool multiplyDivide(std::int64_t a, std::int64_t factor, std::int64_t divisor,
                    std::int64_t& result, bool roundUp = false) noexcept {
    if (a < 0 || factor < 0 || factor > 1000000 || divisor <= 0 || divisor > 1000000) return false;
    const auto q = a / divisor;
    const auto r = a % divisor;
    if (factor && q > maxInt / factor) return false;
    const auto base = q * factor;
    const auto remainder = r * factor; // Both terms <= 1e6, safely within int64.
    const auto extra = remainder / divisor + (roundUp && remainder % divisor != 0 ? 1 : 0);
    if (base > maxInt - extra) return false;
    result = base + extra;
    return true;
}

void checkSpeed(const Speed& speed) {
    if (speed.numerator < 1 || speed.denominator < 1 || speed.numerator > 1000000 || speed.denominator > 1000000 ||
        speed.numerator * 100 < speed.denominator || speed.numerator > speed.denominator * 10000)
        throw std::invalid_argument("speed outside supported rational range");
}
} // namespace

Microseconds parseUtc(std::string_view value) {
    using namespace std::chrono;
    if (value.size() < 20 || value.size() > 32 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':')
        throw std::invalid_argument("timestamp requires ISO date, time and timezone");
    const int y = static_cast<int>(digits(value.substr(0, 4)));
    const unsigned m = static_cast<unsigned>(digits(value.substr(5, 2)));
    const unsigned d = static_cast<unsigned>(digits(value.substr(8, 2)));
    const auto hour = digits(value.substr(11, 2));
    const auto minute = digits(value.substr(14, 2));
    const auto second = digits(value.substr(17, 2));
    const year_month_day day{year{y}, month{m}, std::chrono::day{d}};
    if (y < 1 || !day.ok() || hour > 23 || minute > 59 || second > 59)
        throw std::invalid_argument("invalid calendar timestamp");
    std::size_t cursor = 19;
    Microseconds fraction{};
    if (value[cursor] == '.') {
        const auto begin = ++cursor;
        while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') ++cursor;
        const auto count = cursor - begin;
        if (count == 0 || count > 6) throw std::invalid_argument("timestamp precision exceeds microseconds");
        fraction = digits(value.substr(begin, count));
        for (auto n = count; n < 6; ++n) fraction *= 10;
    }
    std::int64_t zoneMinutes{};
    if (cursor < value.size() && value[cursor] == 'Z' && cursor + 1 == value.size()) {
        // UTC.
    } else if (cursor + 6 == value.size() && (value[cursor] == '+' || value[cursor] == '-') && value[cursor + 3] == ':') {
        const auto zh = digits(value.substr(cursor + 1, 2));
        const auto zm = digits(value.substr(cursor + 4, 2));
        if (zh > 23 || zm > 59) throw std::invalid_argument("invalid timezone offset");
        zoneMinutes = (zh * 60 + zm) * (value[cursor] == '-' ? -1 : 1);
    } else throw std::invalid_argument("explicit timezone required");
    const auto daysSinceEpoch = sys_days{day}.time_since_epoch().count();
    return ((daysSinceEpoch * 86400 + hour * 3600 + minute * 60 + second) - zoneMinutes * 60) * 1000000 + fraction;
}

void Event::validate() const {
    if (id.empty() || sourceId.empty() || kind.empty() || locationKind.empty() || revisionSequence < 1)
        throw std::invalid_argument("event identity or revision missing");
    if (position && (!std::isfinite(position->latitude) || !std::isfinite(position->longitude) ||
        position->latitude < -90 || position->latitude > 90 || position->longitude < -180 || position->longitude > 180))
        throw std::invalid_argument("invalid event position");
    if (kind == "acoustic_species_detection" && locationKind == "station_detection" && individualId)
        throw std::invalid_argument("station species detection must not invent individual identity");
}

Speed Speed::parse(std::string_view value) {
    if (value.empty() || value.size() > 24) throw std::invalid_argument("invalid replay speed");
    Speed result;
    const auto slash = value.find('/');
    const auto dot = value.find('.');
    if (slash != std::string_view::npos) {
        result.numerator = digits(value.substr(0, slash));
        result.denominator = digits(value.substr(slash + 1));
    } else if (dot != std::string_view::npos) {
        const auto decimals = value.size() - dot - 1;
        if (decimals < 1 || decimals > 6) throw std::invalid_argument("use at most six speed decimals");
        std::string combined{value.substr(0, dot)};
        combined += value.substr(dot + 1);
        result.numerator = digits(combined);
        result.denominator = 1;
        for (std::size_t i = 0; i < decimals; ++i) result.denominator *= 10;
    } else result.numerator = digits(value);
    if (result.numerator < 1 || result.denominator < 1) throw std::invalid_argument("speed must be positive");
    const auto divisor = std::gcd(result.numerator, result.denominator);
    result.numerator /= divisor;
    result.denominator /= divisor;
    checkSpeed(result);
    return result;
}

Microseconds Speed::scale(Microseconds interval) const {
    checkSpeed(*this);
    Microseconds result{};
    if (!multiplyDivide(interval, denominator, numerator, result)) throw std::overflow_error("timeline duration overflow");
    return result;
}

Timeline::Timeline(std::vector<Event> events, TimeBasis basis, Speed speed,
                   std::optional<std::pair<Microseconds, Microseconds>> bounds) : events_(std::move(events)) {
    checkSpeed(speed);
    if (events_.size() > 50000) throw std::invalid_argument("session capacity exceeded");
    if (basis != TimeBasis::observed && basis != TimeBasis::arrival) throw std::invalid_argument("invalid time basis");
    for (const auto& event : events_) event.validate();
    const auto time = [basis](const Event& event) { return basis == TimeBasis::observed ? event.observedAt : event.firstSeenAt; };
    std::sort(events_.begin(), events_.end(), [&](const Event& a, const Event& b) {
        const auto aTime = time(a);
        const auto bTime = time(b);
        if (aTime != bTime) return aTime < bTime;
        if (a.revisionSequence != b.revisionSequence) return a.revisionSequence < b.revisionSequence;
        return a.id < b.id;
    });
    const auto start = bounds ? bounds->first : events_.empty() ? 0 : time(events_.front());
    const auto end = bounds ? bounds->second : events_.empty() ? start : time(events_.back());
    if (end < start) throw std::invalid_argument("reversed timeline bounds");
    // Timestamp parser bounds years to 1..9999; reject arbitrary wider integer input.
    constexpr Microseconds magnitudeLimit = 400000000000000000LL;
    if (start < -magnitudeLimit || start > magnitudeLimit || end < -magnitudeLimit || end > magnitudeLimit)
        throw std::invalid_argument("timeline outside supported calendar range");
    duration_ = speed.scale(end - start);
    schedule_.reserve(events_.size());
    for (std::size_t index = 0; index < events_.size(); ++index) {
        const auto when = time(events_[index]);
        if (when < -magnitudeLimit || when > magnitudeLimit) throw std::invalid_argument("event time out of range");
        const auto due = speed.scale(std::max<Microseconds>(0, when - start));
        if (due == maxInt) throw std::overflow_error("final event duration overflow");
        schedule_.push_back({due, index, events_[index].revisionSequence});
        duration_ = std::max(duration_, due + 1);
    }
}

std::size_t Timeline::peakEventsPerSecond() const noexcept {
    std::size_t left{}, peak{};
    for (std::size_t right = 0; right < schedule_.size(); ++right) {
        while (left < right && schedule_[left].dueUs <= schedule_[right].dueUs - 1000000) ++left;
        peak = std::max(peak, right - left + 1);
    }
    return peak;
}

BlockResult Timeline::block(std::int64_t startFrame, std::uint32_t frames,
                            std::uint32_t rate, std::span<BlockTrigger> output) const noexcept {
    if (startFrame < 0 || frames < 1 || frames > 65536 || rate < 8000 || rate > 384000 || output.size() > 4096 ||
        startFrame > maxInt - frames) return {0, 0, 0, false};
    Microseconds leftUs{}, rightUs{};
    if (!multiplyDivide(startFrame, 1000000, rate, leftUs, true) ||
        !multiplyDivide(startFrame + frames, 1000000, rate, rightUs, true)) return {0, 0, 0, false};
    const auto less = [](const ScheduledEvent& event, Microseconds value) { return event.dueUs < value; };
    const auto begin = std::lower_bound(schedule_.begin(), schedule_.end(), leftUs, less);
    const auto end = std::lower_bound(begin, schedule_.end(), rightUs, less);
    const auto total = static_cast<std::size_t>(end - begin);
    const auto count = std::min(total, output.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& event = begin[static_cast<std::ptrdiff_t>(i)];
        Microseconds frame{};
        if (!multiplyDivide(event.dueUs, rate, 1000000, frame)) return {i, total - i, total, false};
        output[i] = {event.eventIndex, event.revisionSequence, static_cast<std::uint32_t>(frame - startFrame)};
    }
    return {count, total - count, total, true};
}
} // namespace atlas
