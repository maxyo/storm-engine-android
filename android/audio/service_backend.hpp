#pragma once
#include <cstdint>
#include "backend.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace storm_audio {
struct Vec3 { float x=0, y=0, z=0; };
struct Play {
    bool stream=false, spatial=false, loop=false, paused=false;
    int category=1, priority=128;
    float volume=1, pitch=1, fade=0, minDistance=1, maxDistance=10000;
    Vec3 position;
};
// Memory and latency policy for decoded samples.
struct Limits {
    // Decoded PCM kept for reuse. Least recently used samples that no voice
    // references are released once the total exceeds this.
    uint64_t cacheBytes=48ull*1024*1024;
    // A sample whose decoded size exceeds this is never fully decoded: its
    // voices read through a miniaudio stream (about one second of PCM each).
    // 4 MiB is ~11 s of 48 kHz stereo or ~22 s of mono.
    uint64_t streamBytes=4ull*1024*1024;
    // Metadata-only entries (streams) are tiny; this only bounds the map.
    unsigned maxEntries=2048;
    // Decode new samples on a worker thread instead of inside play().
    // -1: on for a real device, off for the offline (test/PCM pump) mixer.
    int asyncDecode=-1;
};
// Interleaved stereo float PCM at the engine rate (48 kHz), pushed by one producer thread
// (a media decoder) and pulled by the mixer as a non-spatial sound in the master group, so
// background fades, suspension and ducking apply to it. It is not a Voice: stop(0), pitch and
// category gains leave it alone; its owner sets the volume. Must not outlive its Mixer.
class PcmStream {
public:
    PcmStream(Backend&, ma_sound_group* group, unsigned capacityFrames);
    ~PcmStream();
    PcmStream(const PcmStream&) = delete;
    PcmStream& operator=(const PcmStream&) = delete;
    // Producer thread. Frames accepted; fewer than offered when the buffer is full.
    unsigned write(const float* pcm, unsigned frames);
    // Frames handed to the mixer so far (any thread). Buffer underruns play silence and are
    // not counted, so this is a clock of the stream's own audio.
    uint64_t played() const { return consumed.load(std::memory_order_acquire); }
    void pause(bool paused);
    void volume(float gain);
private:
    struct Source { ma_data_source_base base; PcmStream* owner; };
    static ma_result read(ma_data_source*, void*, ma_uint64, ma_uint64*);
    static ma_result format(ma_data_source*, ma_format*, ma_uint32*, ma_uint32*, ma_channel*, size_t);
    Source source{};
    ma_pcm_rb ring{};
    ma_sound sound{};
    std::atomic<uint64_t> consumed{0};
};
// Game-thread owned. The only other threads touching miniaudio are its mixer,
// its resource-manager job thread and this class's decoder thread, which only
// calls the thread-safe resource manager and never creates sounds or nodes.
// 0 means all; slots 1 and 2 retain the game's fixed music IDs.
class Mixer {
public:
    static constexpr unsigned capacity=4095;
    struct Resource {
        enum State:int {Ready,Pending,Failed};
        std::string path;
        ma_uint64 frames=0;
        unsigned rate=0,channels=0;
        bool streamed=false;      // physical voices stream instead of sharing decoded PCM
        bool accounted=false;     // decoded PCM is pinned in the resource manager and counted in cachedBytes()
        uint64_t bytes=0;         // decoded size when not streamed
        uint64_t lastUse=0;
        std::atomic<int> state{Ready};
    };
    struct Voice {
        uint32_t id=0;
        uint64_t order=0, physicalAtFrame=0;
        std::shared_ptr<Resource> resource;
        std::unique_ptr<Sound> physical;
        Play parameters;
        double cursor=0;
        float envelope=1, fadeStart=1, fadeTarget=1;
        uint64_t fadeBegin=0, fadeFrames=0;
        bool stopped=false, stopAfterFade=false;
    };
    explicit Mixer(bool offline=false, unsigned budget=64, Limits limits={});
    ~Mixer();
    uint32_t play(const std::string&, const Play&);
    // A PCM input for decoded media (video soundtracks); started immediately.
    std::unique_ptr<PcmStream> stream(unsigned capacityFrames=48000);
    void stop(uint32_t, float fade=0);
    void resume(uint32_t);
    void volume(uint32_t,float);
    void categories(float,float,float);
    void pitch(float);
    void spatial(uint32_t,int,const void*);
    void listener(Vec3,Vec3,Vec3);
    // Foreground (fade in, device started) or background (fade out, device stopped). Returns
    // the device's error, if any, without throwing: a device that did not start keeps the
    // mixer suspended until the next active(true).
    ma_result active(bool,float);
    // Device start/stop used by active(); tests inject failures.
    void deviceControl(DeviceControl control) {backend.device=control;}
    void update();
    void read(float*,unsigned);
    bool playing(uint32_t) const;
    Voice* find(uint32_t);
    const Voice* find(uint32_t) const;
    unsigned physicalCount() const;
    unsigned logicalCount() const;
    std::vector<unsigned> takeEnded();
    float positionSeconds(uint32_t) const;
    // Master attenuation that is independent of fades and category gains
    // (Android transient audio-focus loss). 1 restores full volume.
    void duck(float gain);
    // Release unused decoded samples until at most `targetBytes` remain
    // (0: everything not referenced by a voice). Returns the bytes freed.
    uint64_t trim(uint64_t targetBytes);
    uint64_t cachedBytes() const {return cacheBytes;}
    unsigned cachedCount() const {return unsigned(cache.size());}
    unsigned pendingDecodes() const {return unsigned(decoding.size());}
    // Paths whose background decode failed since the last call; their voices have ended.
    std::vector<std::string> takeFailures();
private:
    Backend backend;
    ma_sound_group master{};
    std::array<std::unique_ptr<Voice>,capacity> voices;
    std::unordered_map<std::string,std::shared_ptr<Resource>> cache;
    std::unordered_map<std::string,double> musicPosition;
    std::vector<unsigned> ended;
    unsigned budget;
    uint16_t stamp=0;
    uint64_t sequence=0,lastFrame=0;
    unsigned nextMusic=0;
    bool offline=false,suspended=false;
    std::array<float,3> gains{{0.5f,0.5f,0.5f}};
    Vec3 listenerPosition;
    Limits limits;
    bool asyncDecode=false;
    uint64_t cacheBytes=0,useClock=0;
    std::vector<std::shared_ptr<Resource>> decoding; // queued or being decoded; game thread only
    std::vector<std::string> failures;
    std::thread decoder;
    std::mutex decodeMutex;
    std::condition_variable decodeWake;
    std::deque<std::shared_ptr<Resource>> decodeQueue; // guarded by decodeMutex
    bool decodeQuit=false;                             // guarded by decodeMutex
    std::shared_ptr<Resource> load(const std::string&,bool);
    void decodeLoop();
    void collectDecodes();
    void evict(uint64_t targetBytes);
    void release(Resource&);
    void finish(unsigned);
    void apply(Voice&);
    void rebalance();
    float gain(const Voice&) const;
    float audibility(const Voice&) const;
    void fade(Voice&,float,float,bool);
};
}
