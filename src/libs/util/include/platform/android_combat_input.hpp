#pragma once
#include "android_gameplay.hpp"
#include <array>
#include <functional>
#include <unordered_map>

namespace storm::android
{
// Script-maintained one-second proximity hint; actual ChrAction dispatch still
// performs the native FindDialogCharacter query and owns eligibility.
inline std::string_view interactKind(bool verifiedPlayer, std::string_view cachedTarget)
{
    uint32_t index = 0;
    const auto parsed = std::from_chars(cachedTarget.data(), cachedTarget.data()+cachedTarget.size(), index);
    return verifiedPlayer && !cachedTarget.empty() && parsed.ec == std::errc() &&
        parsed.ptr == cachedTarget.data()+cachedTarget.size() && index <= 0x7fffffffU ? "dialog" : "interact";
}
inline std::atomic<uint64_t> combatCancelEpoch{0};
// JNI producer safe: no container, engine pointer or script access here.
inline void cancelAndroidCombatInput() { combatCancelEpoch.fetch_add(1, std::memory_order_release); }
struct CombatBinding
{
    std::string action, control;
    bool held = false;
    bool operator==(const CombatBinding &) const = default;
};
// Pure semantic state; numeric phases intentionally independent of CONTROLS.
// 0 inactive, 1 active, 2 released, 3 activated match the engine contract.
class CombatInput
{
    struct Request { uint64_t revision, generation, cancel; uint32_t tick; std::string action, phase; };
    struct Virtual { bool held = false, previous = false, pulse = false; int state = 0; };
    inline static std::atomic<uint64_t> nextGeneration_{0};
    uint64_t revision_ = 0, generation_ = 0, sequence_ = 0, cancel_ = 0;
    std::string player_, mode_ = "unknown";
    std::vector<CombatBinding> bindings_;
    std::deque<Request> pending_;
    std::unordered_map<std::string, Virtual> virtual_;
    bool fresh_ = false;
    static bool number(std::string_view value, uint64_t &result)
    {
        const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result);
        return !value.empty() && parsed.ec == std::errc() && parsed.ptr == value.data()+value.size();
    }
    const CombatBinding *binding(std::string_view action) const
    {
        for (const auto &b : bindings_) if (b.action == action) return &b;
        return nullptr;
    }
    bool synchronizeCancel()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch == cancel_) return false;
        cancel_ = epoch; clear(); return true;
    }
public:
    // Installed by the engine adapter. Called only on the game thread.
    std::function<bool()> guard;
    void clear()
    {
        pending_.clear();
        for (auto &[name, value] : virtual_)
        {
            value.state = value.held || value.previous || value.pulse ? 2 : 0;
            value.held = value.previous = value.pulse = false;
        }
        fresh_ = false;
    }
    void resetActivity()
    {
        generation_ = nextGeneration_.fetch_add(1, std::memory_order_relaxed)+1;
        sequence_ = 0; clear();
    }
    void update(uint64_t revision, std::string player, std::string mode, std::vector<CombatBinding> bindings)
    {
        synchronizeCancel();
        // This token owns the input session, including its Activity and native
        // player/capability identity. Same-scene remapping must fence old UI taps.
        if (player != player_ || mode != mode_ || bindings != bindings_) resetActivity();
        if (revision != revision_ || player != player_ || mode != mode_ || bindings != bindings_) clear();
        revision_ = revision; player_ = std::move(player); mode_ = std::move(mode); bindings_ = std::move(bindings);
    }
    uint64_t generation() const { return generation_; }
    uint64_t revision() const { return revision_; }
    const std::string &player() const { return player_; }
    bool offer(std::string_view input, uint32_t tick,
               uint64_t enqueueEpoch = combatCancelEpoch.load(std::memory_order_acquire))
    {
        synchronizeCancel();
        // Fence the JNI drain as well as our own queue: cancellation may occur
        // after Java returned a command but before it reaches this mailbox.
        if (enqueueEpoch != cancel_) return false;
        if (input.size() > 192) return false;
        std::array<std::string_view,6> fields;
        for (size_t i=0;i<fields.size();++i)
        {
            const auto tab=input.find('\t');
            if (i+1==fields.size()) { if(tab!=std::string_view::npos)return false; fields[i]=input; }
            else { if(tab==std::string_view::npos)return false; fields[i]=input.substr(0,tab); input.remove_prefix(tab+1); }
        }
        uint64_t revision=0,generation=0,sequence=0;
        if(fields[0]!="1" || !number(fields[1],revision) || !number(fields[2],generation) || !number(fields[3],sequence) ||
           revision!=revision_ || generation!=generation_ || !generation || sequence<=sequence_)return false;
        if(fields[4]=="all" && fields[5]=="cancel") { sequence_=sequence;clear();return true; }
        const auto *b=binding(fields[4]);
        if(!b || mode_=="unknown" || player_.empty())return false;
        if((b->held && fields[5]!="down" && fields[5]!="up") || (!b->held && fields[5]!="tap"))return false;
        // Release is never dropped because taps filled the queue.
        if(pending_.size()>=16 && fields[5]!="up")return false;
        sequence_=sequence;
        if(fields[5]=="up")
        {
            pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[&](const auto &r){return r.action==b->action;}),pending_.end());
            auto &v=virtual_[b->control];v.held=false;v.state=v.previous?2:0;
            return true;
        }
        pending_.push_back({revision,generation,cancel_,tick,std::string(fields[4]),std::string(fields[5])});
        return true;
    }
    // Called at Controls::Update, after the old frame's Player has consumed its
    // input and before Core::ProcessControls emits the next frame's events.
    void advance(uint32_t tick)
    {
        synchronizeCancel();
        if(!guard || !guard()) { clear();return; }
        for(auto &[name,v]:virtual_){v.previous=v.held||v.pulse;v.pulse=false;}
        bool tapped=false;
        while(!pending_.empty())
        {
            const auto &r=pending_.front();
            if(r.revision!=revision_ || r.generation!=generation_ || r.cancel!=cancel_ || tick-r.tick>1000)
            { pending_.pop_front();continue; }
            const auto *b=binding(r.action);
            if(!b){pending_.pop_front();continue;}
            // Player executes one attack by priority per frame. Preserve tap
            // order and one inactive frame between repeated pulses.
            if(r.phase=="tap" && (tapped || virtual_[b->control].previous))break;
            auto &v=virtual_[b->control];
            if(r.phase=="tap"){v.pulse=true;tapped=true;} else v.held=true;
            pending_.pop_front();
        }
        for(auto &[name,v]:virtual_)
        {
            const bool active=v.held||v.pulse;
            v.state=active?(v.previous?1:3):(v.previous?2:0);
        }
        fresh_=true;
    }
    // Merge only named output states. No PC key maps, arbitrary script events,
    // or direct Character calls. Physical activation/hold keeps priority.
    int merge(std::string_view control,int physical)
    {
        synchronizeCancel();
        auto it=virtual_.find(std::string(control));
        if(it==virtual_.end())return physical;
        if((fresh_ || it->second.held) && (!guard || !guard()))clear();
        const int state=it->second.state;
        if(physical==1 || physical==3)return physical;
        return state==0?physical:state;
    }
    std::string json() const
    {
        std::string result="{\"schemaVersion\":1,\"gameplayRevision\":"+std::to_string(revision_)+
            ",\"activityGeneration\":"+std::to_string(generation_)+",\"playerId\":"+gameplayJsonString(player_)+
            ",\"mode\":"+gameplayJsonString(mode_)+",\"actions\":[";
        for(size_t i=0;i<bindings_.size();++i){if(i)result+=',';result+=gameplayJsonString(bindings_[i].action);}
        return result+"]}";
    }
};
inline CombatInput combatInput;
}
