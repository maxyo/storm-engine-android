#include "service_backend.hpp"
#include <cmath>
#include <chrono>
#include <thread>
#include <limits>
extern "C" ma_uint64 storm_vorbis_frames(const char*, unsigned int*);
namespace storm_audio {
namespace {
float nonnegative(float x) { return std::isfinite(x) ? std::max(0.0f,x) : 0.0f; }
float seconds(float x) { return std::min(nonnegative(x),60.0f); }
}
Mixer::Mixer(bool off,unsigned limit):backend(off),budget(std::clamp(limit,2u,capacity)),offline(off) {
    check(ma_sound_group_init(&backend.engine,0,nullptr,&master));
    lastFrame=ma_engine_get_time_in_pcm_frames(&backend.engine);
    // Storm/FMOD default uses left-handed coordinates. Reflect Z consistently.
    ma_engine_listener_set_direction(&backend.engine,0,0,0,-1);
}
Mixer::~Mixer() {
    for(auto& voice:voices) voice.reset();
    cache.clear();
    ma_sound_group_uninit(&master);
}
std::shared_ptr<Mixer::Resource> Mixer::load(const std::string& path,bool stream) {
    auto key=path+(stream ? "\nstream":"\nsample");
    auto it=cache.find(key); if(it!=cache.end()) return it->second;
    auto r=std::make_shared<Resource>(); r->path=path;
    ma_decoder decoder{};
    auto config=ma_decoder_config_init(ma_format_f32,0,48000);
    check(ma_decoder_init_file(path.c_str(),&config,&decoder));
    r->rate=decoder.outputSampleRate;
    auto result=ma_decoder_get_length_in_pcm_frames(&decoder,&r->frames);
    ma_decoder_uninit(&decoder);
    if(result!=MA_SUCCESS || r->frames==0) r->frames=storm_vorbis_frames(path.c_str(),&r->rate);
    if(!r->frames || !r->rate) throw std::runtime_error("audio resource has no seekable duration: "+path);
    if(!stream) r->cache=std::make_unique<Sound>(backend,path.c_str(),false,false,&master);
    cache.emplace(key,r); return r;
}
Mixer::Voice* Mixer::find(uint32_t id) {
    unsigned index=(id&0xffffu)-1;
    if(index>=capacity || !voices[index] || voices[index]->id!=id) return nullptr;
    return voices[index].get();
}
const Mixer::Voice* Mixer::find(uint32_t id) const { return const_cast<Mixer*>(this)->find(id); }
bool Mixer::playing(uint32_t id) const { auto v=find(id); return v && !v->stopped; }
float Mixer::positionSeconds(uint32_t id) const { auto v=find(id);return v ? float(v->cursor/v->resource->rate):0; }
void Mixer::fade(Voice& v,float target,float duration,bool stopAfter) {
    v.fadeStart=v.envelope;v.fadeTarget=target;v.fadeBegin=ma_engine_get_time_in_pcm_frames(&backend.engine);
    v.fadeFrames=uint64_t(seconds(duration)*48000);v.stopAfterFade=stopAfter;
    if(!v.fadeFrames) {v.envelope=target; if(stopAfter)v.stopped=true;}
}
uint32_t Mixer::play(const std::string& path,const Play& input) {
    update();
    auto resource=load(path,input.stream); // Fail before replacing an existing music slot.
    unsigned index=capacity;
    if(input.stream) index=nextMusic;
    else for(unsigned i=2;i<capacity;++i) if(!voices[i]) {index=i;break;}
    if(index==capacity) throw std::runtime_error("audio logical voice limit reached");
    auto v=std::make_unique<Voice>();v->resource=resource;v->parameters=input;
    v->parameters.volume=nonnegative(input.volume);v->parameters.pitch=nonnegative(input.pitch);
    v->parameters.priority=std::clamp(input.priority,0,255);
    v->parameters.minDistance=std::max(0.001f,nonnegative(input.minDistance));
    v->parameters.maxDistance=std::max(v->parameters.minDistance+0.001f,nonnegative(input.maxDistance));
    v->order=++sequence;
    if(input.stream) {v->id=index+1;v->parameters.priority=0;v->cursor=musicPosition[path];}
    else {if(++stamp==0)++stamp;v->id=(uint32_t(stamp)<<16)|(index+1);}
    if(input.fade>0) {v->envelope=0;fade(*v,1,input.fade,false);}
    if(input.stream) {
        if(voices[index]) finish(index);
        unsigned previous=1-index;
        if(voices[previous]) fade(*voices[previous],0,input.fade,true);
        nextMusic=previous;
    }
    auto id=v->id;voices[index]=std::move(v);
    try {rebalance();} catch(...) {finish(index);throw;}
    return id;
}
void Mixer::finish(unsigned i) {
    if(!voices[i]) return;
    auto& v=*voices[i];
    if(v.parameters.stream) musicPosition[v.resource->path]=std::fmod(v.cursor,double(v.resource->frames));
    voices[i].reset();ended.push_back(i);
}
float Mixer::gain(const Voice& v) const {
    int c=v.parameters.category;
    return v.parameters.volume*v.envelope*(c>=1 && c<=3 ? gains[c-1]:1.0f);
}
float Mixer::audibility(const Voice& v) const {
    float a=gain(v);if(!v.parameters.spatial)return a;
    auto p=v.parameters.position;float dx=p.x-listenerPosition.x,dy=p.y-listenerPosition.y,dz=p.z-listenerPosition.z;
    float d=std::sqrt(dx*dx+dy*dy+dz*dz);
    return a*std::clamp(1-(d-v.parameters.minDistance)/(v.parameters.maxDistance-v.parameters.minDistance),0.0f,1.0f);
}
void Mixer::apply(Voice& v) {
    if(!v.physical)return;
    auto& s=v.physical->sound;auto& p=v.parameters;
    ma_sound_set_looping(&s,p.loop);
    ma_sound_set_volume(&s,gain(v));ma_sound_set_pitch(&s,std::max(0.001f,p.pitch));
    ma_sound_set_attenuation_model(&s,ma_attenuation_model_linear);
    ma_sound_set_min_distance(&s,p.minDistance);ma_sound_set_max_distance(&s,p.maxDistance);
    ma_sound_set_position(&s,p.position.x,p.position.y,-p.position.z);
    if(p.paused || p.pitch==0)v.physical->pause();else v.physical->play();
}
void Mixer::rebalance() {
    std::vector<Voice*> eligible;
    for(auto& v:voices) if(v && !v->stopped && !v->parameters.paused && v->parameters.pitch>0) eligible.push_back(v.get());
    std::sort(eligible.begin(),eligible.end(),[&](const Voice* a,const Voice* b){
        if(a->parameters.priority!=b->parameters.priority)return a->parameters.priority<b->parameters.priority;
        float ag=audibility(*a),bg=audibility(*b);if(ag!=bg)return ag>bg;
        return a->order<b->order;
    });
    if(eligible.size()>budget)eligible.resize(budget);
    for(auto& v:voices) if(v && v->physical && std::find(eligible.begin(),eligible.end(),v.get())==eligible.end()) {
        if(ma_engine_get_time_in_pcm_frames(&backend.engine)>v->physicalAtFrame) v->cursor=v->physical->cursor();
        if(v->parameters.loop)v->cursor=std::fmod(v->cursor,double(v->resource->frames));
        v->physical.reset();
    }
    for(auto* v:eligible) {
        if(!v->physical) {
            v->physical=std::make_unique<Sound>(backend,v->resource->path.c_str(),v->parameters.stream,v->parameters.spatial,&master);
            v->physical->seek(uint64_t(v->cursor));
            v->physicalAtFrame=ma_engine_get_time_in_pcm_frames(&backend.engine);
        }
        apply(*v);
    }
}
void Mixer::update() {
    uint64_t now=ma_engine_get_time_in_pcm_frames(&backend.engine), delta=now-lastFrame;lastFrame=now;
    for(unsigned i=0;i<capacity;++i) {
        if(!voices[i])continue;auto& v=*voices[i];
        if(v.physical && !v.parameters.paused) {
            if(now>v.physicalAtFrame) v.cursor=v.physical->cursor();
            if(ma_sound_at_end(&v.physical->sound) && !v.parameters.loop)v.stopped=true;
        } else if(!v.parameters.paused && !suspended) v.cursor+=double(delta)*v.resource->rate/48000*v.parameters.pitch;
        if(v.cursor>=v.resource->frames) {
            if(v.parameters.loop)v.cursor=std::fmod(v.cursor,double(v.resource->frames));
            else v.stopped=true;
        }
        if(v.fadeFrames) {
            float t=std::min(1.0,double(now-v.fadeBegin)/v.fadeFrames);
            v.envelope=v.fadeStart+(v.fadeTarget-v.fadeStart)*t;
            if(t>=1) {v.fadeFrames=0;if(v.stopAfterFade)v.stopped=true;}
        }
        if(v.stopped)finish(i);
    }
    rebalance();
}
void Mixer::stop(uint32_t id,float duration) {
    update();for(auto& v:voices)if(v && (id==0 || v->id==id))fade(*v,0,duration,true);update();
}
void Mixer::resume(uint32_t id) {for(auto& v:voices)if(v && (id==0 || v->id==id))v->parameters.paused=false;rebalance();}
void Mixer::volume(uint32_t id,float value) {for(auto& v:voices)if(v && (id==0 || v->id==id))v->parameters.volume=nonnegative(value);rebalance();}
void Mixer::categories(float a,float b,float c) {gains={std::clamp(nonnegative(a),0.f,1.f),std::clamp(nonnegative(b),0.f,1.f),std::clamp(nonnegative(c),0.f,1.f)};rebalance();}
void Mixer::pitch(float p) {update();for(auto& v:voices)if(v)v->parameters.pitch=nonnegative(p);rebalance();}
void Mixer::spatial(uint32_t id,int message,const void* value) {
    auto v=find(id);if(!v || !value)return;
    if(message==1)v->parameters.maxDistance=std::max(v->parameters.minDistance+0.001f,nonnegative(*static_cast<const float*>(value)));
    if(message==2)v->parameters.minDistance=std::clamp(nonnegative(*static_cast<const float*>(value)),0.001f,v->parameters.maxDistance-0.001f);
    if(message==3) {auto p=static_cast<const float*>(value);v->parameters.position={p[0],p[1],p[2]};}
    apply(*v);
}
void Mixer::listener(Vec3 p,Vec3 f,Vec3 up) {
    listenerPosition=p;
    ma_engine_listener_set_position(&backend.engine,0,p.x,p.y,-p.z);
    ma_engine_listener_set_direction(&backend.engine,0,f.x,f.y,-f.z);
    ma_engine_listener_set_world_up(&backend.engine,0,up.x,up.y,-up.z);
}
void Mixer::active(bool enabled,float duration) {
    if(enabled==!suspended)return;
    // Control thread owns stop/start. Background transition waits at most 1s for
    // the audio-frame fade, then stops the device; no audio callback blocks.
    uint64_t frames=uint64_t(std::min(seconds(duration),1.0f)*48000);
    if(enabled) {
        ma_sound_group_set_fade_in_pcm_frames(&master,0,1,frames);
        backend.resume();suspended=false;
    } else {
        ma_sound_group_set_fade_in_pcm_frames(&master,ma_sound_group_get_current_fade_volume(&master),0,frames);
        uint64_t end=ma_engine_get_time_in_pcm_frames(&backend.engine)+frames;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1200);
        while(ma_engine_get_time_in_pcm_frames(&backend.engine)<end && std::chrono::steady_clock::now()<deadline) {
            if(offline) {float pcm[512];backend.read(pcm,256);}
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        update();backend.suspend();suspended=true;
    }
}
void Mixer::read(float* pcm,unsigned frames) {backend.read(pcm,frames);update();}
unsigned Mixer::physicalCount() const {unsigned n=0;for(auto& v:voices)if(v && v->physical)++n;return n;}
unsigned Mixer::logicalCount() const {unsigned n=0;for(auto& v:voices)if(v)++n;return n;}
std::vector<unsigned> Mixer::takeEnded() {auto result=std::move(ended);ended.clear();return result;}
}
