#include "service_backend.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>
using namespace storm_audio;
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
double render(Mixer& m,unsigned frames){float pcm[512];double e=0;while(frames){unsigned n=std::min(256u,frames);m.read(pcm,n);for(unsigned i=0;i<n*2;++i){require(std::isfinite(pcm[i]),"finite PCM");e+=pcm[i]*pcm[i];}frames-=n;}return e;}
int main(int argc,char**argv) try {
    require(argc==2,"fixture directory required");std::string dir=argv[1];Mixer m(true,2);Play p;p.loop=true;
    auto a=m.play(dir+"/tone.wav",p);p.priority=200;auto b=m.play(dir+"/tone.wav",p);p.priority=0;auto c=m.play(dir+"/tone.wav",p);
    require(m.physicalCount()==2 && m.logicalCount()==3,"voice budget");
    require(m.find(a)->physical && !m.find(b)->physical && m.find(c)->physical,"priority order");
    require(render(m,4800)>1,"real PCM");require(m.find(b)->cursor>=4790,"virtual cursor advances");
    m.stop(a);require(m.find(b)->physical!=nullptr,"virtual promotion");m.update();require(m.find(b)->cursor>=4790,"control update preserves pending seek");render(m,512);require(m.find(b)->cursor>4800,"promotion preserves cursor");
    m.stop(b);auto replacement=m.play(dir+"/tone.wav",p);require(replacement!=b,"new stamp");m.stop(b);require(m.playing(replacement),"stale stop ignored");
    m.volume(0,.5f);m.categories(.2f,.3f,.4f);require(m.find(c)->parameters.volume==.5f && m.find(replacement)->parameters.volume==.5f,"master volume does not compound");
    m.active(false,.01f);auto cursor=m.find(c)->cursor;require(render(m,1024)==0 && m.find(c)->cursor==cursor,"background freezes cursor");m.active(true,.01f);require(render(m,2048)>0,"foreground resumes");
    m.stop(0);require(m.logicalCount()==0,"stop all");
    p={};p.paused=true;auto paused=m.play(dir+"/tone.wav",p);render(m,1024);require(m.find(paused)->cursor==0,"simple cache paused");m.resume(paused);require(render(m,1024)>.01,"cached resume");m.stop(0);
    p={};p.stream=true;p.loop=true;auto music1=m.play(dir+"/tone.mp3",p);require(music1==1,"first music ID");p.fade=.1f;auto music2=m.play(dir+"/tone.ogg",p);require(music2==2 && m.logicalCount()==2,"two music slots");render(m,9600);require(!m.playing(music1) && m.playing(music2),"music crossfade finishes");
    auto music3=m.play(dir+"/tone.mp3",p);require(music3==1,"music slot reuse");render(m,9600);require(m.logicalCount()==1,"rapid switch cleanup");m.stop(0);
    p={};p.loop=true;p.paused=true;for(unsigned i=2;i<Mixer::capacity;i++)m.play(dir+"/tone.wav",p);
    bool rejected=false;try{m.play(dir+"/tone.wav",p);}catch(const std::runtime_error&){rejected=true;}require(rejected,"logical overflow rejected");m.stop(0);
    for(int i=0;i<100;++i) {
        Mixer cycle(true,2);Play effect;auto id=cycle.play(dir+"/tone.wav",effect);
        render(cycle,256);cycle.active(false,0);auto saved=cycle.find(id)->cursor;
        require(render(cycle,256)==0 && cycle.find(id)->cursor==saved,"cycle suspended");
        cycle.active(true,0);require(render(cycle,512)>0,"cycle resumed");
    }
    {   // A device that refuses to start or stop on a focus change (an AAudio error, the phone crash
        // of 2026-11-12) is reported, never thrown: the mixer stays suspended and silent, and the
        // next active(true) starts the device again.
        static int starts=0;
        Mixer device(true,2);Play effect;effect.loop=true;auto id=device.play(dir+"/tone.wav",effect);
        render(device,256);require(device.active(false,0)==MA_SUCCESS,"device stop");
        device.deviceControl([](ma_engine*,bool start){if(start){++starts;return MA_ERROR;}return MA_SUCCESS;});
        auto saved=device.find(id)->cursor;
        require(device.active(true,0)==MA_ERROR,"start failure reported");
        require(render(device,512)==0 && device.find(id)->cursor==saved,"silent and frozen after a failed start");
        require(device.active(true,0)==MA_ERROR && starts==2,"start retried on the next foreground");
        device.deviceControl(noDevice);
        require(device.active(true,0)==MA_SUCCESS && render(device,512)>0,"sound after the device starts");
        device.deviceControl([](ma_engine*,bool start){return start ? MA_SUCCESS:MA_ERROR;});
        require(device.active(false,0)==MA_ERROR,"stop failure reported");
        require(render(device,512)==0,"silent after a failed stop: the master has faded out");
        require(device.active(true,0)==MA_SUCCESS && render(device,512)>0,"resumes after a failed stop");
    }
    Mixer levels(true,2);p={};p.loop=true;auto level=levels.play(dir+"/tone.wav",p);
    levels.categories(0,1,1);require(render(levels,1024)==0,"FX category mute");
    levels.categories(1,1,1);require(render(levels,1024)>1,"FX category unmute");
    levels.pitch(0);auto frozen=levels.find(level)->cursor;render(levels,1024);
    require(levels.find(level)->cursor==frozen,"zero pitch freezes");
    levels.pitch(1);require(render(levels,1024)>1,"pitch resumes");
    for (std::string ext:{"wav","mp3","ogg"}) {
        Mixer rates(true,2);p={};p.stream=ext!="wav";p.loop=true;
        auto id=rates.play(dir+"/tone441."+ext,p);render(rates,4800);
        require(rates.find(id)->resource->rate==48000,"resource cursor sample rate");
        require(std::abs(rates.positionSeconds(id)-.1f)<.01f,"44100 Hz time conversion");
    }
    {   // Size-capped LRU: six distinct ~0.5 MiB samples, room for two.
        const char* files[]={"tone.wav","tone.mp3","tone.ogg","tone441.wav","tone441.mp3","tone441.ogg"};
        Mixer probe(true,8);Play one;auto first=probe.play(dir+"/tone.wav",one);
        const uint64_t each=probe.find(first)->resource->bytes;
        require(each>400000 && probe.cachedBytes()==each,"decoded size accounting");
        Limits small;small.cacheBytes=each*2+each/2;
        Mixer lru(true,8,small);std::vector<uint32_t> ids;
        for(auto* f:files) {
            auto id=lru.play(dir+"/"+f,one);render(lru,256);lru.stop(id);
            require(lru.cachedBytes()<=small.cacheBytes,"cache stays within budget");
        }
        require(lru.cachedCount()==2 && lru.cachedBytes()==each*2,"only the two most recent samples remain");
        // Most recently used survive: replaying them neither decodes nor evicts.
        auto a1=lru.play(dir+"/tone441.mp3",one);auto a2=lru.play(dir+"/tone441.ogg",one);
        require(lru.cachedCount()==2 && lru.cachedBytes()==each*2,"recent entries were cache hits");
        // Samples in use are never evicted, even over budget...
        auto a3=lru.play(dir+"/tone.wav",one);auto a4=lru.play(dir+"/tone.mp3",one);
        require(lru.cachedBytes()==each*4 && lru.physicalCount()==4,"in-use samples are pinned");
        require(render(lru,2048)>1,"pinned samples keep playing");
        // ...and the overshoot is released as soon as their voices end.
        lru.stop(a1);lru.stop(a2);
        require(lru.cachedBytes()==each*2 && lru.playing(a3) && lru.playing(a4),"overshoot released, LRU order");
        require(render(lru,1024)>1,"survivors unaffected by eviction");
        lru.stop(0);
        require(lru.trim(0)==each*2 && lru.cachedBytes()==0 && lru.cachedCount()==0,"trim releases every unused sample");
        auto again=lru.play(dir+"/tone.wav",one);require(render(lru,1024)>1 && lru.playing(again),"evicted sample reloads");
    }
    {   // Long samples are streamed instead of decoded into memory.
        Limits tiny;tiny.streamBytes=100000;
        Mixer longs(true,4,tiny);Play fx;fx.loop=true;
        auto id=longs.play(dir+"/tone.wav",fx);
        require(longs.find(id)->resource->streamed && longs.cachedBytes()==0,"oversized sample is streamed");
        require(id>2,"streamed effect keeps an effect ID, not a music slot");
        double energy=0;for(int i=0;i<40 && energy<1;++i) {energy+=render(longs,1024);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        require(energy>1 && longs.find(id)->cursor>0,"streamed effect renders PCM");
        longs.stop(0);require(longs.logicalCount()==0,"streamed effect stops");
    }
    {   // Background decoding: play() returns before the sample is decoded.
        Limits async;async.asyncDecode=1;
        Mixer bg(true,8,async);Play fx;
        auto id=bg.play(dir+"/tone441.ogg",fx);
        require(bg.playing(id),"voice exists while decoding");
        unsigned waited=0;
        while(bg.pendingDecodes() && waited++<5000) {require(bg.find(id)->cursor==0,"cursor held while decoding");std::this_thread::sleep_for(std::chrono::milliseconds(1));bg.update();}
        require(bg.pendingDecodes()==0 && bg.cachedBytes()>0,"decode completes");
        require(render(bg,4800)>1,"decoded sample plays from the start");
        require(std::abs(bg.find(id)->cursor-4800)<600,"no audio skipped during decode");
        // Stop before the decode finishes: the voice ends, the sample is still cached afterwards.
        auto early=bg.play(dir+"/tone441.mp3",fx);bg.stop(early);bg.update();
        require(!bg.playing(early),"stop while decoding");
        waited=0;while(bg.pendingDecodes() && waited++<5000) {std::this_thread::sleep_for(std::chrono::milliseconds(1));bg.update();}
        require(bg.pendingDecodes()==0 && bg.takeFailures().empty(),"abandoned decode is collected");
        bool missing=false;try{bg.play(dir+"/missing.wav",fx);}catch(const std::runtime_error&){missing=true;}
        require(missing,"missing file still fails synchronously");
        bg.duck(.5f);double ducked=render(bg,2400);bg.duck(1);double full=render(bg,2400);
        require(ducked>0 && ducked<full*.5,"ducking attenuates the master group");
    }
    for(int i=0;i<50;++i) { // Destroy with decodes queued or running.
        Limits async;async.asyncDecode=1;Mixer racing(true,4,async);Play fx;
        for(const char* f:{"tone.wav","tone.mp3","tone.ogg","tone441.wav"}) racing.play(dir+"/"+f,fx);
        if(i%2) render(racing,256);
    }
    {   // PCM stream (video soundtracks): played-frame clock, underrun silence, pause, stop(0) immunity.
        Mixer mixer(true,2);auto stream=mixer.stream(4800);
        std::vector<float> tone(2400*2);
        for(size_t i=0;i<tone.size();i+=2)tone[i]=tone[i+1]=0.5f*std::sin(i*0.02f);
        require(stream->write(tone.data(),2400)==2400 && stream->write(tone.data(),2400)==2400,"stream accepts its capacity");
        require(stream->write(tone.data(),2400)==0,"full stream refuses more");
        require(render(mixer,2400)>1 && stream->played()==2400,"stream plays and counts frames");
        mixer.stop(0);require(render(mixer,2400)>1 && stream->played()==4800,"stop(0) leaves the stream alone");
        // The engine has already pulled the next block from the stream: only that short tail sounds.
        require(render(mixer,1024)<2 && stream->played()==4800,"underrun is silent and not counted");
        stream->write(tone.data(),2400);stream->pause(true);
        require(render(mixer,1024)==0 && stream->played()==4800,"paused stream holds its clock");
        stream->pause(false);require(render(mixer,1024)>0 && stream->played()==5824,"resumed stream continues");
        mixer.active(false,0);require(render(mixer,512)==0 && stream->played()==5824,"background suspends the stream");
        mixer.active(true,0);stream->volume(0);render(mixer,512);require(render(mixer,512)==0,"stream volume");
    }
    std::cout<<"service: PCM stream clock/underrun/pause/background PASS\n";
    std::cout<<"service: LRU cache/streamed long samples/background decode/ducking PASS\n";
    std::cout<<"service: priority/virtualization/stale IDs/categories/lifecycle/cache/two streams/overflow PASS\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
