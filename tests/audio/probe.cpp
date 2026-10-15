#include "backend.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sys/resource.h>
using namespace storm_audio;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::array<double,2> render(Backend& b, unsigned frames) {
    std::array<double,2> energy{};
    float pcm[512];
    for (unsigned done=0; done<frames;) {
        auto count = std::min(256u, frames-done);
        b.read(pcm,count);
        for(unsigned i=0;i<count*2;i++) {
            require(std::isfinite(pcm[i]), "non-finite PCM");
            energy[i%2] += pcm[i]*pcm[i];
        }
        done += count;
        // Let stream refill jobs progress while rendering faster than realtime.
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return energy;
}
int main(int argc, char** argv) try {
    require(argc==2 || (argc==3 && (std::string(argv[2])=="--device" || std::string(argv[2])=="--stream-memory")), "usage: audio-probe fixtures [--device|--stream-memory]");
    std::string dir=argv[1];
    if(argc==3 && std::string(argv[2])=="--stream-memory") {
        Backend b(true);
        Sound a(b,(dir+"/tone.mp3").c_str(),true,false);
        Sound c(b,(dir+"/tone.ogg").c_str(),true,false);
        a.play(); c.play(); render(b,48000);
        struct rusage usage{};
        require(getrusage(RUSAGE_SELF,&usage)==0,"getrusage");
        std::cout << "peak_rss_kib=" << usage.ru_maxrss << '\n';
        return 0;
    }
    if(argc==3) {
        Backend b(false);
        Sound a(b,(dir+"/tone.wav").c_str(),false,true);
        Sound m(b,(dir+"/tone.ogg").c_str(),true,false);
        a.play(); m.play();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        b.suspend(); std::this_thread::sleep_for(std::chrono::seconds(1)); b.resume();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "Device probe completed; audible/lifecycle verification is manual\n";
        return 0;
    }
    Backend b(true);
    bool rejected = false;
    try { Sound missing(b,(dir+"/missing.wav").c_str(),false,false); }
    catch(const std::runtime_error&) { rejected = true; }
    require(rejected,"missing file accepted");
    for(auto ext : {"wav","mp3","ogg"}) {
        Sound s(b,(dir+"/tone."+ext).c_str(),false,false);
        s.play(); auto e=render(b,4800);
        require(e[0]>1 && e[1]>1,"silent decode");
        require(std::abs(e[0]-e[1])<1e-6,"2D mono balance");
        require(s.cursor()>=4700 && s.cursor()<=4900,"decode cursor");
        s.pause(); auto before=s.cursor(); e=render(b,1024);
        require(s.cursor()==before && e[0]+e[1]<1e-8,"pause advanced or audible");
        s.seek(24000); s.play(); render(b,1024);
        require(s.cursor()>=25000 && s.cursor()<25200,"seek cursor");
        ma_sound_set_pitch(&s.sound,2); auto start=s.cursor(); render(b,4800);
        require(s.cursor()-start>9400 && s.cursor()-start<9800,"pitch cursor");
        ma_sound_set_pitch(&s.sound,1); ma_sound_set_looping(&s.sound,MA_TRUE);
        ma_uint64 length=0; check(ma_sound_get_length_in_pcm_frames(&s.sound,&length));
        s.seek(length-1024); e=render(b,4096);
        require(e[0]>1 && s.cursor()<5000,"loop wrap");
        ma_sound_set_fade_in_pcm_frames(&s.sound,1,0,1024); render(b,2048); e=render(b,1024);
        require(e[0]+e[1]<1e-8,"fade endpoint");
        std::cout << ext << ": decode/pause/seek/pitch/loop/fade PASS\n";
    }
    auto spatial=[&](float x,float z) {
        Sound s(b,(dir+"/tone.wav").c_str(),false,true);
        ma_sound_set_attenuation_model(&s.sound,ma_attenuation_model_linear);
        ma_sound_set_min_distance(&s.sound,1); ma_sound_set_max_distance(&s.sound,10);
        ma_sound_set_position(&s.sound,x,0,z); s.play(); return render(b,4800);
    };
    auto left=spatial(-2,0), right=spatial(2,0), near=spatial(0,-1), far=spatial(0,-8);
    require(left[0]>left[1]*2 && right[1]>right[0]*2,"spatial orientation");
    require(near[0]+near[1]>(far[0]+far[1])*4,"linear attenuation");
    std::cout << "3D orientation/attenuation PASS\n";
    {
        Sound music1(b,(dir+"/tone.mp3").c_str(),true,false);
        Sound music2(b,(dir+"/tone.ogg").c_str(),true,false);
        Sound effect(b,(dir+"/tone.wav").c_str(),false,true);
        for(auto s:{&music1,&music2,&effect}) { ma_sound_set_looping(&s->sound,MA_TRUE); s->play(); }
        auto e=render(b,48000*7);
        require(e[0]>100 && e[1]>100,"concurrent streams silent");
        for(auto s:{&music1,&music2,&effect}) s->pause();
        for(auto s:{&music1,&music2}) {
            s->play(); auto tail=render(b,4800);
            require(tail[0]>1 && tail[1]>1 && ma_sound_is_playing(&s->sound),"stream loop stopped/silent");
            s->pause(); auto paused=s->cursor(); render(b,1024);
            require(s->cursor()==paused,"stream pause cursor");
            s->seek(24000); s->play();
            // Mixer consumes the seek request; refill then completes on a worker.
            render(b,256);
            require(s->cursor()>=24000 && s->cursor()<=24256,"stream seek dispatched");
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (s->cursor()<25024 && std::chrono::steady_clock::now()<deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                render(b,256);
            }
            require(s->cursor()>=25024 && s->cursor()<=25280,"stream seek advancement");
            s->pause();
        }
        music1.play(); music2.play();
        ma_sound_set_fade_in_pcm_frames(&music1.sound,1,0,4800);
        ma_sound_set_fade_in_pcm_frames(&music2.sound,0,1,4800);
        render(b,9600);
        require(ma_sound_get_current_fade_volume(&music1.sound)==0 && ma_sound_get_current_fade_volume(&music2.sound)==1,"crossfade endpoint");
        std::cout << "two streaming music slots + 3D effect/seek/crossfade PASS\n";
    }
    for(int i=0;i<100;i++) {
        Backend cycle(true); Sound s(cycle,(dir+"/tone.wav").c_str(),false,false);
        s.play(); render(cycle,256); auto pos=s.cursor();
        cycle.suspend(); auto silence=render(cycle,256);
        require(s.cursor()==pos && silence[0]+silence[1]==0,"lifecycle cursor/silence");
        cycle.resume(); render(cycle,256); require(s.cursor()>pos,"lifecycle resume");
    }
    std::cout << "100 offline lifecycle/cleanup cycles PASS (device unverified)\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
