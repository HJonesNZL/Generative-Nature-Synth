#include "planner.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    using namespace gbif_pilot;
    try {
        Planner p(100);
        for (unsigned i = 0; i < 3; ++i) {
            const auto request = p.next(100 + i * 1000); check(request.has_value(), "request due");
            check(request->offset == i * 20 && request->limit == 20, "page bounds");
            check(request->url == "https://api.gbif.org/v1/occurrence/search?taxonKey=212&country=NZ&year=2020&hasCoordinate=true&hasGeospatialIssue=false&occurrenceStatus=PRESENT&limit=20&offset=" + std::to_string(i * 20), "fixed historical query");
            check(!p.next(100 + i * 1000), "one in flight");
            check(p.page(request->token, i * 20, 20, 20, false, 100 + i * 1000), "page accepted");
            if (i < 2) check(!p.next(1099 + i * 1000), "minimum interval");
        }
        check(p.state() == State::truncated && p.scanned() == 60 && p.requests() == 3 && !p.next(4000), "total/page budgets");
        Planner empty(0); auto request = empty.next(0); check(empty.page(request->token, 0, 20, 0, true, 1) && empty.state() == State::complete, "explicit empty success");
        Planner retry(0); request = retry.next(0);
        check(retry.tooManyRequests(request->token, "3", 100), "429 accepted");
        check(!retry.next(3099), "honour Retry-After"); auto second = retry.next(3100); check(second && second->offset == 0, "retry same page");
        check(!retry.page(request->token, 0, 20, 20, true, 3101), "stale response ignored");
        check(retry.tooManyRequests(second->token, "", 3101), "second retry"); auto third = retry.next(7101); check(third.has_value(), "four-second backoff");
        check(retry.tooManyRequests(third->token, "0", 7102) && retry.state() == State::rate_limited && retry.requests() == 3, "finite retries");
        for (const auto header : {"Wed, 21 Oct 2030 07:28:00 GMT", "-1", "999999999999999999999", "60"}) {
            Planner limited(0); request = limited.next(0); check(limited.tooManyRequests(request->token, header, 0) && limited.state() == State::rate_limited, "unsupported or over-budget Retry-After deferred");
        }
        Planner cancelled(0); request = cancelled.next(0); cancelled.cancel();
        check(!cancelled.page(request->token, 0, 20, 1, true, 1) && !cancelled.next(2) && cancelled.state() == State::cancelled, "cancel discards late completion");
        Planner timeout(0); request = timeout.next(0); check(!timeout.page(request->token, 0, 20, 1, true, 60000) && timeout.state() == State::deadline, "late deadline");
        for (unsigned bad = 0; bad < 4; ++bad) {
            Planner invalid(0); request = invalid.next(0);
            check(!invalid.page(request->token, bad == 0 ? 20 : 0, bad == 1 ? 300 : 20, bad == 2 ? 21 : 0, false, 1) && invalid.state() == State::failed, "malformed page fails closed");
        }
        Planner clockEdge(std::numeric_limits<std::uint64_t>::max() - 60000);
        request = clockEdge.next(std::numeric_limits<std::uint64_t>::max() - 2);
        check(request && clockEdge.page(request->token, 0, 20, 20, false, std::numeric_limits<std::uint64_t>::max() - 1), "clock-edge page");
        check(!clockEdge.next(std::numeric_limits<std::uint64_t>::max() - 1), "clock-edge interval cannot wrap");
        check(!clockEdge.next(std::numeric_limits<std::uint64_t>::max()) && clockEdge.state() == State::deadline, "clock-edge deadline");
        Planner failure(0); request = failure.next(0); check(failure.fail(request->token, 1) && failure.state() == State::failed && !failure.next(2000), "HTTP/parse failure observable");
        Planner worst(0); std::uint64_t now{};
        for (unsigned page = 0; page < 3; ++page) {
            for (unsigned attempt = 0; attempt < 2; ++attempt) {
                request = worst.next(now); check(request.has_value(), "worst request"); check(worst.tooManyRequests(request->token, "", now), "worst retry"); now += attempt == 0 ? 2000 : 4000;
            }
            request = worst.next(now); check(request && worst.page(request->token, page * 20, 20, 20, false, now), "worst page"); now += 1000;
        }
        check(worst.requests() == 9 && worst.scanned() == 60 && worst.state() == State::truncated, "nine total requests ceiling");
        std::cout << "PASS offline GBIF planner: bounds, pagination, empty/error, retry, stale, cancel, deadline\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
