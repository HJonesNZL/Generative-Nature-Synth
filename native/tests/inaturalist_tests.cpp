#include <atlas/inaturalist.hpp>
#include <atlas/collection.hpp>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>
#include <source_location>
#include <string>
namespace {
void check(bool value, const std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("inaturalist check failed at line " + std::to_string(location.line()));
}
std::string fixture() {
    return R"({"total_results":1,"page":1,"per_page":30,"results":[{"id":123,"created_at":"2026-11-07T14:00:00+13:00","updated_at":"2026-11-07T14:00:00+13:00","time_observed_at":"2026-11-07T14:00:00+13:00","license_code":"cc-by","geoprivacy":null,"taxon_geoprivacy":null,"obscured":false,"geojson":{"coordinates":[174,-41]},"positional_accuracy":45,"taxon":{"id":7,"name":"Test animal"},"user":{"login":"observer"}}]})";
}
std::string replace(std::string value, const std::string& from, const std::string& to) {
    const auto index=value.find(from); check(index!=std::string::npos); value.replace(index,from.size(),to); return value;
}
}
int main(int argc, char** argv) {
    try {
        constexpr auto now="2026-11-08T00:00:00Z";
        if (argc == 2) {
            std::ifstream input(argv[1]);
            const std::string body((std::istreambuf_iterator<char>(input)), {});
            const auto result=atlas::parseInaturalistResponse(body,now);
            std::cout << "live admitted=" << result.normalizedRecords.size() << " excluded=" << result.invalidRecords << '\n';
            check(!result.normalizedRecords.empty()); return 0;
        }
        const auto parsed=atlas::parseInaturalistResponse(fixture(),now);
        check(parsed.normalizedRecords.size()==1 && parsed.invalidRecords==0);
        const auto& event=parsed.normalizedRecords[0];
        check(event.find("2026-11-07T01:00:00.000000Z")!=std::string::npos);
        check(parsed.originalTimestampEvidence.at("inaturalist:123").find("2026-11-07T14:00:00+13:00")!=std::string::npos);
        check(event.find("individual_id")==std::string::npos && event.find("station_id")==std::string::npos);
        check(event.find("provider_published_at_utc")!=std::string::npos && event.find("provider_record_revision")!=std::string::npos);
        check(event.find("observer")!=std::string::npos && event.find("uncertainty_m\":45")!=std::string::npos);
        for (const auto& pair : {std::pair{"\"cc-by\"","\"cc-by-nc\""},
             {"\"obscured\":false","\"obscured\":true"},
             {"\"geoprivacy\":null","\"geoprivacy\":\"private\""},
             {"\"taxon_geoprivacy\":null","\"taxon_geoprivacy\":\"obscured\""},
             {"2026-11-07T14:00:00+13:00","2026-11-07"},
             {"2026-11-07T14:00:00+13:00","2026-11-09T00:00:00Z"},
             {"[174,-41]","[181,-41]"},{"\"positional_accuracy\":45","\"positional_accuracy\":-1"},
             {"\"positional_accuracy\":45","\"positional_accuracy\":\"45\""}}) {
            const auto bad=atlas::parseInaturalistResponse(replace(fixture(),pair.first,pair.second),now);
            check(bad.normalizedRecords.empty() && bad.invalidRecords + bad.explicitlyRevokedIds.size()==1);
        }
        for (const auto& pair : {std::pair{"\"time_observed_at\":\"2026-11-07T14:00:00+13:00\"", "\"time_observed_at\":\"2026-11-07\""},
             {"\"updated_at\":\"2026-11-07T14:00:00+13:00\"", "\"updated_at\":\"2026-11-06T00:00:00Z\""},
             {"\"created_at\":\"2026-11-07T14:00:00+13:00\"", "\"created_at\":42"}}) {
            const auto bad=atlas::parseInaturalistResponse(replace(fixture(),pair.first,pair.second),now);
            check(bad.normalizedRecords.empty() && bad.invalidRecords + bad.explicitlyRevokedIds.size()==1);
        }
        check(atlas::inaturalistRevalidationUrl({"123","456"}).find("?id=123,456&")!=std::string::npos);
        bool rejected=false;
        try { (void)atlas::parseInaturalistResponse(replace(fixture(),"\"page\":1","\"page\":1,\"page\":2"),now); }
        catch(const std::invalid_argument&) { rejected=true; } check(rejected);
        rejected=false;
        try { (void)atlas::parseInaturalistResponse("{}",now); }
        catch(const std::invalid_argument&) { rejected=true; } check(rejected);
        const auto url=atlas::inaturalistRequestUrl("2026-11-06T00:00:00Z",now);
        check(url.find("per_page=30")!=std::string::npos && url.find("order_by=updated_at")!=std::string::npos && url.find("taxon_id=1")!=std::string::npos && url.find("T00%3A00%3A00Z")!=std::string::npos);
        rejected=false;
        try { (void)atlas::inaturalistRequestUrl("2025-01-01T00:00:00Z",now); }
        catch(const std::invalid_argument&) { rejected=true; } check(rejected);
        const auto directory = std::filesystem::temp_directory_path() /
            ("atlas-inat-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        try {
            const auto path = directory / "library.sqlite3";
            atlas::Collector collector(path);
            const auto scalar = [&](const char* sql) {
                sqlite3* db{}; check(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr)==SQLITE_OK);
                sqlite3_stmt* query{}; check(sqlite3_prepare_v2(db,sql,-1,&query,nullptr)==SQLITE_OK);
                check(sqlite3_step(query)==SQLITE_ROW);
                const auto* text=sqlite3_column_text(query,0);
                const std::string result=text ? reinterpret_cast<const char*>(text) : "";
                sqlite3_finalize(query); sqlite3_close(db); return result;
            };
            std::string body = fixture();
            int recheckMode = 0;
            constexpr auto empty = R"({"total_results":0,"page":1,"per_page":30,"results":[]})";
            const auto pairResponse = [&](const std::string& firstRow, const std::string& secondRow) {
                const auto row=[](const std::string& response) {
                    const auto begin=response.find('[')+1;
                    return response.substr(begin,response.rfind(']')-begin);
                };
                return std::string(R"({"total_results":2,"page":1,"per_page":30,"results":[)") + row(firstRow) + ',' + row(secondRow) + "]}";
            };
            const auto fetch = [&](const atlas::CollectionRequest& request) {
                check(request.url.find("https://api.inaturalist.org/v1/observations?") == 0);
                check(request.body.empty());
                if (request.url.find("?id=") != std::string::npos) {
                    check(request.url.find("taxon_geoprivacy=open")!=std::string::npos);
                    if (recheckMode==1) throw atlas::CollectionFetchError("timeout");
                    if (recheckMode==2) return replace(std::string(empty),"total_results\":0","total_results\":1");
                    if (recheckMode==3) return std::string("{}");
                    if (recheckMode==4) return std::string(empty);
                    if (recheckMode==5) throw atlas::CollectionFetchError("http_429",1800);
                    if (recheckMode==6) throw atlas::CollectionFetchError("http_503",2100);
                    if (recheckMode==7) return replace(fixture(), "\"obscured\":false", "\"obscured\":true");
                    if (recheckMode==8) return replace(fixture(), "\"cc-by\"", "\"cc-by-nc\"");
                    if (recheckMode==9) return replace(fixture(), "\"taxon_geoprivacy\":null", "\"taxon_geoprivacy\":\"obscured\"");
                    if (recheckMode==10) return replace(fixture(), "\"page\":1", "\"page\":2");
                    if (recheckMode==11) return replace(fixture(), "\"id\":123", "\"id\":124");
                    if (recheckMode==13) return pairResponse(replace(fixture(), "\"obscured\":false", "\"obscured\":true"), replace(replace(fixture(), "\"id\":123", "\"id\":124"), "\"geoprivacy\":null", "\"geoprivacy\":42"));
                    if (recheckMode==12) return pairResponse(replace(fixture(), "\"obscured\":false", "\"obscured\":true"), replace(fixture(), "\"id\":123", "\"id\":124"));
                }
                return body;
            };
            const auto first = collector.collect("inaturalist", now, fetch);
            check(first.inserted == 1 && first.complete);
            const auto second = collector.collect("inaturalist", "2026-11-08T00:15:00Z", fetch);
            check(second.duplicates == 1 && second.inserted == 0);
            body = replace(body, "[174,-41]", "[175,-41]");
            const auto third = collector.collect("inaturalist", "2026-11-08T00:30:00Z", fetch);
            check(third.corrected == 1 && third.inserted == 0);
            const auto identity = scalar("SELECT value FROM metadata WHERE key='store_id'");
            body=empty;
            for (const int mode : {1,2,3,10,11}) {
                recheckMode=mode;
                (void)collector.collect("inaturalist", "2026-11-08T00:45:00Z", fetch);
                check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="1");
                check(scalar("SELECT value FROM metadata WHERE key='store_id'")==identity);
            }
            for (const int mode : {5,6}) {
                recheckMode=mode;
                bool cooldown=false;
                try { (void)collector.collect("inaturalist", "2026-11-08T00:50:00Z", fetch); }
                catch (const atlas::CollectionFetchError& error) {
                    cooldown=error.retryAfterSeconds()==(mode==5 ? 1800 : 2100);
                }
                check(cooldown);
                check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="1");
                check(scalar("SELECT value FROM metadata WHERE key='store_id'")==identity);
            }
            check(scalar("SELECT json_extract(raw_json,'$.original_timestamps.time_observed_at') FROM revisions LIMIT 1")=="2026-11-07T14:00:00+13:00");
            recheckMode=4;
            (void)collector.collect("inaturalist", "2026-11-08T01:00:00Z", fetch);
            check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="0");
            check(scalar("SELECT count(*) FROM revisions WHERE source_id='inaturalist'")=="0");
            check(scalar("SELECT value FROM metadata WHERE key='store_id'")!=identity);
            check(scalar("SELECT json_extract(status,'$.last_revalidation_withdrawn') FROM sources WHERE id='inaturalist'")=="1");
            for (const int mode : {7,8,9}) {
                body=fixture(); recheckMode=0;
                (void)collector.collect("inaturalist", now, fetch);
                check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="1");
                body=empty; recheckMode=mode;
                (void)collector.collect("inaturalist", now, fetch);
                check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="0");
                check(scalar("SELECT count(*) FROM revisions WHERE source_id='inaturalist'")=="0");
            }
            body=pairResponse(fixture(),replace(fixture(),"\"id\":123","\"id\":124")); recheckMode=0;
            (void)collector.collect("inaturalist", now, fetch);
            body=replace(fixture(),"\"id\":123","\"id\":124");
            (void)collector.collect("inaturalist", now, fetch);
            body=empty; recheckMode=13;
            (void)collector.collect("inaturalist", now, fetch);
            check(scalar("SELECT count(*) FROM events WHERE source_id='inaturalist'")=="2");
            recheckMode=12;
            (void)collector.collect("inaturalist", now, fetch);
            check(scalar("SELECT count(*) FROM events WHERE id='inaturalist:123'")=="0");
            check(scalar("SELECT count(*) FROM revisions WHERE event_id='inaturalist:123'")=="0");
            check(scalar("SELECT count(*) FROM events WHERE id='inaturalist:124'")=="1");
            const auto revoked=atlas::parseInaturalistResponse(replace(fixture(), "\"obscured\":false", "\"obscured\":true"), now);
            check(revoked.explicitlyRevokedIds==std::vector<std::string>{"123"});
            check(revoked.normalizedRecords.empty() && revoked.originalTimestampEvidence.empty());
            rejected=false;
            try { (void)atlas::inaturalistRevalidationUrl(std::vector<std::string>(31,"123")); }
            catch (const std::invalid_argument&) { rejected=true; } check(rejected);
        } catch (...) { std::filesystem::remove_all(directory); throw; }
        std::filesystem::remove_all(directory);
        std::cout << "iNaturalist boundary checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
