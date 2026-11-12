#pragma once
#include "miniaudio.h"
#include <stdexcept>
#include <algorithm>
namespace storm_audio {
inline void check(ma_result r) { if (r != MA_SUCCESS) throw std::runtime_error(ma_result_description(r)); }
// Starts (true) or stops (false) the playback device of an engine. Tests replace it to inject
// device failures (Backend::device).
using DeviceControl = ma_result (*)(ma_engine*, bool start);
inline ma_result controlDevice(ma_engine* engine, bool start) { return start ? ma_engine_start(engine) : ma_engine_stop(engine); }
inline ma_result noDevice(ma_engine*, bool) { return MA_SUCCESS; }
// Shared miniaudio primitive used by the isolated checks and open SoundService.
class Backend {
    ma_context context{};
    bool hasContext = false;
    bool offline;
    bool suspended = false;
public:
    ma_engine engine{};
    DeviceControl device;
    explicit Backend(bool offline) : offline(offline), device(offline ? noDevice : controlDevice) {
        auto config = ma_engine_config_init();
        config.channels = 2;
        config.sampleRate = 48000;
        config.noDevice = offline;
        if (!offline) {
#ifdef __ANDROID__
            const ma_backend backends[] = {ma_backend_aaudio, ma_backend_opensl};
#else
            const ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa};
#endif
            check(ma_context_init(backends, sizeof(backends)/sizeof(backends[0]), nullptr, &context));
            hasContext = true;
            config.pContext = &context; // Explicit list excludes null fallback.
        }
        auto result = ma_engine_init(&config, &engine);
        if (result != MA_SUCCESS) {
            if (hasContext) ma_context_uninit(&context);
            check(result);
        }
    }
    ~Backend() { ma_engine_uninit(&engine); if (hasContext) ma_context_uninit(&context); }
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    // MA_SUCCESS, or the device's error with the state unchanged. A device stops and starts on
    // focus and lifecycle changes, where the platform may refuse it (AAudio/OpenSL errors).
    ma_result suspend() { const ma_result r = device(&engine, false); if (r == MA_SUCCESS) suspended = true; return r; }
    ma_result resume() { const ma_result r = device(&engine, true); if (r == MA_SUCCESS) suspended = false; return r; }
    // Game/control thread only. Device rendering remains owned by miniaudio.
    void read(float* pcm, ma_uint64 frames) {
        if (!offline) throw std::runtime_error("manual PCM read requires offline mode");
        if (suspended) std::fill_n(pcm, frames*2, 0.0f);
        else check(ma_engine_read_pcm_frames(&engine, pcm, frames, nullptr));
    }
};
class Sound {
public:
    ma_sound sound{};
    Sound(Backend& b, const char* file, bool stream, bool spatial, ma_sound_group* group = nullptr) {
        check(ma_sound_init_from_file(&b.engine, file,
            MA_SOUND_FLAG_WAIT_INIT | (stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE) |
            (spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION), group, nullptr, &sound));
    }
    ~Sound() { ma_sound_uninit(&sound); }
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    void play() { check(ma_sound_start(&sound)); }
    void pause() { check(ma_sound_stop(&sound)); }
    void seek(ma_uint64 frame) { check(ma_sound_seek_to_pcm_frame(&sound, frame)); }
    ma_uint64 cursor() { ma_uint64 n; check(ma_sound_get_cursor_in_pcm_frames(&sound, &n)); return n; }
};
}
