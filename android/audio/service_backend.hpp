#pragma once
#include <cstdint>
#include "backend.hpp"
#include <array>
#include <memory>
#include <string>
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
// Game-thread owned. The only other thread touching miniaudio nodes is its mixer.
// 0 means all; slots 1 and 2 retain the game's fixed music IDs.
class Mixer {
public:
    static constexpr unsigned capacity=4095;
    struct Resource {
        std::string path;
        ma_uint64 frames=0;
        unsigned rate=0;
        std::unique_ptr<Sound> cache;
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
    explicit Mixer(bool offline=false, unsigned budget=64);
    ~Mixer();
    uint32_t play(const std::string&, const Play&);
    void stop(uint32_t, float fade=0);
    void resume(uint32_t);
    void volume(uint32_t,float);
    void categories(float,float,float);
    void pitch(float);
    void spatial(uint32_t,int,const void*);
    void listener(Vec3,Vec3,Vec3);
    void active(bool,float);
    void update();
    void read(float*,unsigned);
    bool playing(uint32_t) const;
    Voice* find(uint32_t);
    const Voice* find(uint32_t) const;
    unsigned physicalCount() const;
    unsigned logicalCount() const;
    std::vector<unsigned> takeEnded();
    float positionSeconds(uint32_t) const;
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
    std::shared_ptr<Resource> load(const std::string&,bool);
    void finish(unsigned);
    void apply(Voice&);
    void rebalance();
    float gain(const Voice&) const;
    float audibility(const Voice&) const;
    void fade(Voice&,float,float,bool);
};
}
