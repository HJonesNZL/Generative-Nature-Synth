#include "../src/live_activity.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void contains(const std::string &json, const std::string &text) {
    if (json.find(text) == std::string::npos)
        throw std::runtime_error("Missing activity result: " + text + " in " + json);
}
void absent(const std::string &json, const std::string &text) {
    if (json.find(text) != std::string::npos)
        throw std::runtime_error("Unexpected activity result: " + text);
}
}
int main() {
    try {
        atlas::LiveActivityJournal journal;
        atlas::LiveObservation event{"bird:1", "bird:species:1", "Real provider name",
                                    atlas::Position{-41.123456789, 174.987654321}};
        event.stationId = "receiver:1";
        event.eventKind = "acoustic_species_detection";
        event.locationKind = "station_detection";
        event.locationLabel = "Acoustic receiver location";
        event.uncertaintyMetres = 25.5;
        atlas::VoiceSettings voice;
        voice.character = atlas::VoiceCharacter::whistle;
        journal.submitted(1, 1, event, voice, "Measured region");
        const auto queued = journal.page("session", true, 0, 250, 10000000);
        contains(queued, "\"voices\":[]"); // Acceptance/queueing cannot invent an onset.
        contains(queued, "\"nextSequence\":\"1\"");
        atlas::LiveVoiceStart start{1, 0, 10000000, 12000000, 0};
        journal.ingest(1, std::span(&start, 1), 10000000);
        auto page = journal.page("session", true, 1, 250, 10000000);
        contains(page, "\"token\":\"1:1\"");
        contains(page, "\"onsetUs\":\"10000000\"");
        contains(page, "\"endUs\":\"12000000\"");
        contains(page, "\"voiceType\":\"whistle\"");
        contains(page, "\"stationId\":\"receiver:1\"");
        contains(page, "Acoustic receiver location");
        contains(page, "\"uncertaintyMetres\":25.5");
        contains(page, "-41.123456789");
        auto second = event;
        second.id = "bird:2";
        voice.character = atlas::VoiceCharacter::bell;
        journal.submitted(1, 2, second, voice, "Measured region");
        atlas::LiveVoiceStart stolen{2, 1, 10100000, 12100000, 10110000};
        journal.ingest(1, std::span(&stolen, 1), 10100000);
        page = journal.page("session", true, 2, 1, 10100000);
        contains(page, "\"endUs\":\"10110000\"");
        contains(page, "\"hasMore\":true");
        contains(page, "\"reset\":false");
        page = journal.page("session", true, 3, 1, 10100000);
        contains(page, "\"token\":\"1:2\"");
        contains(page, "\"voiceType\":\"bell\"");
        contains(page, "\"hasMore\":false");
        journal.stopAt(10500000);
        page = journal.page("session", true, 4, 250, 10600000);
        contains(page, "\"endUs\":\"10500000\"");
        contains(page, "\"clockUs\":\"10600000\"");
        contains(journal.page("session", false, 0, 250, 10600000), "\"voices\":[]");
        contains(journal.page("session", true, 5, 250, 43000000), "\"voices\":[]");
        journal.reset();
        page = journal.page("next-session", true, 0, 250, 43000000);
        contains(page, "\"voices\":[]");
        contains(page, "\"reset\":true");
        contains(journal.page("next-session", true, 1, 250, 43000000), "\"reset\":false");
        // Metadata and onset keys are preparation-qualified, so a rebuild may
        // reuse a numeric token without aliasing the previous audible voice.
        journal.submitted(2, 1, event, voice, "Measured region");
        journal.ingest(2, std::span(&start, 1), 10000000);
        contains(journal.page("next-session", true, 0, 250, 10000000), "\"token\":\"2:1\"");
        journal.reset();
        for (std::uint64_t i = 1; i <= atlas::LiveActivityJournal::voiceCapacity + 1; ++i) {
            journal.submitted(1, i, event, voice, "Measured region");
            atlas::LiveVoiceStart row{i, 0, 10000000 + static_cast<std::int64_t>(i), 12000000, 0};
            journal.ingest(1, std::span(&row, 1), 10010000);
        }
        page = journal.page("capacity", true, 0, 250, 10010000);
        contains(page, "\"dropped\":1");
        contains(page, "\"hasMore\":true");
        absent(page, "\"token\":\"1:1\"");
        std::cout << "Live activity receipts, timing, early ends, metadata and bounded paging passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
