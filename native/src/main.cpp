#include <atlas/storage.hpp>
#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
std::string quote(std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : value) {
        const auto c = static_cast<unsigned char>(byte);
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + "\"";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("usage: atlas-native sessions|replay --db PATH [--session latest --mode observed|arrival --speed 60 --limit 5]");
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc || !std::string_view(argv[i]).starts_with("--")) throw std::invalid_argument("options require values");
            if (!options.emplace(argv[i], argv[i + 1]).second) throw std::invalid_argument("duplicate option");
        }
        for (const auto& [key, value] : options) {
            (void)value;
            if (key != "--db" && key != "--session" && key != "--mode" && key != "--speed" && key != "--limit")
                throw std::invalid_argument("unknown option");
        }
        const auto get = [&](const std::string& key, const std::string& fallback) { const auto found = options.find(key); return found == options.end() ? fallback : found->second; };
        atlas::ReadOnlyLibrary library(get("--db", ".state/atlas.sqlite3"));
        if (std::string_view(argv[1]) == "sessions") {
            std::cout << "[";
            bool first = true;
            for (const auto& session : library.sessions()) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quote(session.id) << ",\"name\":" << quote(session.name)
                          << ",\"state\":" << quote(session.state) << ",\"event_count\":" << session.eventCount
                          << ",\"capture_truncated\":" << (session.truncated ? "true" : "false") << '}';
            }
            std::cout << "]\n";
        } else if (std::string_view(argv[1]) == "replay") {
            const auto mode = get("--mode", "observed");
            if (mode != "observed" && mode != "arrival") throw std::invalid_argument("invalid time basis");
            const auto speed = atlas::Speed::parse(get("--speed", "1"));
            const auto saved = library.session(get("--session", "latest"));
            const auto timeline = library.load(saved.id, mode == "observed" ? atlas::TimeBasis::observed : atlas::TimeBasis::arrival, speed);
            const auto limitText = get("--limit", "5");
            unsigned limit{};
            const auto parsed = std::from_chars(limitText.data(), limitText.data() + limitText.size(), limit);
            if (parsed.ec != std::errc{} || parsed.ptr != limitText.data() + limitText.size() || limit > 50000)
                throw std::invalid_argument("limit must be 0..50000");
            std::cout << "{\"engine\":\"portable-cpp\",\"session_id\":" << quote(saved.id)
                      << ",\"mode\":" << quote(mode) << ",\"event_count\":" << timeline.events().size()
                      << ",\"duration_us\":" << timeline.durationUs() << ",\"peak_events_in_one_second\":" << timeline.peakEventsPerSecond()
                      << ",\"capture_truncated\":" << (saved.truncated ? "true" : "false")
                      << ",\"integrity\":\"sha256-event-content-verified; metadata-envelope-unverified\",\"events\":[";
            const auto count = std::min<std::size_t>(limit, timeline.schedule().size());
            for (std::size_t i = 0; i < count; ++i) {
                if (i) std::cout << ',';
                const auto& item = timeline.schedule()[i];
                std::cout << "{\"index\":" << i << ",\"due_us\":" << item.dueUs
                          << ",\"original_seq\":" << item.revisionSequence
                          << ",\"event\":" << timeline.events()[item.eventIndex].normalizedJson << '}';
            }
            std::cout << "]}\n";
        } else throw std::invalid_argument("unknown command");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animal Atlas native: " << error.what() << '\n';
        return 1;
    }
}
