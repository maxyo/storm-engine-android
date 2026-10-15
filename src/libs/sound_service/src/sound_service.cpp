#ifndef STORM_OPEN_AUDIO
#include "sound_service.h"

#include <thread>

#include "matrix.h"
#include "rands.h"
#include "v_file_service.h"
#include "vma.hpp"
#include <fmod_errors.h>

// for debugging
#include "core.h"

#include "debug_entity.h"
#include "math3d/color.h"
#include "math_inlines.h"





#define DISTANCEFACTOR 1.0f
#define CHECKFMODERR(expr) ErrorHandler(expr, __FILE__, __LINE__, __func__, #expr)

namespace
{

FMOD_RESULT ErrorHandler(const FMOD_RESULT result, const char *file, unsigned line, const char *func, const char *expr)
{
    if (result != FMOD_OK)
    {
        core.Trace("[%s:%s:%d] %s (%s)", file, func, line, FMOD_ErrorString(result), expr);
    }
    return result;
}

} // namespace

SoundService::SoundService()
{
    fFXVolume = 0.5f;
    fMusicVolume = 0.5f;
    fSpeechVolume = 0.5f;

    fPitch = 1.0f;

    bShowDebugInfo = false;
    OGG_sound[0] = nullptr;
    OGG_sound[1] = nullptr;
    system = nullptr;
    initialized = false;

    vListenerPos.x = 0.0f;
    vListenerPos.y = 0.0f;
    vListenerPos.z = 0.0f;

    vListenerForward.x = 0.0f;
    vListenerForward.y = 0.0f;
    vListenerForward.z = 0.0f;

    vListenerTop.x = 0.0f;
    vListenerTop.y = 0.0f;
    vListenerTop.z = 0.0f;

    FaderParity = false;
}

SoundService::~SoundService()
{
    if (initialized)
    {
        CHECKFMODERR(system->close());
        CHECKFMODERR(system->release());
    }
}

bool SoundService::Init()
{
    initialized = false;

    rs = static_cast<VDX9RENDER *>(core.GetService("DX9RENDER"));

    if (rs == nullptr) {
        return false;
    }

    CHECKFMODERR(FMOD::System_Create(&system));
    unsigned version;
    CHECKFMODERR(system->getVersion(&version));

    if (version < FMOD_VERSION)
    {
        core.Trace("Error : You are using old FMOD version %08x !\n", version);
        return false;
    }
    core.Trace("Using FMOD %08x", FMOD_VERSION);
    CHECKFMODERR(system->setSoftwareChannels(MAX_SOUNDS_SLOTS));
    CHECKFMODERR(system->setOutput(FMOD_OUTPUTTYPE_AUTODETECT));
    CHECKFMODERR(system->init(MAX_SOUNDS_SLOTS, FMOD_INIT_NORMAL, nullptr));
    CHECKFMODERR(system->set3DSettings(1.0, DISTANCEFACTOR, 1.0f));

    if (const auto ini = fio->OpenIniFile(core.EngineIniFileName()))
    {
        fadeTimeInSeconds = ini->GetFloat("sound", "fade_time", 0.5f);
    }

    numActiveSounds = 2; // 0 and 1 are special

    InitAliases();
    CreateEntityIfNeed();

    initialized = true;
    return true;
}

void SoundService::RunEnd()
{
    CreateEntityIfNeed();
    // Internal FMOD update
    CHECKFMODERR(system->update());
}

void SoundService::ProcessFader(uint16_t idx)
{
    if (PlayingSounds[idx].bFree)
    {
        return;
    }

    if (fabsf(PlayingSounds[idx].fFaderCurrentVolume - PlayingSounds[idx].fFaderNeedVolume) < 0.001f)
    {
        if (PlayingSounds[idx].fFaderDeltaInSec < 0)
        {
            unsigned int OGGpos;
            PlayingSounds[idx].channel->getPosition(&OGGpos, FMOD_TIMEUNIT_MS);
            SetOGGPosition(PlayingSounds[idx].Name.c_str(), OGGpos);
            PlayingSounds[idx].channel->stop();
        }
        return;
    }

    const auto fTime = core.GetDeltaTime() * 0.001f;

    PlayingSounds[idx].fFaderCurrentVolume += PlayingSounds[idx].fFaderDeltaInSec * fTime;

    // FADE IN
    if (PlayingSounds[idx].fFaderDeltaInSec > 0)
    {
        if (PlayingSounds[idx].fFaderCurrentVolume > PlayingSounds[idx].fFaderNeedVolume)
        {
            PlayingSounds[idx].fFaderCurrentVolume = PlayingSounds[idx].fFaderNeedVolume;
        }
    }

    // FADE OUT
    if (PlayingSounds[idx].fFaderDeltaInSec < 0)
    {
        if (PlayingSounds[idx].fFaderCurrentVolume < PlayingSounds[idx].fFaderNeedVolume)
        {
            PlayingSounds[idx].fFaderCurrentVolume = PlayingSounds[idx].fFaderNeedVolume;
            unsigned int OGGpos;
            PlayingSounds[idx].channel->getPosition(&OGGpos, FMOD_TIMEUNIT_MS);
            SetOGGPosition(PlayingSounds[idx].Name.c_str(), OGGpos);

            PlayingSounds[idx].channel->stop();
        }
    }
    PlayingSounds[idx].channel->setVolume(PlayingSounds[idx].fFaderCurrentVolume);
}

uint16_t SoundService::FreeSound(const uint16_t idx)
{
    PlayingSounds[idx].bFree = true;
    if (idx >= 2 && idx < numActiveSounds)
    {
        freeSounds.push(idx);
    }

    return idx;
}

void SoundService::RunStart()
{
    CreateEntityIfNeed();

    if (rs)
    {
        static CVECTOR pos, ang, nose, head;
        static CMatrix view;

        rs->GetTransform(D3DTS_VIEW, view);
        view.Transposition();
        nose = view.Vz();
        head = view.Vy();
        pos = view.Pos();

        SetCameraPosition(pos);
        SetCameraOrientation(nose, head);
    }

    ProcessFader(0);
    ProcessFader(1);

    // release the sounds that have played ...
    for (uint16_t i = 0; i < numActiveSounds; i++)
    {
        if (PlayingSounds[i].bFree)
            continue;

        // If it's just paused, don't need to touch it ...
        bool paused = true;

        FMOD_RESULT status = PlayingSounds[i].channel->getPaused(&paused);
        if (status != FMOD_OK)
        {
            if constexpr (TRACE_INFORMATION)
                core.Trace("PlayingSounds[%d].channel 0x%08X %s paused %d status %d", i, PlayingSounds[i].channel,
                           PlayingSounds[i].Name.c_str(), paused, status);
            i = FreeSound(i);

            continue;
        }

        if (paused)
            continue;

        bool is_playing;
        status = CHECKFMODERR(PlayingSounds[i].channel->isPlaying(&is_playing));

        if (!is_playing || status != FMOD_OK)
        {
            PlayingSounds[i].channel = nullptr;

            if (i == 0)
            {
                if (OGG_sound[0] != nullptr)
                {
                    CHECKFMODERR(OGG_sound[0]->release());
                    OGG_sound[0] = nullptr;
                }
            }
            else if (i == 1)
            {
                if (OGG_sound[1] != nullptr)
                {
                    CHECKFMODERR(OGG_sound[1]->release());
                    OGG_sound[1] = nullptr;
                }
            }

            i = FreeSound(i);
            core.Event("SoundEnded", "l", i + 2);
        }
    }
    ProcessSoundSchemes();
}

bool SoundService::AllocateSound(TSD_ID &id)
{
    if (!freeSounds.empty())
    {
        id = TSD_ID::createId(freeSounds.top());
        freeSounds.pop();
        return true;
    }

    if (numActiveSounds > MAX_SOUNDS_SLOTS)
    {
        core.Trace("SoundService::AllocateSound(): no empty slots!");
        return false;
    }

    id = TSD_ID::createId(numActiveSounds++);
    return true;
}





TSD_ID SoundService::SoundPlay(const char *_name, eSoundType _type, eVolumeType _volumeType,
                               bool _simpleCache /* = false*/, bool _looped /* = false*/, bool _cached /* = false*/,
                               int32_t _time /* = 0*/, const CVECTOR *_startPosition /* = 0*/,
                               float _minDistance /* = -1.0f*/, float _maxDistance /* = -1.0f*/,
                               int32_t _loopPauseTime /* = 0*/, float _volume, /* = 1.0f*/
                               int32_t _prior)
{
    std::string FileName = _name;

    // aliases don`t contain `\`
    if (!strchr(_name, '\\'))
    {
        // Trying to find in aliases
        const auto AliasIdx = GetAliasIndexByName(_name);
        if (AliasIdx >= 0 && !Aliases[AliasIdx].soundFiles.empty())
        {
            // play sound from the alias ...
            FileName = GetRandomName(&Aliases[AliasIdx]);
            if constexpr (TRACE_INFORMATION)
                core.Trace("Play sound from alias %s", FileName.c_str());

            _minDistance = Aliases[AliasIdx].fMinDistance;
            _maxDistance = Aliases[AliasIdx].fMaxDistance;
            _prior = Aliases[AliasIdx].iPrior;

            if (Aliases[AliasIdx].fVolume > 0.0f)
            {
                _volume = Aliases[AliasIdx].fVolume;
            }
        }
    }

    std::string SoundName = "resource\\sounds\\";
    SoundName += FileName;
    SoundName = fio->ConvertPathResource(SoundName.c_str());

    FMOD::Sound *sound = nullptr;
    uint16_t SoundIdx = 0;

    TSD_ID id;
    if (_type == MP3_STEREO)
    {
        // play streamed immediately, without caching and always in 0 slot ...
        try
        {
            uint32_t dwMode = FMOD_LOOP_OFF;
            if (_looped)
                dwMode = FMOD_LOOP_NORMAL;
            const auto status = CHECKFMODERR(
                system->createStream(SoundName.c_str(), FMOD_CREATESAMPLE | FMOD_2D | dwMode, nullptr, &sound));
            if (status != FMOD_OK)
            {
                core.Trace("system->createStream(%s, FMOD_HARDWARE | FMOD_2D | dwMode, 0, &sound)", SoundName.c_str());
                return 0;
            }
        }
        catch (...)
        {
            core.Trace("Internal FMOD error, when create stream. File '%s'", SoundName.c_str());
            return 0;
        }

        auto MusicIdx = 0;
        auto OldMusicIdx = 1;
        if (FaderParity)
        {
            MusicIdx = 1;
            OldMusicIdx = 0;
        }
        FaderParity = !FaderParity;

        if (OGG_sound[OldMusicIdx])
        {
            SoundStop(OldMusicIdx + 1, _time);
        }

        SoundIdx = MusicIdx;
        id = SoundIdx + 1;
        OGG_sound[SoundIdx] = sound;
        PlayingSounds[SoundIdx].fFaderNeedVolume = _volume * fMusicVolume;
        PlayingSounds[SoundIdx].fFaderCurrentVolume = 0.0f;
        PlayingSounds[SoundIdx].fFaderDeltaInSec = (_volume * fMusicVolume) / (_time * 0.001f);
    }
    else
    {
        // For all other sounds, take from the cache
        const auto CacheIdx = GetFromCache(SoundName.c_str(), _type);
        if (CacheIdx < 0)
        {
            return 0;
        }

        bool success = AllocateSound(id);
        if (!success)
        {
            return 0;
        }
        SoundIdx = id.index();
        PlayingSounds[SoundIdx].stamp = id.stamp();
        sound = SoundCache[CacheIdx].sound;
    }

    //--------
    PlayingSounds[SoundIdx].type = _volumeType;
    PlayingSounds[SoundIdx].fSoundVolume = _volume;

    // start to play the sound, but paused ...
    const auto status = CHECKFMODERR(system->playSound(sound, nullptr, true, &PlayingSounds[SoundIdx].channel));
    if (status != FMOD_OK)
    {
        core.Trace("system->playSound(FMOD_CHANNEL_FREE, sound, true, &PlayingSounds[%d].channel)", SoundIdx);
    }

    if (SoundIdx <= 1)
    {
        const auto OGGpos = GetOGGPosition(SoundName.c_str());
        PlayingSounds[SoundIdx].channel->setPosition(OGGpos, FMOD_TIMEUNIT_MS);

        _prior = 0;
    }

    if (_prior < 0)
        _prior = 0;
    if (_prior > 255)
        _prior = 255;

    // put priority ...
    PlayingSounds[SoundIdx].channel->setPriority(_prior);

    // Adjust parameters for 3D channel ...
    if (_type == PCM_3D)
    {
        // If necessary, set the parameters by default ...
        if (_minDistance < 0.0f)
            _minDistance = 0.0f;
        if (_maxDistance < 0.0f)
            _maxDistance = 0.0f;

        CHECKFMODERR(PlayingSounds[SoundIdx].channel->set3DMinMaxDistance(_minDistance * DISTANCEFACTOR,
                                                                          _maxDistance * DISTANCEFACTOR));

        FMOD_VECTOR vVelocity = {0.0f, 0.0f, 0.0f};
        FMOD_VECTOR vPosition{};
        if (_startPosition != nullptr)
        {
            vPosition.x = _startPosition->x;
            vPosition.y = _startPosition->y;
            vPosition.z = _startPosition->z;
        }
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->set3DAttributes(&vPosition, &vVelocity));
    }

    switch (_volumeType)
    {
    case VOLUME_FX:
        _volume *= fFXVolume;
        break;
    case VOLUME_MUSIC:
        _volume *= fMusicVolume;
        break;
    case VOLUME_SPEECH:
        _volume *= fSpeechVolume;
        break;
    default:
        _volume *= 1.0f;
        break;
    }

    if (_time <= 0)
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setVolume(_volume));
    }
    else
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setVolume(0));
    }

    CHECKFMODERR(PlayingSounds[SoundIdx].channel->setPitch(fPitch));

    PlayingSounds[SoundIdx].Name = std::move(SoundName);
    PlayingSounds[SoundIdx].sound_type = _type;

    // If not just caching ... then unpause ...
    if (!_simpleCache)
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setPaused(false));
    }
    else
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setPaused(true));
    }

    PlayingSounds[SoundIdx].bFree = false;
    // set the looping ..
    if (_looped)
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setMode(FMOD_LOOP_NORMAL));
    }
    else
    {
        CHECKFMODERR(PlayingSounds[SoundIdx].channel->setMode(FMOD_LOOP_OFF));
    }

    // ---------- loop through all sounds looking for the one with the same channel --------------
    for (uint16_t j = 0; j < numActiveSounds; j++)
    {
        if (j == SoundIdx)
            continue;

        if (PlayingSounds[j].channel == PlayingSounds[SoundIdx].channel)
        {
            // note that the sound is thrown out ...
            // so as not to stop him ...
            j = FreeSound(j);
        }
    }
    // Returning the sound ID ...

    return id;
}

void SoundService::SoundSet3DParam(TSD_ID _id, eSoundMessage _message, const void *_op)
{
    if (_id.master() || _id.index() > numActiveSounds)
        return;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return;

    auto vVelocity = CVECTOR(0.0f, 0.0f, 0.0f);

    switch (_message)
    {
    case SM_MAX_DISTANCE: {
        float maxDistance = *((float *)_op);
        CHECKFMODERR(sound.channel->set3DMinMaxDistance(NULL, maxDistance));
        break;
    }

    case SM_MIN_DISTANCE: {
        float minDistance = *((float *)_op);
        CHECKFMODERR(sound.channel->set3DMinMaxDistance(minDistance, NULL));
        break;
    }

    case SM_POSITION: {
        FMOD_VECTOR pos;
        auto *fPtr = (float *)_op;
        pos.x = *(fPtr + 0);
        pos.y = *(fPtr + 1);
        pos.z = *(fPtr + 2);
        FMOD_VECTOR vVelocity = {0.0f, 0.0f, 0.0f};
        CHECKFMODERR(sound.channel->set3DAttributes(&pos, &vVelocity));
        break;
    }
    }
}

void SoundService::SoundRestart(TSD_ID _id)
{
    if constexpr (TRACE_INFORMATION)
        core.Trace("Sound restart !");
}

void SoundService::SoundRelease(TSD_ID _id)
{
    if constexpr (TRACE_INFORMATION)
        core.Trace("Sound release !");
}

void SoundService::SoundSetVolume(TSD_ID _id, float _volume)
{
    if constexpr (TRACE_INFORMATION)
        core.Trace("Sound set volume !");

    if (_id.master())
    {
        for (uint16_t i = 0; i < numActiveSounds; i++)
        {
            if (PlayingSounds[i].bFree)
                continue;

            PlayingSounds[i].fSoundVolume = _volume;

            switch (PlayingSounds[i].type)
            {
            case VOLUME_FX:
                _volume *= fFXVolume;
                break;
            case VOLUME_MUSIC:
                _volume *= fMusicVolume;
                break;
            case VOLUME_SPEECH:
                _volume *= fSpeechVolume;
                break;
            default:
                _volume *= 1.0f;
                break;
            }

            if (i <= 1)
            {
                PlayingSounds[i].fFaderNeedVolume = _volume;
                PlayingSounds[i].fFaderCurrentVolume = PlayingSounds[i].fFaderNeedVolume;
            }

            CHECKFMODERR(PlayingSounds[i].channel->setVolume(_volume));
        }
        return;
    }

    if (_id.index() >= numActiveSounds)
        return;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return;

    if (_id.index() <= 1)
    {
        if (sound.fFaderCurrentVolume - sound.fFaderNeedVolume > 0.001f)
        {
            return;
        }
    }

    sound.fSoundVolume = _volume;

    switch (sound.type)
    {
    case VOLUME_FX:
        _volume *= fFXVolume;
        break;
    case VOLUME_MUSIC:
        _volume *= fMusicVolume;
        break;
    case VOLUME_SPEECH:
        _volume *= fSpeechVolume;
        break;
    default:
        _volume *= 1.0f;
        break;
    }
    CHECKFMODERR(sound.channel->setVolume(_volume));
}

bool SoundService::SoundIsPlaying(TSD_ID _id)
{
    if (_id.master() || _id.index() >= numActiveSounds)
        return false;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return false;

    return !sound.bFree;
}

void SoundService::SoundResume(TSD_ID _id, int32_t _time /* = 0*/)
{
    // TODO: weird cond
    if (_id.master() || _id.index() == 0 || _id == -1)
    {
        for (uint16_t i = 0; i < numActiveSounds; i++)
        {
            if (PlayingSounds[i].bFree)
                continue;

            CHECKFMODERR(PlayingSounds[i].channel->setPaused(false));
        }
        return;
    }

    if (_id.index() >= numActiveSounds)
        return;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return;

    CHECKFMODERR(sound.channel->setPaused(false));
}

float SoundService::SoundGetPosition(TSD_ID _id)
{
    if (_id.index() >= numActiveSounds)
        return 0;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return 0;

    unsigned int SoundPositionInMilisecond;
    CHECKFMODERR(sound.channel->getPosition(&SoundPositionInMilisecond, FMOD_TIMEUNIT_MS));
    return 0;
}

void SoundService::SetCameraPosition(const CVECTOR &_cameraPosition)
{
    vListenerPos.x = _cameraPosition.x;
    vListenerPos.y = _cameraPosition.y;
    vListenerPos.z = _cameraPosition.z;

    CHECKFMODERR(system->set3DListenerAttributes(0, &vListenerPos, nullptr, nullptr, nullptr));
}

void SoundService::SetCameraOrientation(const CVECTOR &_nose, const CVECTOR &_head)
{
    const auto nose = !_nose;
    const auto head = !_head;

    vListenerForward.x = nose.x;
    vListenerForward.y = nose.y;
    vListenerForward.z = nose.z;

    vListenerTop.x = head.x;
    vListenerTop.y = head.y;
    vListenerTop.z = head.z;

    CHECKFMODERR(system->set3DListenerAttributes(0, nullptr, nullptr, &vListenerForward, &vListenerTop));
}

void SoundService::SetMasterVolume(float _fxVolume, float _musicVolume, float _speechVolume)
{
    if (_fxVolume < 0.0f)
        _fxVolume = 0.0f;
    if (_fxVolume > 1.0f)
        _fxVolume = 1.0f;

    if (_musicVolume < 0.0f)
        _musicVolume = 0.0f;
    if (_musicVolume > 1.0f)
        _musicVolume = 1.0f;

    if (_speechVolume < 0.0f)
        _speechVolume = 0.0f;
    if (_speechVolume > 1.0f)
        _speechVolume = 1.0f;

    fFXVolume = _fxVolume;
    fMusicVolume = _musicVolume;
    fSpeechVolume = _speechVolume;

    for (uint16_t i = 0; i < numActiveSounds; i++)
    {
        if (PlayingSounds[i].bFree)
            continue;

        // update volume.....
        float _volume = PlayingSounds[i].fSoundVolume;

        switch (PlayingSounds[i].type)
        {
        case VOLUME_FX:
            _volume *= fFXVolume;
            break;
        case VOLUME_MUSIC:
            _volume *= fMusicVolume;
            break;
        case VOLUME_SPEECH:
            _volume *= fSpeechVolume;
            break;
        default:
            _volume *= 1.0f;
            break;
        }

        const auto status = CHECKFMODERR(PlayingSounds[i].channel->setVolume(_volume));
        if (status != FMOD_OK)
        {
            i = FreeSound(i);
        }
    }
    if constexpr (TRACE_INFORMATION)
        core.Trace("Set master volume");
}

void SoundService::GetMasterVolume(float *_fxVolume, float *_musicVolume, float *_speechVolume)
{
    *_fxVolume = fFXVolume;
    *_musicVolume = fMusicVolume;
    *_speechVolume = fSpeechVolume;
    if constexpr (TRACE_INFORMATION)
        core.Trace("Get master volume");
}

void SoundService::SetPitch(float _pitch)
{
    fPitch = (_pitch >= 0.0f) ? _pitch : 0.0f;

    for (uint16_t i = 0; i < numActiveSounds; i++)
    {
        if (PlayingSounds[i].bFree)
            continue;

        const auto status = CHECKFMODERR(PlayingSounds[i].channel->setPitch(fPitch));
        if (status != FMOD_OK)
        {
            i = FreeSound(i);
        }
    }

    if constexpr (TRACE_INFORMATION)
        core.Trace("Set pitch");
}

float SoundService::GetPitch()
{
    if constexpr (TRACE_INFORMATION)
        core.Trace("Get pitch");
    return fPitch;
}

TSD_ID SoundService::SoundDuplicate(TSD_ID _sourceID)
{
    core.Trace("Sound duplicate");
    return 0;
}

void SoundService::SetEnabled(bool _enabled)
{
}

void SoundService::SetActiveWithFade(const bool active)
{
    if (fadeTimeInSeconds == 0.0f)
    {
        return;
    }

    if (system == nullptr) {
        return;
    }

    if (active)
    {
        system->mixerResume();
    }

    FMOD::ChannelGroup *mastergroup;
    CHECKFMODERR(system->getMasterChannelGroup(&mastergroup));
    unsigned long long parentclock; // type should match with getDSPClock param from fmod.hpp
    int rate;
    system->getSoftwareFormat(&rate, nullptr, nullptr);
    mastergroup->getDSPClock(nullptr, &parentclock);

    const auto dsp_clock_start = parentclock;
    const auto dsp_clock_end = static_cast<unsigned long long>(parentclock + fadeTimeInSeconds * rate);

    if (active)
    {
        mastergroup->setDelay(dsp_clock_start, 0, false);
    }

    for (const auto &PlayingSound : PlayingSounds)
    {
        if (PlayingSound.bFree)
            continue;

        if (active)
        {
            PlayingSound.channel->addFadePoint(dsp_clock_start, 0.0f);
            PlayingSound.channel->addFadePoint(dsp_clock_end, 1.0f);
        }
        else
        {
            PlayingSound.channel->addFadePoint(dsp_clock_start, 1.0f);
            PlayingSound.channel->addFadePoint(dsp_clock_end, 0.0f);
        }
    }

    if (!active)
    {
        mastergroup->setDelay(dsp_clock_start, dsp_clock_end, false);
        system->mixerSuspend();
    }
}

void SoundService::SoundStop(TSD_ID _id, int32_t _time)
{
    bool is_playing; // boal fix

    if (_id.master())
    {
        // --------- remove all sounds -----------------------------------------
        int start = 0;
        if (_time > 0)
        {
            float fVol = 0.0f;
            PlayingSounds[0].channel->getVolume(&fVol);
            PlayingSounds[0].fFaderNeedVolume = 0.0f;
            PlayingSounds[0].fFaderCurrentVolume = fVol;
            PlayingSounds[0].fFaderDeltaInSec = -fVol / (_time * 0.001f);

            if (PlayingSounds[0].fFaderDeltaInSec <= 0.00001f)
            {
                if (OGG_sound[0])
                {
                    CHECKFMODERR(OGG_sound[0]->release());
                    OGG_sound[0] = nullptr;
                }
            }

            PlayingSounds[1].channel->getVolume(&fVol);
            PlayingSounds[1].fFaderNeedVolume = 0.0f;
            PlayingSounds[1].fFaderCurrentVolume = fVol;
            PlayingSounds[1].fFaderDeltaInSec = -fVol / (_time * 0.001f);

            if (PlayingSounds[1].fFaderDeltaInSec <= 0.00001f)
            {
                if (OGG_sound[1])
                {
                    CHECKFMODERR(OGG_sound[1]->release());
                    OGG_sound[1] = nullptr;
                }
            }
            start = 2;
        }
        else
        {
            start = 0;
        }

        for (uint16_t i = start; i < numActiveSounds; i++)
        {
            if (PlayingSounds[i].bFree)
                continue;

            if (i <= 1)
            {
                unsigned int OGGpos;
                PlayingSounds[i].channel->getPosition(&OGGpos, FMOD_TIMEUNIT_MS);
                SetOGGPosition(PlayingSounds[i].Name.c_str(), OGGpos);
            }

            FMOD_RESULT status = PlayingSounds[i].channel->isPlaying(&is_playing);
            if (!is_playing || status != FMOD_OK) // boal fix
            {
                if constexpr (TRACE_INFORMATION)
                    core.Trace("PlayingSounds[%d].channel 0x%08X %s status %d", i, PlayingSounds[i].channel,
                               PlayingSounds[i].Name.c_str(), status);

                i = FreeSound(i);
            }
            else
            {
                CHECKFMODERR(PlayingSounds[i].channel->stop());
            }
        }

        if (_time <= 0)
        {
            if (OGG_sound[0])
            {
                CHECKFMODERR(OGG_sound[0]->release());
                OGG_sound[0] = nullptr;
            }

            if (OGG_sound[1])
            {
                CHECKFMODERR(OGG_sound[1]->release());
                OGG_sound[1] = nullptr;
            }
        }
        // --------- remove all sounds -----------------------------------------
        return;
    }

    // Delete the selected
    if (_id.index() >= numActiveSounds)
        return;

    auto &sound = PlayingSounds[_id.index()];

    if (_id.stamp() != sound.stamp)
        return;

    if (_time > 0)
    {
        float fVol = 0.0f;
        sound.channel->getVolume(&fVol);
        sound.fFaderNeedVolume = 0.0f;
        sound.fFaderCurrentVolume = fVol;
        sound.fFaderDeltaInSec = -fVol / (_time * 0.001f);
    }
    else
    {
        if (_id.index() <= 1)
        {
            unsigned int OGGpos;
            sound.channel->getPosition(&OGGpos, FMOD_TIMEUNIT_MS);
            SetOGGPosition(sound.Name.c_str(), OGGpos);
        }

        auto status = FMOD_OK;
        if (!sound.bFree) // boal fix
        {
            status = CHECKFMODERR(sound.channel->isPlaying(&is_playing));
            if (!is_playing || status != FMOD_OK) // boal fix
            {
                FreeSound(_id.index());
            }
            else
            {
                status = CHECKFMODERR(sound.channel->stop());
            }
        }

        if (_id.index() <= 1 && OGG_sound[_id.index()] && status == FMOD_OK)
        {
            CHECKFMODERR(OGG_sound[_id.index()]->release());
            OGG_sound[_id.index()] = nullptr;
        }
    }
}











void SoundService::DebugDraw()
{
    if (core.Controls->GetDebugAsyncKeyState('J') < 0)
    {
        bShowDebugInfo = !bShowDebugInfo;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!bShowDebugInfo)
        return;

    bool bShowDistances = true;

    int Ypos = 38 + 16;
    int Count = 0;
    int RealCount = 0;

    FMOD_VECTOR lpos, lvel, lforward, lup;
    system->get3DListenerAttributes(0, &lpos, &lvel, &lforward, &lup);

    auto vListener = CVECTOR(lpos.x, lpos.y, lpos.z);

    FMOD_CPU_USAGE usage;
    system->getCPUUsage(&usage);
    float fTotal = usage.dsp + usage.stream + usage.geometry + usage.update + usage.convolution1 + usage.convolution1;

    int CurrentAlloc, PeakAlloc;
    FMOD::Memory_GetStats(&CurrentAlloc, &PeakAlloc);

    CVECTOR list_pos;
    list_pos.x = lpos.x;
    list_pos.y = lpos.y;
    list_pos.z = lpos.z;
    // rs->DrawSphere(list_pos, 4.0f, 0xFF008000);

    rs->Print(0, 0, "CPU Usage %3.2f Mem: %3.2f Kb, MemPeak %3.2f Kb, Cached %d sounds", fTotal, CurrentAlloc / 1024.0f,
              PeakAlloc / 1024.0f, SoundCache.size());
    rs->Print(0, 16, "position  %3.2f, %3.2f, %3.2f, forward %3.2f, %3.2f, %3.2f, up %3.2f, %3.2f, %3.2f", lpos.x,
              lpos.y, lpos.z, lforward.x, lforward.y, lforward.z, lup.x, lup.y, lup.z);

    CMatrix ind;
    ind.SetIdentity();
    rs->SetWorld(ind);

    for (uint16_t i = 0; i < numActiveSounds; i++)
    {
        if (PlayingSounds[i].bFree)
            continue;

        bool bVirtual;
        PlayingSounds[i].channel->isVirtual(&bVirtual);

        bool paused;
        PlayingSounds[i].channel->getPaused(&paused);

        float fMin, fMax;
        PlayingSounds[i].channel->get3DMinMaxDistance(&fMin, &fMax);

        FMOD_MODE sound_mode;
        PlayingSounds[i].channel->getMode(&sound_mode);
        bool bIsLooped = false;
        if ((sound_mode & FMOD_LOOP_NORMAL) != 0)
            bIsLooped = true;

        float fVol;
        PlayingSounds[i].channel->getVolume(&fVol);

        bool bPlaying;
        PlayingSounds[i].channel->isPlaying(&bPlaying);

        unsigned int position;
        PlayingSounds[i].channel->getPosition(&position, FMOD_TIMEUNIT_MS);

        if (!bVirtual)
            RealCount++;
        Count++;

        FMOD_VECTOR pos, vel;
        PlayingSounds[i].channel->get3DAttributes(&pos, &vel);

        float audib = 0;
        PlayingSounds[i].channel->getAudibility(&audib);

        int prior;
        PlayingSounds[i].channel->getPriority(&prior);

        if (PlayingSounds[i].sound_type == PCM_3D)
        {
            // 0xFFFFFF00 plays but cannot be heard
            // 0xFFFF0000 not playing
            // 0xFF00FF00 is playing

            auto drawColor = Color(0xFFFF0000);

            if (!bVirtual)
            {
                // morph between yellow audib 0 and green audib 1 colors
                auto Zero = Color(0xFFFFFF00);
                auto Full = Color(0xFF00FF00);
                drawColor.Lerp(Zero, Full, audib);
            }

            CVECTOR vec_pos;
            vec_pos.x = pos.x;
            vec_pos.y = pos.y;
            vec_pos.z = pos.z;
            rs->DrawSphere(vec_pos, 0.2f, drawColor.GetDword());

            if (bShowDistances && audib > 0.001f)
            {
                rs->SetWorld(ind);
                Draw2DCircle(vec_pos, drawColor.GetDword(), fMin, drawColor.GetDword(), fMax);
            }

            float fDistance = CVECTOR(vListener - vec_pos).GetLength();

            DebugPrint3D(vec_pos, 30.0f, 2, 1.0f, drawColor.GetDword(), 1.0f, "%s", PlayingSounds[i].Name.c_str());
            DebugPrint3D(vec_pos, 30.0f, 0, 1.0f, drawColor.GetDword(), 1.0f, "%3.2f m", fDistance);
            DebugPrint3D(vec_pos, 30.0f, 4, 1.0f, drawColor.GetDword(), 1.0f, "prior: %d", prior);
        }
        else
        {
            rs->Print(0, Ypos,
                      "[%d] [vol:%f] [Pos:%d] [Loop:%d] [Play:%d] sound [Pause:%d][Vir:%d][Aud:%f] '%s' 0x%08X", i,
                      fVol, position, bIsLooped, bPlaying, paused, bVirtual, audib, PlayingSounds[i].Name.c_str(),
                      PlayingSounds[i].channel);
            Ypos += 16;
        }

        // rs->Print(0, Ypos, "pos %f, %f, %f", pos.x, pos.y, pos.z);
        // Ypos += 16;
        // rs->Print(0, Ypos, "min %f,  max %f", fMin, fMax);
        // Ypos += 16;

        // rs->DrawSphere(vec_pos, fMax, 0xFFFF0000);
    }

    rs->Print(0, 32, "Real sounds %d, Total sounds %d", RealCount, Count);
    rs->Print(800, 0, "Sound schemes %d", SoundSchemeChannels.size());
    Ypos = 16;
    for (size_t i = 0; i < SoundSchemeChannels.size(); i++)
    {
        rs->Print(810, Ypos, "[%d] %s", i, SoundSchemeChannels[i].soundName.c_str());
        Ypos += 16;
    }
}

int SoundService::GetFromCache(const char *szName, eSoundType _type)
{
    const uint32_t dwSearchHash = MakeHashValue(szName);

    for (size_t i = 0; i < SoundCache.size(); i++)
    {
        if (SoundCache[i].type != _type)
            continue;
        if (SoundCache[i].dwNameHash == dwSearchHash)
        {
            if (SoundCache[i].Name == szName)
            {
                SoundCache[i].fTimeFromLastPlay = 0.0;
                return i;
            }
        }
    }

    FMOD_MODE mode = FMOD_DEFAULT;
    if (_type == PCM_3D)
    {
        mode = mode | FMOD_3D | FMOD_3D_LINEARROLLOFF;
    }

    if (_type == PCM_STEREO)
    {
        mode = mode | FMOD_2D;
    }

    tSoundCache Cache;
    CHECKFMODERR(system->createSound(szName, mode, nullptr, &Cache.sound));

    if (Cache.sound == nullptr)
    {
        core.Trace("Problem with sound loading !!! '%s'", szName);
        return -1;
    }
    Cache.type = _type;
    Cache.Name = szName;
    Cache.dwNameHash = dwSearchHash;
    Cache.fTimeFromLastPlay = 0.0f;

    SoundCache.push_back(Cache);

    return (SoundCache.size() - 1);
}

// Write text




//--------------------------------------------------------------------




//--------------------------------------------------------------------


//--------------------------------------------------------------------






int SoundService::GetOGGPositionIndex(const char *szName)
{
    const uint32_t dwHash = MakeHashValue(szName);

    for (size_t i = 0; i < OGGPosition.size(); i++)
    {
        if (OGGPosition[i].dwHash == dwHash)
        {
            if (OGGPosition[i].Name == szName)
            {
                return i;
            }
        }
    }
    return -1;
}

unsigned int SoundService::GetOGGPosition(const char *szName)
{
    const int idx = GetOGGPositionIndex(szName);
    if (idx >= 0)
    {
        return OGGPosition[idx].position;
    }
    return 0;
}

void SoundService::SetOGGPosition(const char *szName, unsigned int pos)
{
    const int idx = GetOGGPositionIndex(szName);
    if (idx >= 0)
    {
        OGGPosition[idx].position = pos;
        return;
    }

    PlayedOGG ogg;
    ogg.dwHash = MakeHashValue(szName);
    ogg.Name = szName;
    ogg.position = pos;

    OGGPosition.push_back(ogg);
}

#endif
