// Real ReliefGenerator::run/queues/entry transitions. Only inference, file producers, and thermal sensors are fake.
// HasUpscaled is the production cache lookup; a cache hit is not a GPU upload and never claims pixels are ready.
#include "effects/relief_generator.hpp"
#include "effects/normal_maps.hpp"
#include "effects/upscale.hpp"
#include "effects/thermal_api.hpp"
#include "relief.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
using namespace storm;
using namespace std::chrono_literals;
namespace {
std::atomic<int> status{2};
std::atomic<float> forecast{0.95f};
std::atomic<bool> hot{true}, block_job{false}, cancelled{false}, slow_open{false};
std::atomic<int> opened{0}, started{0}, pause_after{0};
std::mutex events_mutex;
std::vector<std::string> events;
std::filesystem::path cache;
void expect(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
    std::printf("ok: %s\n",message); std::fflush(stdout);
}
template<class Predicate> void until(Predicate predicate, const char *message) {
    const auto end=std::chrono::steady_clock::now()+7s;
    while (!predicate() && std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(5ms);
    expect(predicate(),message);
}
void reset() {
    status=2; forecast=0.95f; hot=true; block_job=false; cancelled=false; slow_open=false; opened=0; started=0; pause_after=0;
    std::lock_guard lock(events_mutex); events.clear();
}
relief::Outcome infer(const std::filesystem::path &file, const char *lane) {
    ++started;
    { std::lock_guard lock(events_mutex); events.emplace_back(std::string(lane)+file.string()); }
    while (block_job && !cancelled) std::this_thread::sleep_for(1ms);
    if (cancelled || file.string().find("fail")!=std::string::npos) return relief::Outcome::Failed;
    std::this_thread::sleep_for(2ms);
    if (pause_after > 0 && started == pause_after) { status=2; forecast=0.95f; }
    return relief::Outcome::Prepared;
}
std::shared_ptr<effects::NormalMapEntry> normal(const char *name, uint64_t key, uint32_t drawn=10) {
    auto e=std::make_shared<effects::NormalMapEntry>();e->name=name;e->file=name;e->key=key;e->drawn=drawn;
    e->state=effects::NormalMapEntry::Waiting;return e;
}
std::shared_ptr<effects::UpscaleEntry> upscale(const char *name, uint64_t key) {
    auto e=std::make_shared<effects::UpscaleEntry>();e->name=name;e->file=name;e->key=key;e->width=e->height=1024;
    e->state=effects::UpscaleEntry::Waiting;return e;
}
void configure(effects::ReliefGenerator &g) {
    g.configure({cache,cache,[](const char*){return std::vector<uint8_t>{1};}});
    g.set_display(true,cache,false);
}
}
namespace storm::shim { void logf(const char*,...) {} }
namespace storm::effects {
NormalMapEntry::~NormalMapEntry()=default;
UpscaleEntry::~UpscaleEntry()=default;
const char *background_wait_reason() {
    const bool value=thermal_hot(hot.load(),status.load(),forecast.load());hot=value;
    return value?"synthetic thermal hold":nullptr;
}
}
namespace storm::fx {
bool HasReliefMap(const std::filesystem::path&,uint64_t) { return true; }
bool HasGlossMap(const std::filesystem::path&,uint64_t) { return true; }
}
namespace storm::relief {
struct Session::Impl { Stats stats; };
Session::Session(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
Session::~Session()=default;
std::unique_ptr<Session> Session::Open(const Models&,int,const std::filesystem::path&,std::string&) {
    ++opened; if (slow_open) std::this_thread::sleep_for(150ms);
    return std::unique_ptr<Session>(new Session(std::make_unique<Impl>()));
}
const Session::Stats &Session::stats() const { return impl_->stats; }
void Session::Cancel() { cancelled=true; }
Outcome Session::PrepareFile(const std::filesystem::path&,const std::filesystem::path &file,
    const std::filesystem::path&,int,std::string&,const UpscaleOutput*) { return infer(file,"N:"); }
Outcome Session::Upscale(const std::filesystem::path&,const std::filesystem::path &file,
    const UpscaleOutput&,int,std::string&) { return infer(file,"U:"); }
bool ReleaseGpu() { return false; }
std::vector<std::filesystem::path> List(const std::filesystem::path&) { return {}; }
int Prune(const std::filesystem::path&,const std::vector<std::filesystem::path>&,const std::filesystem::path&,int) { return 0; }
void MarkComplete(const std::filesystem::path&,int,size_t) {}
bool Complete(const std::filesystem::path&,int,size_t,int*) { return true; }
}
int main(int argc,char **argv) {
    expect(argc==2,"isolated cache supplied");cache=std::filesystem::path(argv[1])/"cache";
    std::filesystem::create_directories(cache);
    auto &g=effects::ReliefGenerator::instance();
    // The file is already prepared by a preceding normal job, but thermal protection now forbids inference.
    reset();configure(g);
    auto ready=upscale("ready",101), wrong_key=upscale("not-ready",102);
    const uint8_t payload[]={1,2,3};
    expect(fx::WriteUpscaled(cache,ready->key,payload),"production atomic cache writer succeeds");
    expect(g.request_upscale(ready) && g.request_upscale(wrong_key),"both cache requests queued");
    until([&]{return ready->state==effects::UpscaleEntry::Stored;},"completed exact-key upscale publishes while hot");
    expect(wrong_key->state==effects::UpscaleEntry::Waiting,"different key stays pending while hot");
    expect(opened==0 && started==0,"cache completion starts no model or inference under thermal hold");
    g.shutdown();expect(wrong_key->state==effects::UpscaleEntry::Missing,"shutdown releases queued request for later request");
    // Both live lanes remain populated. Expired entries and cached completions must not consume turns.
    reset();configure(g);
    std::vector<std::shared_ptr<effects::NormalMapEntry>> normals;
    std::vector<std::shared_ptr<effects::UpscaleEntry>> upscales;
    for (int i=0;i<8;++i) {
        const auto name=std::string("normal")+std::to_string(i);
        normals.push_back(normal(name.c_str(),200+i,10+i));g.request(normals.back(),name);
    }
    { auto dead=upscale("expired",300);expect(g.request_upscale(dead),"expired upscale queued"); }
    for (int i=0;i<8;++i) {
        const auto name=std::string("upscale")+std::to_string(i);
        upscales.push_back(upscale(name.c_str(),400+i));expect(g.request_upscale(upscales.back()),"live upscale queued");
    }
    until([&]{return !g.stats().paused.empty();},"thermal pause observed before work");
    status=1;forecast=0.8f;
    std::this_thread::sleep_for(5200ms);
    expect(started==0,"LIGHT with forecast above resume threshold remains paused");
    pause_after=2; forecast=0.6f;
    until([&]{return started==2;},"brief cool window admits only two jobs");
    until([&]{return g.stats().made+g.stats().upscaled==2;},"two admitted jobs finish before the thermal hold");
    expect(g.stats().made==1 && g.stats().upscaled==1,"both lanes make progress in the brief cool window");
    until([&]{return !g.stats().paused.empty();},"thermal rise pauses remaining jobs");
    const int held_count=started;
    std::this_thread::sleep_for(50ms);
    expect(started==held_count,"no inference starts while the thermal hold remains");
    status=1; forecast=0.6f;
    until([&]{return g.stats().made==8 && g.stats().upscaled==8;},"both queues complete after subsequent cooling");
    {
        std::lock_guard lock(events_mutex);
        expect(events.size()==16,"no expired request inferred");
        expect(events.front()=="N:normal7","most recently drawn normal runs first");
        for(size_t i=0;i<events.size();++i)expect(events[i][0]==(i%2?'U':'N'),"normal and upscale lanes alternate while both live");
    }
    g.shutdown();
    // A real inference failure is terminal for that entry and does not starve the other lane.
    reset();status=0;forecast=0.5f;hot=false;configure(g);
    auto failed=upscale("fail",500), good=upscale("good",501);
    expect(g.request_upscale(failed)&&g.request_upscale(good),"failure and following request queued");
    until([&]{return good->state==effects::UpscaleEntry::Stored;},"subsequent upscale succeeds after a failure");
    expect(failed->state==effects::UpscaleEntry::Failed,"real failure stays distinct from thermal waiting");
    g.shutdown();
    // Cancellation of an in-flight operation must not leave an entry permanently Waiting.
    for (bool is_upscale : {false,true}) {
        reset();status=0;forecast=0.5f;hot=false;block_job=true;configure(g);
        auto n=normal("cancel-normal",600);auto u=upscale("cancel-upscale",601);
        if(is_upscale)expect(g.request_upscale(u),"in-flight upscale queued");else g.request(n,n->file);
        until([&]{return started==1;},"in-flight operation started");
        g.shutdown();expect(cancelled,"shutdown cancels running inference");
        expect(is_upscale ? u->state==effects::UpscaleEntry::Missing : n->state==effects::NormalMapEntry::Missing,
               "cancelled in-flight entry leaves Waiting");
    }
    reset();configure(g);status=1;forecast=0.5f;slow_open=true;
    auto opening=upscale("opening",900);expect(g.request_upscale(opening),"model-loading request queued");
    until([&]{return opened==1;},"model loading started");g.shutdown();
    expect(opening->state==effects::UpscaleEntry::Missing,"shutdown during model loading releases request");
    expect(started==0,"shutdown during loading starts no inference");
    // A fresh session can retry that cancelled entry; shutdown never poisons the next model instance.
    reset();configure(g);status=1;forecast=0.5f;
    opening->state=effects::UpscaleEntry::Waiting; // the renderer sets Waiting before publishing the request
    expect(g.request_upscale(opening),"cancelled entry can be queued again");
    until([&]{return opening->state==effects::UpscaleEntry::Stored;},"retry completes in the new session");
    g.shutdown();
    std::puts("relief_scheduler: PASS");
}
