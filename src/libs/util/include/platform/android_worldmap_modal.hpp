#pragma once
#include <cstdint>
#include <mutex>
#include <string>

namespace storm::android
{
// Only the engine thread publishes completed frames. JNI can commit/cancel that
// receipt, but cannot manufacture a frame or extend its opening deadline.
class WorldMapModal
{
public:
    struct Owner { uint64_t revision=0, activity=0, gameplay=0, world=0; bool operator==(const Owner &) const = default; };
    enum class Phase { None, Preparing, Presented, Committed, Paused, Closing, Resuming, Recovery };
    struct Receipt { uint64_t token=0; uint32_t mask=0; uint64_t active=0,failed=0; };
    struct ResumeAck { Owner owner;uint64_t proof=0;uint32_t mask=0,at=0; };
private:
    mutable std::mutex mutex_;
    Owner owner_;
    Phase phase_=Phase::None;
    uint64_t token_=0, proof_=0;
    uint32_t mask_=0, since_=0;
    bool resumeProof_=false;
    uint64_t closeBarrier_=0;
    ResumeAck resumeAck_;
    void clear() { phase_=Phase::None; token_=proof_=closeBarrier_=0; mask_=0; resumeProof_=false;resumeAck_={}; }
    bool expired(uint32_t now) const {
        return (phase_==Phase::Preparing && now-since_>=1000) ||
            ((phase_==Phase::Presented || phase_==Phase::Committed || phase_==Phase::Closing || phase_==Phase::Resuming) && now-since_>=500);
    }
    void expire(uint32_t now){
        if(!expired(now))return;
        if(phase_==Phase::Committed||phase_==Phase::Closing||phase_==Phase::Resuming)phase_=Phase::Recovery;
        else clear();
    }
public:
    bool request(Owner owner,uint64_t token,uint32_t mask,uint64_t proof,uint32_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if(phase_!=Phase::None || !token || !mask || !proof)return false;
        owner_=owner;token_=token;mask_=mask;proof_=proof;since_=now;phase_=Phase::Preparing;return true;
    }
    void owner(Owner current) { std::lock_guard lock(mutex_);if(owner_!=current)clear(); }
    bool ack(Owner owner,uint32_t mask,uint64_t proof,uint32_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if(phase_==Phase::None)return true;
        if(owner!=owner_ || proof<proof_)return false;
        proof_=proof;
        if((mask&mask_)!=mask_){
            if(phase_==Phase::Preparing||phase_==Phase::Presented)clear();else phase_=Phase::Recovery;
            return true;
        }
        return true;
    }
    void presented(Owner owner,uint32_t mask,uint64_t proof,uint32_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if(phase_!=Phase::Preparing || owner!=owner_ || proof<proof_ || (mask&mask_)!=mask_)return;
        phase_=Phase::Presented;since_=now;
    }
    Receipt receipt(uint32_t now) const {
        std::lock_guard lock(mutex_);
        return {phase_==Phase::Presented&&!expired(now)?token_:0,mask_,phase_==Phase::None?0:token_,phase_==Phase::Recovery?token_:0};
    }
    // 0 cancel, 1 commit opening, 4 Close click barrier, 2 admit a new draw
    // after that click, 3 actual Activity focus regained with current proof.
    bool operate(uint64_t revision,uint64_t activity,uint64_t gameplay,uint64_t token,
                 uint64_t proof,int operation,uint32_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if(phase_==Phase::None || token_!=token || owner_.revision!=revision || owner_.activity!=activity || owner_.gameplay!=gameplay)return false;
        if(operation==0){clear();return true;}
        if(operation==1 && phase_==Phase::Presented && proof>=proof_){proof_=proof;phase_=Phase::Committed;return true;}
        if(operation==4 && (phase_==Phase::Committed || phase_==Phase::Paused)){
            closeBarrier_=proof>proof_?proof:proof_;return true;
        }
        if(operation==2 && (phase_==Phase::Committed || phase_==Phase::Paused) && proof>closeBarrier_ && proof>=proof_){
            proof_=proof;phase_=Phase::Closing;since_=now;resumeProof_=false;return true;
        }
        if(operation==3 && (phase_==Phase::Closing||phase_==Phase::Resuming) && proof>=proof_){
            if(phase_==Phase::Closing){phase_=Phase::Resuming;since_=now;}
            proof_=proof;resumeAck_={owner_,proof,mask_,now};return true;
        }
        return false;
    }
    ResumeAck resumeAck() const {std::lock_guard lock(mutex_);return phase_==Phase::Resuming?resumeAck_:ResumeAck{};}
    void resumed(Owner owner,uint64_t proof){std::lock_guard lock(mutex_);if(phase_==Phase::Resuming&&owner==owner_&&proof>=proof_)resumeProof_=true;}
    bool hold(bool focused,uint32_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if(phase_==Phase::Committed&&!focused)phase_=Phase::Paused;
        if(phase_==Phase::Paused&&focused)phase_=Phase::Recovery; // focus without admitted Close cannot reuse an old ACK
        if(phase_==Phase::Resuming&&focused&&resumeProof_)clear();
        // Paused follows the real focus loss, not a renewable opening grace.
        // In particular run_in_background cannot advance the owned modal scene.
        return phase_!=Phase::None&&phase_!=Phase::Preparing;
    }
    bool ownsInput(uint32_t now) { std::lock_guard lock(mutex_);expire(now);return phase_!=Phase::None; }
    std::string debug() const {
        std::lock_guard lock(mutex_);
        return "phase="+std::to_string(static_cast<int>(phase_))+" token="+std::to_string(token_)+" proof="+std::to_string(proof_)+
            " mask="+std::to_string(mask_)+" owner="+std::to_string(owner_.revision)+"/"+std::to_string(owner_.activity)+"/"+
            std::to_string(owner_.gameplay)+"/"+std::to_string(owner_.world)+" since="+std::to_string(since_);
    }
};
}
