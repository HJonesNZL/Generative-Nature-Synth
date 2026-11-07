#include "juce_collection.hpp"
#include "json_text.hpp"
#include <algorithm>
#include <array>
#include <atlas/core.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iomanip>
#include <juce_core/juce_core.h>
#include <mutex>
#include <sstream>
#include <thread>

namespace atlas::standalone {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t responseLimit = 4U * 1024U * 1024U;
constexpr int maximumRetrySeconds = 86400;
std::string utcNow() {
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    const auto day = floor<days>(now);
    const year_month_day date{day};
    const hh_mm_ss time{now - day};
    char result[40]{};
    const auto count = std::snprintf(
        result, sizeof(result), "%04d-%02u-%02uT%02lld:%02lld:%02lldZ",
        static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<long long>(time.hours().count()),
        static_cast<long long>(time.minutes().count()),
        static_cast<long long>(time.seconds().count()));
    if (count != 20)
        throw std::runtime_error("collection clock unavailable");
    return result;
}
std::string nullable(std::string_view value) {
    return value.empty() ? "null" : detail::quoteJson(value);
}
bool allowed(const CollectionRequest &request) {
    if (request.url == "https://app.birdweather.com/graphql")
        return !request.body.empty() && request.body.size() <= 32768;
    if (!request.body.empty())
        return false;
    if (request.url ==
            "https://map.sustainableoceansociety.co.nz/public/bootstrap.json" ||
        request.url == "https://map.sustainableoceansociety.co.nz/public/"
                       "configs/config.json")
        return true;
    constexpr std::string_view prefix =
        "https://ermap-server.pamdas.org/nzsharkman/api/v1.0/"
        "subjects?subject_group=";
    if (!request.url.starts_with(prefix))
        return false;
    const auto group = std::string_view(request.url).substr(prefix.size());
    if (group.empty() || group.size() > 1536)
        return false;
    const auto hex = [](char value) {
        return (value >= '0' && value <= '9') ||
               (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
    };
    for (std::size_t i = 0; i < group.size(); ++i) {
        const auto c = group[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~')
            continue;
        if (c != '%' || i + 2 >= group.size() || !hex(group[i + 1]) ||
            !hex(group[i + 2]))
            return false;
        i += 2;
    }
    return true;
}
int retryAfter(const juce::String &header) {
    const auto text = header.trim().toStdString();
    if (text.empty() || text.size() > 128)
        return 0;
    unsigned long long seconds{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (parsed.ptr == text.data() + text.size()) {
        if (parsed.ec == std::errc::result_out_of_range)
            return maximumRetrySeconds;
        if (parsed.ec == std::errc{})
            return static_cast<int>(std::min(
                seconds, static_cast<unsigned long long>(maximumRetrySeconds)));
    }
    std::tm time{};
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::get_time(&time, "%a, %d %b %Y %H:%M:%S GMT");
    if (input.fail() || input.peek() != std::char_traits<char>::eof())
        return 0;
    using namespace std::chrono;
    const year_month_day date{year{time.tm_year + 1900},
                              month{static_cast<unsigned>(time.tm_mon + 1)},
                              day{static_cast<unsigned>(time.tm_mday)}};
    if (!date.ok() || time.tm_hour < 0 || time.tm_hour > 23 ||
        time.tm_min < 0 || time.tm_min > 59 || time.tm_sec < 0 ||
        time.tm_sec > 59)
        return 0;
    const auto point = sys_days{date} + hours{time.tm_hour} +
                       minutes{time.tm_min} + std::chrono::seconds{time.tm_sec};
    const auto delay =
        (point - floor<std::chrono::seconds>(system_clock::now())).count();
    return static_cast<int>(
        std::clamp<std::int64_t>(delay, 0, maximumRetrySeconds));
}
} // namespace
struct CollectionService::Impl {
    struct Source {
        CollectionSourceState stored;
        bool enabled{}, polling{};
        unsigned failures{};
        int minimumSeconds{};
        Clock::time_point due{};
    };
    std::filesystem::path path;
    std::unique_ptr<Collector> collector;
    Collector::Fetch injected;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::array<Source, 2> sources;
    std::shared_ptr<juce::WebInputStream> active;
    std::atomic<bool> cancelled{false};
    std::jthread worker;

    Impl(const std::filesystem::path &database, Collector::Fetch fetch)
        : path(database), collector(std::make_unique<Collector>(database)),
          injected(std::move(fetch)) {
        sources[0].stored.id = "birdweather";
        sources[0].minimumSeconds = 60;
        sources[1].stored.id = "nzsharks";
        sources[1].minimumSeconds = 900;
        hydrate();
        const auto now = Clock::now();
        const auto wall =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        for (auto &source : sources) {
            source.due = now;
            source.failures = std::min(source.stored.consecutiveErrors, 7U);
            if (source.stored.lastAttemptUtc.empty())
                continue;
            const auto elapsed =
                (wall - parseUtc(source.stored.lastAttemptUtc)) / 1000000;
            const auto interval = std::max<std::int64_t>(
                source.minimumSeconds,
                std::min(source.stored.retryDelaySeconds,
                         static_cast<unsigned>(maximumRetrySeconds)));
            const auto remaining =
                std::clamp<std::int64_t>(interval - elapsed, 0, interval);
            source.due += std::chrono::seconds{remaining};
        }
        worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Impl() {
        worker.request_stop();
        cancelled.store(true);
        std::shared_ptr<juce::WebInputStream> request;
        {
            std::lock_guard lock(mutex);
            request = active;
        }
        if (request)
            request->cancel();
        wake.notify_all();
        worker.join();
    }
    void hydrate() {
        for (auto &value : collector->sourceStates()) {
            for (auto &source : sources)
                if (source.stored.id == value.id)
                    source.stored = std::move(value);
        }
    }
    void checkCancelled() const {
        if (cancelled.load())
            throw CollectionFetchError("cancelled");
    }
    std::string http(const CollectionRequest &request) {
        auto url = juce::URL(juce::String::fromUTF8(
            request.url.data(), static_cast<int>(request.url.size())));
        if (!request.body.empty())
            url = url.withPOSTData(juce::String::fromUTF8(
                request.body.data(), static_cast<int>(request.body.size())));
        auto stream = std::make_shared<juce::WebInputStream>(url, false);
        stream->withConnectionTimeout(8000)
            .withNumRedirectsToFollow(0)
            .withCustomRequestCommand(request.body.empty() ? "GET" : "POST")
            .withExtraHeaders(
                "User-Agent: AnimalAtlas-LocalResearch/0.1\r\nAccept: "
                "application/json\r\n" +
                juce::String(request.body.empty()
                                 ? ""
                                 : "Content-Type: application/json\r\n"));
        {
            std::lock_guard lock(mutex);
            checkCancelled();
            active = stream;
        }
        struct Clear {
            Impl &owner;
            ~Clear() {
                std::lock_guard lock(owner.mutex);
                owner.active.reset();
            }
        } clear{*this};
        // Enforce a whole-request deadline, including slow reads, rather than
        // relying solely on the transport's connection timeout.
        std::mutex deadlineMutex;
        std::condition_variable_any deadlineWake;
        std::atomic<bool> expired{false};
        std::jthread deadline([stream, &deadlineMutex, &deadlineWake,
                               &expired](std::stop_token stop) {
            std::unique_lock lock(deadlineMutex);
            (void)deadlineWake.wait_for(lock, stop, 8s, [] { return false; });
            if (!stop.stop_requested()) {
                expired.store(true);
                stream->cancel();
            }
        });
        const auto connected = stream->connect(nullptr);
        checkCancelled();
        if (expired.load())
            throw CollectionFetchError("request_timeout");
        if (!connected)
            throw CollectionFetchError("network_or_tls_error");
        const auto status = stream->getStatusCode();
        if (status >= 300 && status < 400)
            throw CollectionFetchError("redirect_not_allowed");
        if (status < 200 || status >= 300)
            throw CollectionFetchError(
                "http_" + std::to_string(status),
                retryAfter(stream->getResponseHeaders()["Retry-After"]));
        if (stream->getTotalLength() > static_cast<juce::int64>(responseLimit))
            throw CollectionFetchError("response_too_large");
        std::string result;
        std::array<char, 16384> bytes;
        while (!stream->isExhausted()) {
            checkCancelled();
            const auto count =
                stream->read(bytes.data(), static_cast<int>(bytes.size()));
            if (expired.load())
                throw CollectionFetchError("request_timeout");
            if (count < 0 || stream->isError())
                throw CollectionFetchError("network_or_tls_error");
            if (count == 0)
                break;
            if (result.size() + static_cast<std::size_t>(count) > responseLimit)
                throw CollectionFetchError("response_too_large");
            result.append(bytes.data(), static_cast<std::size_t>(count));
        }
        checkCancelled();
        return result;
    }
    void run(std::stop_token stop) {
        std::unique_lock lock(mutex);
        while (!stop.stop_requested()) {
            Source *selected{};
            auto earliest = Clock::time_point::max();
            const auto now = Clock::now();
            for (auto &source : sources) {
                if (!source.enabled)
                    continue;
                if (source.due <= now && !selected)
                    selected = &source;
                earliest = std::min(earliest, source.due);
            }
            if (!selected) {
                if (earliest == Clock::time_point::max())
                    wake.wait(lock);
                else
                    wake.wait_until(lock, earliest);
                continue;
            }
            selected->polling = true;
            selected->stored.lastAttemptUtc = utcNow();
            const auto attemptedAt = selected->stored.lastAttemptUtc;
            cancelled.store(false);
            lock.unlock();
            bool failed{}, cancelledPoll{};
            int retry{};
            std::string failure;
            try {
                (void)collector->collect(
                    selected->stored.id, attemptedAt,
                    [this](const CollectionRequest &request) {
                        checkCancelled();
                        if (!allowed(request))
                            throw CollectionFetchError("endpoint_not_allowed");
                        auto response =
                            injected ? injected(request) : http(request);
                        checkCancelled();
                        if (response.size() > responseLimit)
                            throw CollectionFetchError("response_too_large");
                        return response;
                    });
            } catch (const CollectionFetchError &error) {
                if (std::string_view(error.what()) == "cancelled") {
                    cancelledPoll = true;
                } else {
                    failed = true;
                    retry = std::clamp(error.retryAfterSeconds(), 0,
                                       maximumRetrySeconds);
                    failure = error.what();
                }
            } catch (const std::exception &) {
                failed = true;
                failure = "internal_or_storage_error";
            }
            std::vector<CollectionSourceState> refreshed;
            try {
                refreshed = collector->sourceStates();
            } catch (const std::exception &) {
                failed = true;
                failure = "status_unavailable";
            }
            lock.lock();
            for (auto &value : refreshed) {
                for (auto &source : sources)
                    if (source.stored.id == value.id)
                        source.stored = std::move(value);
            }
            selected->polling = false;
            if (failed) {
                selected->failures = std::min(selected->failures + 1U, 7U);
                selected->stored.error = failure;
                selected->stored.complete = false;
                retry = std::max(
                    retry,
                    std::min(3600, 60 * (1 << (selected->failures - 1U))));
            } else if (cancelledPoll) {
                selected->failures =
                    std::min(selected->stored.consecutiveErrors, 7U);
                retry = static_cast<int>(
                    std::min(selected->stored.retryDelaySeconds,
                             static_cast<unsigned>(maximumRetrySeconds)));
            } else
                selected->failures = 0;
            selected->due =
                Clock::now() +
                std::chrono::seconds{std::max(selected->minimumSeconds, retry)};
        }
    }
};
CollectionService::CollectionService(const std::filesystem::path &database,
                                     Collector::Fetch fetch)
    : impl_(std::make_unique<Impl>(database, std::move(fetch))) {}
CollectionService::~CollectionService() = default;
void CollectionService::configure(const std::vector<std::string> &ids) {
    std::array<bool, 2> enabled{};
    for (const auto &id : ids) {
        const auto index = id == "birdweather" ? 0U
                           : id == "nzsharks"  ? 1U
                                               : 2U;
        if (index == 2U || enabled[index])
            throw std::invalid_argument("Invalid collection source selection.");
        enabled[index] = true;
    }
    std::shared_ptr<juce::WebInputStream> request;
    {
        std::lock_guard lock(impl_->mutex);
        for (std::size_t i = 0; i < enabled.size(); ++i) {
            auto &source = impl_->sources[i];
            source.enabled = enabled[i];
            if (!source.enabled && source.polling) {
                impl_->cancelled.store(true);
                request = impl_->active;
            }
        }
    }
    if (request)
        request->cancel();
    impl_->wake.notify_all();
}
std::string CollectionService::stateJson() const {
    std::lock_guard lock(impl_->mutex);
    const auto active =
        std::any_of(impl_->sources.begin(), impl_->sources.end(),
                    [](const auto &source) { return source.enabled; });
    std::string result = std::string("{\"collecting\":") +
                         (active ? "true" : "false") + ",\"sources\":[";
    for (const auto &source : impl_->sources) {
        if (result.back() != '[')
            result += ',';
        const auto remaining =
            source.enabled ? std::clamp<std::int64_t>(
                                 std::chrono::ceil<std::chrono::seconds>(
                                     source.due - Clock::now())
                                     .count(),
                                 0, maximumRetrySeconds)
                           : 0;
        result +=
            "{\"id\":" + detail::quoteJson(source.stored.id) +
            ",\"enabled\":" + (source.enabled ? "true" : "false") +
            ",\"polling\":" + (source.polling ? "true" : "false") +
            ",\"lastAttemptUtc\":" + nullable(source.stored.lastAttemptUtc) +
            ",\"lastSuccessUtc\":" + nullable(source.stored.lastSuccessUtc) +
            ",\"lastObservationUtc\":" +
            nullable(source.stored.lastObservationUtc) +
            ",\"error\":" + nullable(source.stored.error) + ",\"records\":" +
            detail::quoteJson(std::to_string(source.stored.records)) +
            ",\"complete\":" + (source.stored.complete ? "true" : "false") +
            ",\"gap\":" + (source.stored.gap ? "true" : "false") +
            ",\"nextPollSeconds\":" + std::to_string(remaining) + '}';
    }
    return result + "]}";
}
const std::filesystem::path &CollectionService::databasePath() const noexcept {
    return impl_->path;
}
} // namespace atlas::standalone
