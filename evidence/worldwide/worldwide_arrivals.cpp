#include "juce_collection.hpp"
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
std::string aggregate(const std::filesystem::path& path) {
    sqlite3* db{};
    if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK) throw std::runtime_error("read_failed");
    sqlite3_stmt* query{};
    const auto sql=R"SQL(SELECT json_object(
        'records',(SELECT count(*) FROM events),
        'revisions',(SELECT count(*) FROM revisions),
        'live_eligible',(SELECT count(*) FROM revisions WHERE live_eligible=1),
        'baseline_revisions',(SELECT count(*) FROM revisions WHERE change_kind='baseline'),
        'corrected_revisions',(SELECT count(*) FROM revisions WHERE change_kind='corrected'),
        'newest_observed_utc',(SELECT max(observed_at) FROM events),
        'other_source_records',(SELECT count(*) FROM events WHERE source_id!='birdweather'),
        'region_polls',(SELECT count(*) FROM polls),
        'regions',json((SELECT json_group_array(json_object('id',id,
            'attempt_utc',json_extract(status,'$.last_attempt_at_utc'),
            'latest_observation_utc',json_extract(status,'$.latest_observation_at_utc'),
            'error',json_extract(status,'$.last_error'),
            'pages',json_extract(status,'$.pages_fetched'),
            'complete',json_extract(status,'$.last_poll_complete'),
            'gap',json_extract(status,'$.possible_unrecoverable_gap'),
            'inserted',json_extract(status,'$.last_poll_inserted'),
            'duplicates',json_extract(status,'$.last_poll_duplicates'),
            'corrected',json_extract(status,'$.last_poll_corrected'),
            'invalid',json_extract(status,'$.invalid_records'),
            'deferred_future',json_extract(status,'$.deferred_future_records')))
            FROM sources WHERE id LIKE 'birdweather:%'))))SQL";
    if(sqlite3_prepare_v2(db,sql,-1,&query,nullptr)!=SQLITE_OK) {sqlite3_close(db);throw std::runtime_error("aggregate_query_failed");}
    if(sqlite3_step(query)!=SQLITE_ROW) {sqlite3_finalize(query);sqlite3_close(db);throw std::runtime_error("aggregate_read_failed");}
    const std::string result=reinterpret_cast<const char*>(sqlite3_column_text(query,0));
    sqlite3_finalize(query);sqlite3_close(db);return result;
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/("atlas-world-arrivals-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path=directory/"disposable.sqlite3";
    const auto begin=std::chrono::steady_clock::now();
    try {
        {
            atlas::standalone::CollectionService service(path);
            service.configure({"birdweather_world"});
            std::string lastAttempt;
            unsigned sweeps{};
            while(sweeps<3) {
                const auto state=juce::JSON::parse(juce::String(service.stateJson()));
                const auto* sources=state["sources"].getArray();
                if(!sources) throw std::runtime_error("source_state_missing");
                for(const auto& source:*sources) {
                    if(source["id"].toString()!="birdweather_world") continue;
                    const auto attempt=source["lastAttemptUtc"].toString().toStdString();
                    if(!attempt.empty() && attempt!=lastAttempt && !static_cast<bool>(source["polling"])) {
                        lastAttempt=attempt;++sweeps;
                        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
                        std::cout<<"{\"sweep\":"<<sweeps<<",\"elapsed_ms\":"<<elapsed<<",\"aggregate\":"<<aggregate(path)<<"}"<<std::endl;
                    }
                }
                if(std::chrono::steady_clock::now()-begin>std::chrono::seconds(240)) {
                    service.configure({});throw std::runtime_error("bounded_qualification_timeout");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            service.configure({});
        }
        std::filesystem::remove_all(directory);return 0;
    } catch(const std::exception& error) {
        std::filesystem::remove_all(directory);std::cerr<<error.what()<<std::endl;return 1;
    }
}
