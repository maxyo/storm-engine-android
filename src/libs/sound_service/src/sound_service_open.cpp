#ifdef STORM_OPEN_AUDIO
#include "sound_service.h"
#include "core.h"
#include "matrix.h"
#include "v_file_service.h"
#include <thread>

SoundService::SoundService():bShowDebugInfo(false),initialized(false),fFXVolume(.5f),fMusicVolume(.5f),fSpeechVolume(.5f),fPitch(1) {}
SoundService::~SoundService() = default;
bool SoundService::Init() {
    rs=static_cast<VDX9RENDER*>(core.GetService("DX9RENDER"));
    if(!rs)return false;
    try {
        unsigned budget=64;
        if(auto ini=fio->OpenIniFile(core.EngineIniFileName())) {
            fadeTimeInSeconds=ini->GetFloat("sound","fade_time",.5f);
            budget=std::clamp(ini->GetInt("sound","physical_voices",64),2,128);
        }
        audio=std::make_unique<storm_audio::Mixer>(false,budget);
        core.Trace("Open audio: miniaudio 0.11.23 + stb_vorbis, %u physical voices",budget);
        InitAliases();CreateEntityIfNeed();initialized=true;return true;
    } catch(const std::exception& e) {core.Trace("Audio initialization failed: %s",e.what());audio.reset();return false;}
}
void SoundService::RunStart() {
    if(!audio)return;
    CMatrix view;rs->GetTransform(D3DTS_VIEW,view);view.Transposition();
    SetCameraPosition(view.Pos());SetCameraOrientation(view.Vz(),view.Vy());
    try {
        audio->update();
        for(unsigned slot:audio->takeEnded())core.Event("SoundEnded","l",slot+2); // Preserve legacy event payload.
        ProcessSoundSchemes();
    } catch(const std::exception& e) {core.Trace("Audio update failed: %s",e.what());}
}
void SoundService::RunEnd() { if(audio) {try {audio->update();} catch(const std::exception& e){core.Trace("Audio update failed: %s",e.what());}} }
TSD_ID SoundService::SoundPlay(const char* name,eSoundType type,eVolumeType category,bool simpleCache,bool looped,bool cached,int32_t time,const CVECTOR* pos,float minDistance,float maxDistance,int32_t loopPause,float volume,int32_t priority) {
    if(!audio || !name)return 0;
    std::string file=name;
    if(!strchr(name,'\\')) {
        int index=GetAliasIndexByName(name);
        if(index>=0 && !Aliases[index].soundFiles.empty()) {
            auto& a=Aliases[index];file=GetRandomName(&a);minDistance=a.fMinDistance;maxDistance=a.fMaxDistance;priority=a.iPrior;
            if(a.fVolume>0)volume=a.fVolume;
        }
    }
    file=fio->ConvertPathResource((std::string("resource\\sounds\\")+file).c_str());
    storm_audio::Play p;
    p.stream=type==MP3_STEREO;p.spatial=type==PCM_3D; // MP3_3D retains its legacy non-spatial behavior.
    p.category=category;p.loop=looped;p.paused=simpleCache;p.volume=volume;p.pitch=fPitch;p.priority=priority;p.fade=std::max(0,time)*.001f;
    // Legacy defaults were zero/zero (undefined attenuation). Use explicit
    // finite defaults; retain alias/caller distances when supplied.
    p.minDistance=minDistance>0 ? minDistance:1.f;p.maxDistance=maxDistance>p.minDistance ? maxDistance:10000.f;
    if(pos)p.position={pos->x,pos->y,pos->z};
    try {return TSD_ID(audio->play(file,p));}
    catch(const std::exception& e) {core.Trace("Audio load/play failed '%s': %s",file.c_str(),e.what());return 0;}
}
void SoundService::SoundSet3DParam(TSD_ID id,eSoundMessage message,const void* value) {if(audio)audio->spatial(uint32_t(int32_t(id)),message,value);}
void SoundService::SoundStop(TSD_ID id,int32_t time) {if(audio)audio->stop(uint32_t(int32_t(id)),std::max(0,time)*.001f);}
void SoundService::SoundSetVolume(TSD_ID id,float value) {if(audio)audio->volume(uint32_t(int32_t(id)),value);}
bool SoundService::SoundIsPlaying(TSD_ID id) {return audio && audio->playing(uint32_t(int32_t(id)));}
void SoundService::SoundResume(TSD_ID id,int32_t) {if(audio)audio->resume(id.master() || id.index()==0 || int32_t(id)==-1 ? 0:uint32_t(int32_t(id)));}
// These four legacy entry points intentionally retain their existing behavior.
TSD_ID SoundService::SoundDuplicate(TSD_ID) {return 0;}
void SoundService::SoundRestart(TSD_ID) {}
void SoundService::SoundRelease(TSD_ID) {}
float SoundService::SoundGetPosition(TSD_ID) {return 0;}
void SoundService::SetEnabled(bool) {}
void SoundService::SetMasterVolume(float fx,float music,float speech) {
    fFXVolume=std::clamp(fx,0.f,1.f);fMusicVolume=std::clamp(music,0.f,1.f);fSpeechVolume=std::clamp(speech,0.f,1.f);
    if(audio)audio->categories(fFXVolume,fMusicVolume,fSpeechVolume);
}
void SoundService::GetMasterVolume(float* fx,float* music,float* speech) {*fx=fFXVolume;*music=fMusicVolume;*speech=fSpeechVolume;}
void SoundService::SetPitch(float value) {fPitch=std::max(0.f,value);if(audio)audio->pitch(fPitch);}
float SoundService::GetPitch() {return fPitch;}
void SoundService::SetCameraPosition(const CVECTOR& p) {listenerPos={p.x,p.y,p.z};if(audio)audio->listener(listenerPos,listenerForward,listenerUp);}
void SoundService::SetCameraOrientation(const CVECTOR& nose,const CVECTOR& head) {
    auto f=!nose;auto u=!head;listenerForward={f.x,f.y,f.z};listenerUp={u.x,u.y,u.z};
    if(audio)audio->listener(listenerPos,listenerForward,listenerUp);
}
void SoundService::SetActiveWithFade(bool active) {if(audio)audio->active(active,fadeTimeInSeconds);}
void SoundService::DebugDraw() {
    if(core.Controls->GetDebugAsyncKeyState('J')<0) {bShowDebugInfo=!bShowDebugInfo;std::this_thread::sleep_for(std::chrono::milliseconds(200));}
    if(!bShowDebugInfo || !audio)return;
    rs->Print(0,0,"Open audio: physical %u / logical %u; CPU/memory metrics unavailable",audio->physicalCount(),audio->logicalCount());
    rs->Print(0,16,"Sound schemes %u",unsigned(SoundSchemeChannels.size()));
}
#endif
