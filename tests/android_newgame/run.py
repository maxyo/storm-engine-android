#!/usr/bin/env python3
"""Exercise the production game-thread choice and one-shot delayed-event redirect."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[2]
source=(root/'src/libs/core/include/android_newgame.hpp').read_text()
source=source[source.index('namespace storm::android'):]
code=r'''
#include <cstdint>
#include <string>
#include <map>
#include <cassert>
struct ATTRIBUTES {
 std::string value; bool has=true; std::map<std::string,ATTRIBUTES*> children;
 ATTRIBUTES* FindAClass(ATTRIBUTES*,const char* key){auto i=children.find(key);return i==children.end()?nullptr:i->second;}
 bool HasValue(){return has;}const std::string& GetValue(){return value;}
 void SetAttribute(const char* key,const char* v){assert(std::string(key)=="function");value=v;}
};
struct VDATA {int32_t value=0;ATTRIBUTES* attr=nullptr;bool Get(int32_t& v){v=value;return true;}ATTRIBUTES* GetAClass(){return attr;}};
struct FakeCore {std::map<std::string,VDATA*> vars;void* GetScriptVariable(const char* key){auto i=vars.find(key);return i==vars.end()?nullptr:i->second;}} core;
''' + source + r'''
int main(){
 using namespace storm::android;
 tutorialSkip.reset(7);assert(!tutorialSkip.selected);
 assert(!tutorialSkip.apply(6,"1@7"));assert(!tutorialSkip.apply(7,"1@6"));assert(!tutorialSkip.apply(7,"1@7x"));
 assert(tutorialSkip.apply(7,"1@7"));ApplyNewGameChoiceAfterFrame();assert(!tutorialSkip.selected);
 VDATA hero{1};ATTRIBUTES player,pending,video,interface,supported;video.value="Sharlie_Start";VDATA pchar{0,&player},state{0,&interface};
 supported.value="1";interface.children["AndroidTutorialSkipSupported"]=&supported;
 core.vars={{"InterfaceStates",&state},{"startHeroType",&hero},{"pchar",&pchar}};
 tutorialSkip.apply(7,"1@7");ApplyNewGameChoiceAfterFrame();assert(tutorialSkip.selected); // init not finished
 player.children["PostEventQuest.questDelay.PostVideo_Start"]=&pending;
 player.children["GenQuest.VideoAfterQuest"]=&video;
 ApplyNewGameChoiceAfterFrame();assert(!tutorialSkip.selected);assert(pending.value=="AndroidStartWithoutTutorial");
 pending.value="";ApplyNewGameChoiceAfterFrame();assert(pending.value.empty());
 tutorialSkip.apply(7,"1@7");hero.value=2;ApplyNewGameChoiceAfterFrame();assert(!tutorialSkip.selected&&pending.value.empty());
 hero.value=1;video.value="OtherIntro";tutorialSkip.apply(7,"1@7");ApplyNewGameChoiceAfterFrame();assert(pending.value.empty());
 video.value="Sharlie_Start";video.has=false;tutorialSkip.apply(7,"1@7");ApplyNewGameChoiceAfterFrame();assert(pending.value.empty());
 video.has=true;tutorialSkip.apply(7,"1@7");supported.value="0";ApplyNewGameChoiceAfterFrame();assert(!tutorialSkip.selected&&pending.value.empty());
 interface.children.clear();tutorialSkip.apply(7,"1@7");ApplyNewGameChoiceAfterFrame();assert(!tutorialSkip.selected);
 tutorialSkip.apply(7,"1@7");tutorialSkip.reset(8);assert(!tutorialSkip.selected);assert(!tutorialSkip.apply(8,"1@7"));
}
'''
with tempfile.TemporaryDirectory(prefix='android-newgame-') as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS revision/default-off/cancel/unsupported/one-shot/post-init/wrong-hero/wrong-intro guards')
