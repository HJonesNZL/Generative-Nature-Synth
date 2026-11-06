#include <atlas/ui_host.hpp>
#include <atlas/storage.hpp>
#include "sqlite_statement.hpp"
#include "event_validation.hpp"
#include "json_text.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace atlas {
namespace {
using detail::Statement;
using detail::quoteJson;
std::int64_t integer(std::string_view value, std::int64_t maximum) {
    std::int64_t result{};
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<0 || result>maximum)
        throw std::invalid_argument("integer outside supported range");
    return result;
}
class Object {
public:
    Object(sqlite3* db, std::string_view json, std::initializer_list<std::string_view> allowed = {}) {
        Statement root(db,"SELECT json_valid(?1),CASE WHEN json_valid(?1) THEN json_type(?1) ELSE 'invalid' END"); root.bindJson(json);
        if (!root.next() || root.integer(0)!=1 || root.text(1)!="object") throw std::invalid_argument("JSON object required");
        Statement fields(db,"SELECT key,type,CAST(value AS TEXT) FROM json_each(?1)"); fields.bindJson(json);
        while (fields.next()) {
            const auto key=fields.text(0);
            if (allowed.size() && std::find(allowed.begin(),allowed.end(),key)==allowed.end()) throw std::invalid_argument("unexpected request field");
            if (!fields_.emplace(key,Field{fields.text(1),fields.optionalText(2).value_or("null")}).second)
                throw std::invalid_argument("duplicate JSON field");
        }
    }
    std::string text(std::string_view key, std::size_t maximum=512) const {
        const auto& field=at(key);
        if (field.type!="text" || field.value.size()>maximum || field.value.find('\0')!=std::string::npos)
            throw std::invalid_argument("invalid text argument");
        return field.value;
    }
    std::string optionalText(std::string_view key) const { return null(key) ? "" : text(key,2048); }
    std::string object(std::string_view key) const {
        const auto& field=at(key);
        if (field.type!="object") throw std::invalid_argument("object argument required");
        return field.value;
    }
    std::int64_t number(std::string_view key, std::int64_t maximum) const {
        const auto& field=at(key);
        if (field.type!="integer") throw std::invalid_argument("integer argument required");
        return integer(field.value,maximum);
    }
    bool null(std::string_view key) const { const auto i=fields_.find(std::string(key)); return i==fields_.end() || i->second.type=="null"; }
    std::string json(std::string_view key) const {
        if (null(key)) return "null";
        const auto& field=at(key);
        if (field.type=="text") return quoteJson(field.value);
        if (field.type=="true" || field.type=="false") return field.type;
        return field.value;
    }
    std::size_t size() const { return fields_.size(); }
private:
    struct Field { std::string type,value; };
    std::map<std::string,Field> fields_;
    const Field& at(std::string_view key) const {
        const auto i=fields_.find(std::string(key));
        if (i==fields_.end()) throw std::invalid_argument("missing request field");
        return i->second;
    }
};
struct CloseDb { void operator()(sqlite3* db) const { if (db) sqlite3_close(db); } };
using Database=std::unique_ptr<sqlite3,CloseDb>;
Database open(const std::filesystem::path& path) {
    const auto utf8=path.u8string();
    const std::string filename(reinterpret_cast<const char*>(utf8.data()),utf8.size());
    sqlite3* raw{};
    const auto status=sqlite3_open_v2(filename.c_str(),&raw,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr);
    Database db(raw);
    if (status!=SQLITE_OK) throw std::runtime_error("library unavailable");
    sqlite3_busy_timeout(db.get(),5000);
    return db;
}
void append(std::string& array, std::string_view value) {
    if (array.size()>1) array+=',';
    array+=value;
    if (array.size()>8*1024*1024) throw std::runtime_error("presentation reply exceeds capacity");
}
std::string eventView(sqlite3* db,std::string_view json,std::int64_t sequence) {
    if (sequence < 1) throw std::runtime_error("invalid revision sequence");
    detail::validateStoredEvent(db,json);
    const Object e(db,json);
    const auto position=e.null("latitude") ? "null" : "{\"latitude\":"+e.json("latitude")+",\"longitude\":"+e.json("longitude")+",\"uncertaintyMetres\":"+e.json("uncertainty_m")+"}";
    const auto confidence=e.null("confidence") ? "null" : "{\"value\":"+e.json("confidence")+",\"meaning\":"+e.json("confidence_meaning")+"}";
    return "{\"id\":"+e.json("event_id")+",\"revisionSequence\":"+quoteJson(std::to_string(sequence))+
        ",\"sourceId\":"+e.json("source_id")+",\"speciesId\":"+e.json("species_id")+",\"commonName\":"+e.json("common_name")+
        ",\"individualId\":"+e.json("individual_id")+",\"deploymentId\":"+e.json("deployment_id")+",\"stationId\":"+e.json("station_id")+
        ",\"eventKind\":"+e.json("event_kind")+",\"locationKind\":"+e.json("location_kind")+",\"position\":"+position+
        ",\"observedAtUtc\":"+e.json("observed_at_utc")+",\"firstSeenAtUtc\":"+e.json("first_seen_at_utc")+
        ",\"confidence\":"+confidence+",\"attribution\":"+e.json("attribution")+",\"qualityFlags\":"+(e.null("quality_flags") ? "[]" : e.json("quality_flags"))+
        ",\"provenance\":"+e.json("provenance_mode")+"}";
}
std::string boundsJson(const std::optional<std::pair<Microseconds,Microseconds>>& bounds) {
    return bounds ? "["+quoteJson(std::to_string(bounds->first))+","+quoteJson(std::to_string(bounds->second))+"]" : "null";
}
std::string sessionView(const SavedSession& s) {
    return "{\"id\":"+quoteJson(s.id)+",\"name\":"+quoteJson(s.name)+",\"state\":"+quoteJson(s.state)+",\"origin\":"+quoteJson(s.origin)+
        ",\"eventCount\":"+std::to_string(s.eventCount)+",\"captureTruncated\":"+(s.truncated ? "true" : "false")+
        ",\"metadataIntegrity\":"+quoteJson(s.metadataIntegrity)+",\"observedWindowPolicy\":"+quoteJson(s.observedWindowPolicy)+
        ",\"observedBoundsUs\":"+boundsJson(s.observedBounds)+",\"arrivalBoundsUs\":"+boundsJson(s.arrivalBounds)+"}";
}
}

struct UiHost::Impl {
    ReadOnlyLibrary library;
    Database db;
    std::optional<Timeline> prepared;
    SavedSession saved;
    std::uint64_t generation{};
    explicit Impl(const std::filesystem::path& path):library(path),db(open(path)) {}
    void load(std::string_view id, TimeBasis basis=TimeBasis::observed, Speed speed={}) {
        // Replace only after complete verification; a failed preparation never exposes new material.
        SavedSession metadata;
        auto next=library.load(id,basis,speed,&metadata);
        prepared=std::move(next); saved=std::move(metadata); ++generation;
    }
    std::string changes(const Object& args) {
        const auto limit=args.number("limit",500);
        if (limit<1) throw std::invalid_argument("positive page limit required");
        const auto after=integer(args.text("afterSequence",20),INT64_MAX);
        const auto requestedStore=args.text("storeId",128);
        // One statement pins store identity, high water and rows to a single read snapshot.
        Statement head(db.get(),"SELECT value,(SELECT COALESCE(MAX(seq),0) FROM revisions) FROM metadata WHERE key='store_id'");
        if (!head.next()) throw std::runtime_error("library identity unavailable");
        const auto storeId=head.text(0); const auto high=head.integer(1);
        const bool baseline=requestedStore!=storeId || after>high;
        const auto sql=baseline ?
            "SELECT seq,event_json,change_kind,live_eligible FROM (SELECT * FROM revisions WHERE seq<="+std::to_string(high)+" ORDER BY seq DESC LIMIT "+std::to_string(limit)+") ORDER BY seq" :
            "SELECT seq,event_json,change_kind,live_eligible FROM revisions WHERE seq>"+std::to_string(after)+" AND seq<="+std::to_string(high)+" ORDER BY seq LIMIT "+std::to_string(limit);
        Statement rows(db.get(),sql.c_str()); std::string events="["; auto next=baseline ? high : after;
        while (rows.next()) {
            const auto seq=rows.integer(0); const auto kind=rows.text(2);
            if (kind!="new" && kind!="corrected" && kind!="baseline") throw std::runtime_error("invalid revision kind");
            append(events,"{\"event\":"+eventView(db.get(),rows.text(1),seq)+",\"liveEligible\":"+(!baseline && kind=="new" && rows.integer(3)==1 ? "true" : "false")+",\"change\":"+quoteJson(baseline ? "baseline" : kind)+"}");
            if (!baseline) next=seq;
        }
        return "{\"storeId\":"+quoteJson(storeId)+",\"nextSequence\":"+quoteJson(std::to_string(next))+",\"events\":"+events+"]}";
    }
    std::string catalog(const Object& args) {
        std::vector<std::string> payloads;
        if (!args.null("material")) {
            Object material(db.get(),args.object("material"),{"kind","sessionId"});
            if (material.text("kind")!="session") throw std::invalid_argument("only saved material is supported");
            const auto id=material.text("sessionId",128);
            if (!prepared || saved.id!=id) load(id);
            for (const auto& e:prepared->events()) { if (payloads.size()==2000) break; payloads.push_back(e.normalizedJson); }
        } else {
            Statement rows(db.get(),"SELECT r.event_json FROM events e JOIN revisions r ON r.seq=e.revision_seq ORDER BY r.seq DESC LIMIT 2000");
            while (rows.next()) payloads.push_back(rows.text(0));
        }
        std::map<std::string,std::string> species,stations,sources;
        bool limited=payloads.size()==2000;
        for (const auto& payload:payloads) {
            detail::validateStoredEvent(db.get(),payload); const Object e(db.get(),payload);
            const auto source=e.text("source_id",128),speciesId=e.optionalText("species_id"),station=e.optionalText("station_id");
            if (!speciesId.empty() && species.size()<500) {
                const auto common=e.optionalText("common_name");
                species.try_emplace(speciesId,"{\"id\":"+quoteJson(speciesId)+",\"commonName\":"+quoteJson(common.empty() ? speciesId : common)+",\"scientificName\":"+quoteJson(e.optionalText("scientific_name"))+",\"group\":\"unclassified\"}");
            }
            const auto locationKind=e.text("location_kind");
            const bool receiver=locationKind=="station_detection" || locationKind=="receiver_detection";
            if (receiver && !station.empty() && !e.null("latitude") && stations.size()<500)
                stations.try_emplace(station,"{\"id\":"+quoteJson(station)+",\"name\":"+quoteJson(station)+",\"sourceId\":"+quoteJson(source)+",\"latitude\":"+e.json("latitude")+",\"longitude\":"+e.json("longitude")+",\"positionDescription\":\"Reported station position; individual position unknown\"}");
            if (sources.size()<128) sources.try_emplace(source,"{\"id\":"+quoteJson(source)+",\"name\":"+quoteJson(source)+",\"measurement\":\"Provider records; inspect event kind\",\"attribution\":"+quoteJson(e.optionalText("attribution"))+"}");
        }
        limited=limited || species.size()==500 || stations.size()==500 || sources.size()==128;
        const auto array=[](const auto& entries) { std::string result="["; for (const auto& [id,json]:entries) { (void)id; append(result,json); } return result+"]"; };
        return "{\"species\":"+array(species)+",\"stations\":"+array(stations)+",\"sources\":"+array(sources)+",\"limited\":"+(limited ? "true" : "false")+"}";
    }
};
UiHost::UiHost(const std::filesystem::path& library):impl_(std::make_unique<Impl>(library)) {}
UiHost::~UiHost()=default;

std::string UiHost::request(std::string_view envelope) {
    std::string id;
    const auto failure=[&](std::string_view code,std::string_view message) { return "{\"requestId\":"+quoteJson(id)+",\"ok\":false,\"error\":{\"code\":"+quoteJson(code)+",\"message\":"+quoteJson(message)+"}}"; };
    try {
        if (envelope.size()>16384) throw std::invalid_argument("request exceeds capacity");
        Object request(impl_->db.get(),envelope,{"protocolVersion","requestId","method","args"});
        id=request.text("requestId",128);
        if (id.empty() || request.number("protocolVersion",1)!=1) throw std::invalid_argument("invalid protocol or request ID");
        const auto method=request.text("method",64), arguments=request.object("args");
        std::string result;
        if (method=="getCapabilities") {
            Object args(impl_->db.get(),arguments); if (args.size()) throw std::invalid_argument("unexpected arguments");
            result=R"({"protocolVersion":1,"implementation":"native","supportedMethods":["getCapabilities","getChanges","getCatalog","listSessions","prepareReplay","getMaterialEvents"],"audioAvailable":false,"maxEventBatch":500,"maxVisibleMarkers":500})";
        } else if (method=="getChanges") {
            result=impl_->changes(Object(impl_->db.get(),arguments,{"storeId","afterSequence","limit"}));
        } else if (method=="getCatalog") {
            result=impl_->catalog(Object(impl_->db.get(),arguments,{"material"}));
        } else if (method=="listSessions") {
            Object args(impl_->db.get(),arguments,{"limit"}); result="[";
            for (const auto& saved:impl_->library.sessions(static_cast<std::size_t>(args.number("limit",100)))) append(result,sessionView(saved));
            result+=']';
        } else if (method=="prepareReplay") {
            Object args(impl_->db.get(),arguments,{"sessionId","basis","speedNumerator","speedDenominator"});
            const auto basis=args.text("basis");
            if (basis!="observed" && basis!="arrival") throw std::invalid_argument("invalid time basis");
            const auto speed=Speed::parse(args.text("speedNumerator",7)+"/"+args.text("speedDenominator",7));
            impl_->load(args.text("sessionId",128),basis=="observed" ? TimeBasis::observed : TimeBasis::arrival,speed);
            result="{\"preparationId\":"+quoteJson("native-"+std::to_string(impl_->generation))+",\"generation\":"+quoteJson(std::to_string(impl_->generation))+
                ",\"durationUs\":"+quoteJson(std::to_string(impl_->prepared->durationUs()))+",\"eventCount\":"+std::to_string(impl_->saved.eventCount)+
                ",\"peakEventsPerSecond\":"+std::to_string(impl_->prepared->peakEventsPerSecond())+",\"session\":"+sessionView(impl_->saved)+"}";
        } else if (method=="getMaterialEvents") {
            Object args(impl_->db.get(),arguments,{"material","offset","limit"});
            Object material(impl_->db.get(),args.object("material"),{"kind","sessionId"});
            if (material.text("kind")!="session") throw std::invalid_argument("only saved material is supported");
            const auto sid=material.text("sessionId",128);
            const auto offset=static_cast<std::size_t>(args.number("offset",50000)),limit=static_cast<std::size_t>(args.number("limit",500));
            if (!limit) throw std::invalid_argument("positive page limit required");
            if (!impl_->prepared || impl_->saved.id!=sid) impl_->load(sid);
            const auto& events=impl_->prepared->events(); result="[";
            for (auto i=offset;i<std::min(events.size(),offset+limit);++i) append(result,eventView(impl_->db.get(),events[i].normalizedJson,events[i].revisionSequence));
            result="{\"total\":"+std::to_string(events.size())+",\"events\":"+result+"]}";
        } else return failure("unsupported","This capability is not implemented by the native data host");
        return "{\"requestId\":"+quoteJson(id)+",\"ok\":true,\"result\":"+result+"}";
    } catch (const std::invalid_argument& e) { return failure("invalid_request",e.what()); }
      catch (const std::exception&) { return failure("unavailable","Library data could not be verified or read; no material was returned"); }
}
}
