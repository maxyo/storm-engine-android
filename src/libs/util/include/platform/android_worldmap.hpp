#pragma once
#include "android_ship_telemetry.hpp"
#include <array>

namespace storm::android
{
// Same zone flags produced by WdmPlayerShip::TestInStorm; absent data is not fair weather.
inline std::optional<int> mapStormState(const char *inStorm, const char *tornado)
{
    if (!inStorm || !tornado) return std::nullopt;
    const std::string_view storm(inStorm), funnel(tornado);
    if ((storm != "0" && storm != "1") || (funnel != "0" && funnel != "1")) return std::nullopt;
    if (storm == "0") return funnel == "0" ? std::optional<int>(0) : std::nullopt;
    return funnel == "1" ? 2 : 1;
}
enum WorldMapCoverage : uint32_t { MapInstruments = 1, MapShips = 2, MapCommands = 4 };
struct MapInstrumentFrame
{
    uint64_t owner = 0;
    bool visible = false, complete = false;
    std::string date, coordinates;
    std::optional<double> hour, windForce, windAngle, heading, morale;
    std::optional<int> foodDays, rumDays, flag, stormState;
};
struct MapShipFrame
{
    std::string id, name;
    std::optional<double> hull, sails;
    std::optional<int> crew, shipClass;
    std::string type; // Optional stable RealShips model name; never the vessel's display name.
};
struct MapCommand { std::string id, label; bool operator==(const MapCommand &) const = default; };
struct WorldMapFrame
{
    bool available = false, instrumentsVisible = false, shipsVisible = false, shipsComplete = true, commandsReady = false;
    uint64_t owner = 0, gameplayRevision = 0;
    std::string identity, context;
    MapInstrumentFrame instruments;
    std::vector<MapShipFrame> ships;
    std::vector<MapCommand> commands;
    uint32_t availableMask() const
    {
        uint32_t mask = 0;
        const auto &i = instruments;
        if (instrumentsVisible && i.complete && !i.date.empty() && i.hour && i.windForce && i.windAngle && i.heading &&
            i.morale && i.foodDays) mask |= MapInstruments;
        if (shipsVisible && shipsComplete && !ships.empty() && std::all_of(ships.begin(),ships.end(),[](const auto &s){
            return !s.id.empty()&&!s.name.empty()&&s.hull&&s.sails&&s.crew&&s.shipClass;
        })) mask |= MapShips;
        if (commandsReady && !commands.empty() && std::all_of(commands.begin(),commands.end(),[](const auto &c){return !c.id.empty()&&!c.label.empty();})) mask |= MapCommands;
        return mask;
    }
};
class WorldMapHud
{
    WorldMapFrame frame_;
    uint64_t revision_ = 0, activity_ = 0;
    uint32_t covered_ = 0, acknowledgedAt_ = 0, tick_ = 0;
    std::string pending_;
    uint32_t pendingAt_ = 0;
public:
    MapInstrumentFrame source;
    uint64_t contextActionOwner=0;
    std::string contextAction;
    void update(WorldMapFrame frame, uint32_t tick)
    {
        tick_ = tick;
        if (frame.available != frame_.available || frame.identity != frame_.identity || frame.context != frame_.context ||
            frame.commands != frame_.commands || frame.commandsReady != frame_.commandsReady ||
            frame.instruments.flag!=frame_.instruments.flag || frame.instruments.coordinates!=frame_.instruments.coordinates ||
            frame.instruments.rumDays.has_value()!=frame_.instruments.rumDays.has_value() ||
            frame.gameplayRevision != frame_.gameplayRevision || frame.availableMask() != frame_.availableMask())
        { ++revision_; covered_ = 0; pending_.clear(); }
        frame_ = std::move(frame);
        if (!frame_.available) { covered_ = 0; pending_.clear(); }
    }
    void resetActivity() { ++activity_; covered_ = 0; pending_.clear(); }
    const WorldMapFrame &frame() const { return frame_; }
    uint64_t revision() const { return revision_; }
    bool covered(uint32_t mask, uint64_t owner) const
    {
        return frame_.available && owner == frame_.owner && tick_ - acknowledgedAt_ <= 1000 &&
            (covered_ & frame_.availableMask() & mask) == mask;
    }
    bool offer(std::string_view request, uint32_t tick)
    {
        if (request.size() > 256 || !frame_.available || !activity_) return false;
        std::array<std::string_view, 4> fields;
        for (size_t i=0;i<4;++i)
        {
            const auto tab=request.find('\t');
            if (i<3 && tab==std::string_view::npos) return false;
            fields[i]=i<3?request.substr(0,tab):request;
            if(i<3)request.remove_prefix(tab+1);
        }
        const auto number=[](std::string_view s,uint64_t &v){auto p=std::from_chars(s.data(),s.data()+s.size(),v);return p.ec==std::errc()&&p.ptr==s.data()+s.size();};
        uint64_t revision=0,activity=0;
        if(!number(fields[0],revision)||!number(fields[1],activity)||revision!=revision_||activity!=activity_)return false;
        if(fields[2]=="ack")
        {
            uint64_t mask=0;if(!number(fields[3],mask)||mask>7)return false;
            covered_=static_cast<uint32_t>(mask)&frame_.availableMask();acknowledgedAt_=tick;return true;
        }
        if(fields[2]!="activate" || !frame_.commandsReady || !pending_.empty())return false;
        for(const auto &command:frame_.commands)if(command.id==fields[3])
        {pending_=command.id;pendingAt_=tick;return true;}
        return false;
    }
    std::optional<std::string> take(uint32_t tick)
    {
        auto result=std::move(pending_);pending_.clear();
        if(result.empty()||!frame_.available||!frame_.commandsReady||tick-pendingAt_>1000)return {};
        // Consume the lease before native callbacks can change or destroy a scene.
        ++revision_;covered_=0;return result;
    }
    std::string json() const
    {
        const auto q=[](const std::string &s){return gameplayJsonString(s);};
        const auto n=[](const auto &v){return v?std::to_string(*v):std::string("null");};
        const auto &i=frame_.instruments;
        std::string out="{\"schemaVersion\":1,\"revision\":"+std::to_string(revision_)+",\"activity\":"+std::to_string(activity_)+
            ",\"gameplayRevision\":"+std::to_string(frame_.gameplayRevision)+",\"available\":"+(frame_.available?"true":"false")+
            ",\"mask\":"+std::to_string(frame_.availableMask())+",\"commandsReady\":"+(frame_.commandsReady?"true":"false")+
            ",\"shipsVisible\":"+(frame_.shipsVisible?"true":"false")+
            ",\"instrumentsVisible\":"+(frame_.instrumentsVisible?"true":"false")+
            ",\"date\":"+q(i.date)+",\"hour\":"+n(i.hour)+",\"windForce\":"+n(i.windForce)+",\"windAngle\":"+n(i.windAngle)+
            ",\"heading\":"+n(i.heading)+",\"morale\":"+n(i.morale)+",\"foodDays\":"+n(i.foodDays)+",\"rumDays\":"+n(i.rumDays)+
            ",\"coordinates\":"+q(i.coordinates)+",\"flag\":"+n(i.flag)+",\"stormState\":"+n(i.stormState)+",\"ships\":[";
        for(size_t k=0;k<frame_.ships.size();++k){const auto &s=frame_.ships[k];if(k)out+=',';
            out+="{\"id\":"+q(s.id)+",\"name\":"+q(s.name)+",\"hull\":"+n(s.hull)+",\"sails\":"+n(s.sails)+",\"crew\":"+n(s.crew)+",\"shipClass\":"+n(s.shipClass);
            if(!s.type.empty())out+=",\"type\":"+q(s.type);
            out+='}';}
        out+="],\"commands\":[";
        for(size_t k=0;k<frame_.commands.size();++k){if(k)out+=',';const auto &c=frame_.commands[k];out+="{\"id\":"+q(c.id)+",\"label\":"+q(c.label)+"}";}
        return out+"]}";
    }
};
inline WorldMapHud worldMapHud;
// Implemented beside WM_INTERFACE so dispatch retains its actual command list.
std::string worldMapContext(uint64_t owner);
void readWorldMapCommands(WorldMapFrame &frame);
bool executeWorldMapCommand(const std::string &id);
}
