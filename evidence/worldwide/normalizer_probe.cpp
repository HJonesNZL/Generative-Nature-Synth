#include <atlas/collection.hpp>
#include <atlas/inaturalist.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
std::string read(const std::filesystem::path &p) {
    std::ifstream in(p);
    if (!in) throw std::runtime_error("probe input unavailable");
    return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char **argv) {
    try {
        if (argc != 5) throw std::runtime_error("mode folder-or-file now database");
        const std::string mode=argv[1];
        if (mode=="inat") {
            const auto page=atlas::parseInaturalistResponse(read(argv[2]),argv[3]);
            for(const auto &record: page.normalizedRecords) std::cout<<record<<'\n';
        } else if(mode=="sharks") {
            const std::filesystem::path folder=argv[2];
            atlas::Collector collector(argv[4]);
            collector.collect("nzsharks",argv[3],[&](const atlas::CollectionRequest &r){
                if(r.url=="https://map.sustainableoceansociety.co.nz/public/bootstrap.json") return read(folder/"bootstrap.json");
                if(r.url=="https://map.sustainableoceansociety.co.nz/public/configs/config.json") return read(folder/"config.json");
                if(r.url.starts_with("https://ermap-server.pamdas.org/nzsharkman/api/v1.0/subjects?")) return read(folder/"subjects.json");
                throw std::runtime_error("unexpected endpoint");
            });
            sqlite3 *db{};
            if(sqlite3_open_v2(argv[4],&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK) throw std::runtime_error("read probe");
            sqlite3_stmt *statement{};
            if(sqlite3_prepare_v2(db,"SELECT event_json FROM revisions",-1,&statement,nullptr)!=SQLITE_OK) throw std::runtime_error("query probe");
            while(sqlite3_step(statement)==SQLITE_ROW) std::cout<<sqlite3_column_text(statement,0)<<'\n';
            sqlite3_finalize(statement); sqlite3_close(db);
        } else throw std::runtime_error("unknown mode");
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n'; return 1;}
}
