#include "service_backend.hpp"
#include <iostream>
#include <cmath>
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
    std::cout<<"service: priority/virtualization/stale IDs/categories/lifecycle/cache/two streams/overflow PASS\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
