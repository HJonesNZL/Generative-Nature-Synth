#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gbif_pilot {
// Worker-only dry planner: returns requests; never sends, sleeps, caches or imports.
enum class State { idle, awaiting, complete, truncated, rate_limited, failed, cancelled, deadline };
struct Request {
    std::uint64_t token{};
    unsigned offset{}, limit{20};
    std::string url;
};
class Planner {
public:
    explicit Planner(std::uint64_t startMillis) : nextMillis_(startMillis) {
        if (startMillis > std::numeric_limits<std::uint64_t>::max() - 60000)
            throw std::invalid_argument("clock capacity");
        deadlineMillis_ = startMillis + 60000;
    }
    State state() const { return state_; }
    unsigned scanned() const { return scanned_; }
    unsigned requests() const { return requests_; }
    void cancel() { state_ = State::cancelled; }
    std::optional<Request> next(std::uint64_t nowMillis) {
        if (state_ != State::idle) return std::nullopt;
        if (nowMillis >= deadlineMillis_) { state_ = State::deadline; return std::nullopt; }
        if (nowMillis < nextMillis_) return std::nullopt;
        ++requests_; ++token_; state_ = State::awaiting;
        return Request{token_, offset_, 20,
            "https://api.gbif.org/v1/occurrence/search?taxonKey=212&country=NZ&year=2020"
            "&hasCoordinate=true&hasGeospatialIssue=false&occurrenceStatus=PRESENT&limit=20&offset=" + std::to_string(offset_)};
    }
    // Metadata only, after the future HTTP/JSON boundary validates a <=512 KiB body.
    bool page(std::uint64_t token, unsigned offset, unsigned limit, unsigned results,
              bool endOfRecords, std::uint64_t nowMillis) {
        if (!current(token, nowMillis)) return false;
        if (offset != offset_ || limit != 20 || results > 20 || (!endOfRecords && results == 0)) {
            state_ = State::failed; return false;
        }
        ++pages_; scanned_ += results; retries_ = 0;
        if (endOfRecords) state_ = State::complete;
        else if (pages_ == 3 || scanned_ == 60) state_ = State::truncated;
        else { offset_ += limit; state_ = State::idle; nextMillis_ = deadlineMillis_ - nowMillis <= 1000 ? deadlineMillis_ : nowMillis + 1000; }
        return true;
    }
    // Delta-seconds Retry-After only. HTTP-date is deliberately deferred, never ignored.
    bool tooManyRequests(std::uint64_t token, std::string_view retryAfter, std::uint64_t nowMillis) {
        if (!current(token, nowMillis)) return false;
        if (retries_ == 2) { state_ = State::rate_limited; return true; }
        unsigned seconds = retries_ == 0 ? 2u : 4u;
        if (!retryAfter.empty()) {
            unsigned parsed{};
            const auto value = std::from_chars(retryAfter.data(), retryAfter.data() + retryAfter.size(), parsed);
            if (value.ec != std::errc{} || value.ptr != retryAfter.data() + retryAfter.size()) {
                state_ = State::rate_limited; return true;
            }
            if (parsed > seconds) seconds = parsed;
        }
        const auto remainingMillis = deadlineMillis_ - nowMillis;
        if (static_cast<std::uint64_t>(seconds) * 1000 >= remainingMillis) {
            state_ = State::rate_limited; return true;
        }
        ++retries_; nextMillis_ = nowMillis + static_cast<std::uint64_t>(seconds) * 1000;
        state_ = State::idle; return true;
    }
    bool fail(std::uint64_t token, std::uint64_t nowMillis) {
        if (!current(token, nowMillis)) return false;
        state_ = State::failed; return true;
    }
private:
    bool current(std::uint64_t token, std::uint64_t nowMillis) {
        if (state_ != State::awaiting || token != token_) return false;
        if (nowMillis >= deadlineMillis_) { state_ = State::deadline; return false; }
        return true;
    }
    State state_{State::idle};
    unsigned offset_{}, pages_{}, scanned_{}, requests_{}, retries_{};
    std::uint64_t nextMillis_{}, deadlineMillis_{}, token_{};
};
} // namespace gbif_pilot
