#pragma once

#include "storm_glm.h"
#include "service.h"
#include "sound_defines.h"
#include "v_sound_service.h"
#include <memory>
#include <vector>

///////////////////////////////////////////////////////////////////
// DEFINES & TYPES
///////////////////////////////////////////////////////////////////

class TSD_ID
{
  public:
    TSD_ID() : id_(0xFFFFFFFF)
    {
    }

    TSD_ID(uint32_t id) : id_(id)
    {
    }

    TSD_ID(int32_t id) : id_(id)
    {
    }

    bool master()
    {
        return id_ == 0;
    }

    uint16_t index() const
    {
        return (id_ & 0xFFFF) - 1;
    }

    uint16_t stamp() const
    {
        return (id_ >> 16) & 0xFFFF;
    }

    static TSD_ID createId(uint16_t index)
    {
        static uint16_t stamp_counter;
        return (index + 1) | (static_cast<uint32_t>(++stamp_counter) << 16);
    }

    operator int32_t()
    {
        return id_;
    }

  private:
    uint32_t id_;
};

using tSoundStatistics = struct
{
    int32_t soundsCount, maxSoundsCount, bytesInBuffers, maxBytesInBuffers, bytesCached, maxBytesCached, totalMem,
        freeMem;
};

// Decoded PCM pushed by a media player (video soundtracks): interleaved stereo float at
// 48 kHz. It plays outside the game's sound slots (MSG_SOUND_STOP and pitch do not affect it)
// but follows the master fades, background suspension and ducking. Destroy before the service.
class VSoundStream
{
  public:
    static constexpr uint32_t kRate = 48000;
    virtual ~VSoundStream() = default;
    // Any single producer thread. Frames accepted; fewer when the buffer (one second) is full.
    virtual uint32_t Write(const float *pcm, uint32_t frames) = 0;
    // Frames played so far; the media player's clock. Any thread.
    virtual uint64_t PlayedFrames() = 0;
    virtual void SetPaused(bool paused) = 0;
    virtual void SetVolume(float volume) = 0;
};

///////////////////////////////////////////////////////////////////
// INTERFACE DEFINITION
///////////////////////////////////////////////////////////////////
class VSoundService : public SERVICE
{
  public:
    bool Init() override = 0;
    uint32_t RunSection() override = 0;
    void RunStart() override = 0;

    // Sound entries functions
    //
    // + play returns id of new loaded sound
    // + in most functions _id = 0 equals "apply to all"
    // + play assumes _name a plain file name
    // + for now loop_pause_time works only for OGG-STEREO sounds
    ///////////////////////////////////////////////////////////////
    virtual TSD_ID SoundPlay(const char *_name,
                             eSoundType _type,          // sound type
                             eVolumeType _volumeType,   // volume type
                             bool _simpleCache = false, // cache only, not play
                             bool _looped = false,      // looped?
                             bool _cached = false,      // unload after stoppping?
                             int32_t _time = 0,         // fade in, if _time > 0
                             const glm::vec3 *_startPosition = nullptr, float _minDistance = -1.0f,
                             float _maxDistance = -1.0f, int32_t _loopPauseTime = 0, float _volume = 1.0f,
                             int32_t _prior = 128) = 0;

    virtual TSD_ID SoundDuplicate(TSD_ID _sourceID) = 0;
    virtual void SoundSet3DParam(TSD_ID _id, eSoundMessage _message, const void *_op) = 0;
    virtual void SoundStop(TSD_ID _id, int32_t _time = 0) = 0;
    virtual void SoundRelease(TSD_ID _id) = 0;
    virtual void SoundSetVolume(TSD_ID _id, float _volume) = 0;
    virtual bool SoundIsPlaying(TSD_ID _id) = 0;
    virtual float SoundGetPosition(TSD_ID _id) = 0;
    virtual void SoundRestart(TSD_ID _id) = 0;
    virtual void SoundResume(TSD_ID _id, int32_t _time = 0) = 0;

    // Service functions
    virtual void SetMasterVolume(float _fxVolume, float _musicVolume, float _speechVolume) = 0;
    virtual void GetMasterVolume(float *_fxVolume, float *_musicVolume, float *_speechVolume) = 0;
    virtual void SetPitch(float _pitch) = 0;
    virtual float GetPitch() = 0;
    virtual void SetCameraPosition(const glm::vec3 &_cameraPosition) = 0;
    virtual void SetCameraOrientation(const glm::vec3 &_nose, const glm::vec3 &_head) = 0;

    virtual void ResetScheme() = 0;
    virtual bool SetScheme(const char *_schemeName) = 0;
    virtual bool AddScheme(const char *_schemeName) = 0;

    virtual void SetEnabled(bool _enabled) = 0;
    virtual void LoadAliasFile(const char *_filename) = 0;

    virtual void SetActiveWithFade(bool active) = 0;

    // Attenuate everything by `gain` (0..1) on top of the regular volumes, e.g.
    // while another app holds transient audio focus. Backends without support ignore it.
    virtual void SetDucking(float gain)
    {
    }

    // Drop cached sound data that is not in use (memory pressure). Returns bytes released.
    virtual uint64_t TrimCaches()
    {
        return 0;
    }

    // nullptr when the audio output failed to initialise (already logged by the service).
    virtual std::unique_ptr<VSoundStream> CreatePcmStream() = 0;

    tSoundStatistics soundStatistics;
};

/*
API_SERVICE_START("sound service")
    DECLARE_MAIN_SERVICE(SoundService)
API_SERVICE_END(SoundService)
*/
