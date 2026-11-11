// The game's portable UI host: one storm::ui::Host in the engine process, on the game
// thread, drawn by the bgfx backend in the frame owner's UI views (after the game picture,
// native resolution). See ../README.md.
//
// Engine code publishes states and interface models through platform/android_ui_sink.hpp;
// they reach the host directly. The Java activity keeps what is Android's: it routes each
// pointer at pointer-down (WantsTouch), sends the pointers the UI takes, its presentation
// state and the system insets through the EngineUi natives below (queued, applied at the
// top of the next engine frame), and receives what only Android can do (pause dialog,
// shortcuts mailbox, system editor, About).
#include "platform/android_ui_sink.hpp"

#include "storm_ui/asset_files.h"
#include "storm_ui/backend_bgfx.h"
#include "storm_ui/ui.h"

#include <storm/render/frame_owner.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <jni.h>
#include <sys/system_properties.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace storm::android::ui
{
namespace
{

constexpr const char *kTag = "StormUi";
// Interface screens close one menu a frame before they open the next: the interface is kept
// over a gap this short, so the game does not flash between them.
constexpr uint32_t kCloseGraceMs = 150;
// Frame owner client order: after the renderer's own clients.
constexpr int kClientOrder = 1000;

// adb shell setprop log.tag.<tag> DEBUG
bool Loggable(const char *tag)
{
    char value[PROP_VALUE_MAX] = {};
    const std::string name = std::string("log.tag.") + tag;
    __system_property_get(name.c_str(), value);
    return std::string_view(value) == "DEBUG" || std::string_view(value) == "VERBOSE";
}

// JNI call into the game activity (game thread). Missing methods are errors: the activity
// and this host ship together.
template <typename Call> void CallActivity(const char *name, const char *signature, Call call)
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        return;
    const jclass type = env->GetObjectClass(activity);
    const jmethodID method = type ? env->GetMethodID(type, name, signature) : nullptr;
    if (method)
        call(env, activity, method);
    else
        __android_log_print(ANDROID_LOG_ERROR, kTag, "GameActivity.%s%s is missing", name, signature);
    if (env->ExceptionCheck())
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (type)
        env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

jstring Utf(JNIEnv *env, const std::string &text)
{
    return env->NewStringUTF(text.c_str());
}

std::string Text(JNIEnv *env, jstring value)
{
    if (!value)
        return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    std::string text = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(value, chars);
    return text;
}

std::string Bytes(JNIEnv *env, jbyteArray value)
{
    const jsize size = env->GetArrayLength(value);
    std::string text(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte *>(text.data()));
    return text;
}

class EngineUi
{
  public:
    static EngineUi &Get()
    {
        static EngineUi instance;
        return instance;
    }

    // ---- game thread -------------------------------------------------------------

    void State(std::string_view channel, std::string json)
    {
        if (Loggable("StormUiState"))
            __android_log_print(ANDROID_LOG_DEBUG, "StormUiState", "%.*s %s", int(channel.size()), channel.data(), json.c_str());
        std::string name(channel);
        if (host_)
            host_->SetState(name, json);
        states_[name] = std::move(json); // replayed when a new host starts
    }

    void Screen(const std::string &screen, uint64_t revision)
    {
        __android_log_print(ANDROID_LOG_INFO, kTag, "interface %s %s (revision %llu)", screen.empty() ? screen_.c_str() : screen.c_str(),
                            screen.empty() ? "closed" : "opened", static_cast<unsigned long long>(revision));
        screen_ = screen;
        if (!screen.empty())
        {
            close_at_ = 0;
            return;
        }
        close_at_ = SDL_GetTicks() + kCloseGraceMs;
        if (close_at_ == 0)
            close_at_ = 1;
    }

    void Interface(std::string json)
    {
        if (screen_.empty())
            return; // a late snapshot of a screen that closed
        Record(json);
        if (host_)
            host_->SetInterface(json);
        interface_ = std::move(json);
        PublishInterfaceActive();
    }

    void Toast(std::string text)
    {
        if (host_)
            host_->Toast(text, "info");
    }

    void Tick()
    {
        ++loop_;
        if (!host_ && !failed_)
            Start();
        if (close_at_ && static_cast<int32_t>(SDL_GetTicks() - close_at_) >= 0)
        {
            close_at_ = 0;
            interface_.clear();
            if (host_)
                host_->CloseInterface();
            PublishInterfaceActive();
        }
        std::vector<Step> work;
        {
            const std::lock_guard lock(inbox_mutex_);
            work.swap(inbox_);
        }
        if (!host_)
            return; // nothing of it applies to a host that is not there (touches of no UI)
        for (Step &step : work)
        {
            if (step.call)
            {
                step.call(*host_);
                continue;
            }
            if (step.phase != kMove)
                TraceInput("touch", std::to_string(step.phase) + " " + std::to_string(int(step.x)) + "," + std::to_string(int(step.y)));
            host_->Touch(static_cast<storm::ui::TouchPhase>(step.phase), step.pointer, step.x, step.y);
        }
        PublishInterfaceActive();
    }

    // adb shell setprop log.tag.StormUiInput DEBUG (before the game starts): when touches and
    // the commands they produce reach the engine, in engine loop iterations (one per frame).
    void TraceInput(const char *what, const std::string &detail) const
    {
        if (trace_input_)
            __android_log_print(ANDROID_LOG_DEBUG, "StormUiInput", "loop %llu: %s %s", static_cast<unsigned long long>(loop_), what,
                                detail.c_str());
    }

    std::vector<std::string> TakeCommands()
    {
        std::vector<std::string> out;
        out.swap(commands_);
        slots_.clear();
        return out;
    }

    uint64_t Generation() const
    {
        return generation_;
    }

    void Shutdown()
    {
        if (auto *owner = storm::render::FrameOwner::Get(); owner && backend_)
        {
            owner->RemoveClient(backend_.get());
            if (trace_input_)
                owner->RemoveClient(&frame_trace_);
        }
        {
            const std::lock_guard lock(host_mutex_);
            host_.reset();
        }
        backend_.reset();
        files_.reset();
        if (auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv()); env && assets_)
            env->DeleteGlobalRef(assets_);
        assets_ = nullptr;
        failed_ = true; // no new host after this
    }

    // ---- platform thread (EngineUi natives) ---------------------------------------

    void Post(std::function<void(storm::ui::Host &)> call)
    {
        const std::lock_guard lock(inbox_mutex_);
        inbox_.push_back({std::move(call)});
    }

    // A pointer step. A move replaces the same pointer's move still waiting (only where the
    // finger is now matters): a fast finger costs one host call per pointer and frame.
    void Touch(int phase, int pointer, float x, float y)
    {
        const std::lock_guard lock(inbox_mutex_);
        if (phase == kMove)
            for (auto step = inbox_.rbegin(); step != inbox_.rend() && !step->call; ++step)
            {
                if (step->pointer != pointer)
                    continue;
                if (step->phase == kMove)
                {
                    step->x = x, step->y = y;
                    return;
                }
                break;
            }
        inbox_.push_back({{}, phase, pointer, x, y});
    }

    // Answered from the layout of the last frame (Host is thread-safe here).
    bool WantsTouch(float x, float y)
    {
        const std::lock_guard lock(host_mutex_);
        return host_ && host_->WantsTouch(x, y);
    }

    bool WantsBack()
    {
        const std::lock_guard lock(host_mutex_);
        return host_ && host_->WantsBack();
    }

    bool InterfaceActive() const
    {
        return interface_active_.load();
    }

    // Safe-area insets in pixels; the host takes dp.
    void SafeArea(float left, float top, float right, float bottom)
    {
        Post([left, top, right, bottom](storm::ui::Host &host) {
            const auto *owner = storm::render::FrameOwner::Get();
            const float density = owner && owner->Surface().density > 0.0f ? owner->Surface().density : 1.0f;
            host.SetSafeArea(left / density, top / density, right / density, bottom / density);
        });
        const std::lock_guard lock(inbox_mutex_);
        safe_area_ = {left, top, right, bottom};
        has_safe_area_ = true;
    }

  private:
    void Start()
    {
        auto *owner = storm::render::FrameOwner::Get();
        if (!owner || !owner->DeviceReady())
            return; // not yet: the renderer starts first
        auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!env || !activity)
            return;
        bool debuggable = false;
        {
            const jclass type = env->GetObjectClass(activity);
            const jmethodID get_assets = env->GetMethodID(type, "getAssets", "()Landroid/content/res/AssetManager;");
            const jobject assets = env->CallObjectMethod(activity, get_assets);
            assets_ = env->NewGlobalRef(assets);
            env->DeleteLocalRef(assets);
            const jmethodID get_info = env->GetMethodID(type, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
            const jobject info = env->CallObjectMethod(activity, get_info);
            const jclass info_type = env->GetObjectClass(info);
            constexpr jint kFlagDebuggable = 2; // ApplicationInfo.FLAG_DEBUGGABLE
            debuggable = (env->GetIntField(info, env->GetFieldID(info_type, "flags", "I")) & kFlagDebuggable) != 0;
            env->DeleteLocalRef(info_type);
            env->DeleteLocalRef(info);
            env->DeleteLocalRef(type);
            env->DeleteLocalRef(activity);
        }
        files_ = storm::ui::AssetFiles(AAssetManager_fromJava(env, assets_), "ui");
        std::string error;
        storm::ui::BgfxBackendOptions options;
        options.owner = owner;
        backend_ = storm::ui::BgfxBackend::Create(options, &error);
        const storm::render::SurfaceMetrics surface = owner->Surface();
        storm::ui::Config config;
        config.files = files_.get();
        config.backend = backend_.get();
        config.width = int(surface.width_px);
        config.height = int(surface.height_px);
        config.density = surface.density;
        config.game_root = std::filesystem::current_path().string(); // prepareGameDirectory
        config.show_errors = debuggable;
        config.on_action = [this](const storm::ui::Action &action) { OnAction(action); };
        config.on_text_request = [](const storm::ui::TextRequest &request) {
            CallActivity("onUiTextRequest", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZIIII)V",
                         [&](JNIEnv *env, jobject activity, jmethodID method) {
                             jstring token = Utf(env, request.token), text = Utf(env, request.text), title = Utf(env, request.title);
                             env->CallVoidMethod(activity, method, token, text, title, jboolean(request.numeric), jboolean(request.secret),
                                                 jint(request.x), jint(request.y), jint(request.width), jint(request.height));
                             env->DeleteLocalRef(token);
                             env->DeleteLocalRef(text);
                             env->DeleteLocalRef(title);
                         });
        };
        std::unique_ptr<storm::ui::Host> host = backend_ ? storm::ui::Host::Create(config, &error) : nullptr;
        if (!host)
        {
            // No other renderer exists for the game's screens: the player is told.
            failed_ = true;
            __android_log_print(ANDROID_LOG_ERROR, kTag, "UI host failed: %s", error.c_str());
            const std::string reason = "The UI runtime failed to start: " + error;
            CallActivity("uiFailed", "(Ljava/lang/String;)V", [&](JNIEnv *env, jobject activity, jmethodID method) {
                jstring text = Utf(env, reason);
                env->CallVoidMethod(activity, method, text);
                env->DeleteLocalRef(text);
            });
            backend_.reset();
            return;
        }
        {
            const std::lock_guard lock(host_mutex_);
            host_ = std::move(host);
        }
        ++generation_;
        trace_input_ = Loggable("StormUiInput");
        if (trace_input_)
            owner->AddClient(&frame_trace_, kClientOrder + 1);
        backend_->AttachHost(host_.get());
        owner->AddClient(backend_.get(), kClientOrder); // OnSurfaceChanged: the host lays out at the surface's size and density
        for (const auto &[channel, json] : states_)
            host_->SetState(channel, json);
        if (!interface_.empty())
            host_->SetInterface(interface_);
        {
            const std::lock_guard lock(inbox_mutex_);
            if (has_safe_area_)
            {
                const auto [left, top, right, bottom] = safe_area_;
                inbox_.insert(inbox_.begin(), Step{[left, top, right, bottom](storm::ui::Host &host) {
                    const auto *frame_owner = storm::render::FrameOwner::Get();
                    const float density = frame_owner ? frame_owner->Surface().density : 1.0f;
                    host.SetSafeArea(left / density, top / density, right / density, bottom / density);
                }});
            }
        }
        __android_log_print(ANDROID_LOG_INFO, kTag, "UI host started in the engine frame (%ux%u, density %.2f)", surface.width_px,
                            surface.height_px, surface.density);
    }

    // What the UI asks for: engine commands stay in this process (read at the top of the next
    // loop iteration, or this one for input applied at its top); what is Android's goes to
    // the activity.
    void OnAction(const storm::ui::Action &action)
    {
        if (action.screen == "hud" && (action.verb == "shortcut" || action.verb == "pause" || action.verb == "presence"))
        {
            CallActivity("onUiHudInput", "(Ljava/lang/String;Ljava/lang/String;)V", [&](JNIEnv *env, jobject activity, jmethodID method) {
                jstring verb = Utf(env, action.verb), value = Utf(env, action.value);
                env->CallVoidMethod(activity, method, verb, value);
                env->DeleteLocalRef(verb);
                env->DeleteLocalRef(value);
            });
            return;
        }
        const std::string command = storm::ui::EngineCommand(action);
        TraceInput("action", action.screen + " " + action.node + " " + action.verb + " -> " + command);
        if (!command.empty())
        {
            if (action.screen == "hud" && !action.node.empty())
            {
                // A latest-value slot (stick position, coverage acknowledgement): only its newest command counts.
                const auto found = slots_.find(action.node);
                if (found != slots_.end())
                {
                    commands_[found->second] = command;
                    return;
                }
                slots_[action.node] = commands_.size();
            }
            commands_.push_back(command);
            return;
        }
        CallActivity("onUiAction", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                     [&](JNIEnv *env, jobject activity, jmethodID method) {
                         jstring node = Utf(env, action.node), verb = Utf(env, action.verb), value = Utf(env, action.value);
                         env->CallVoidMethod(activity, method, node, verb, value);
                         env->DeleteLocalRef(node);
                         env->DeleteLocalRef(verb);
                         env->DeleteLocalRef(value);
                     });
    }

    void PublishInterfaceActive()
    {
        interface_active_ = host_ && host_->InterfaceActive();
    }

    // adb shell setprop log.tag.StormUiModels DEBUG: the newest model of each screen goes to
    // <external files>/ui-models/<screen>.json, to record real models for the screen tests.
    void Record(const std::string &json) const
    {
        if (screen_.empty() || !Loggable("StormUiModels"))
            return;
        const char *external = SDL_AndroidGetExternalStoragePath();
        if (!external)
            return;
        std::error_code error;
        const std::filesystem::path folder = std::filesystem::path(external) / "ui-models";
        std::filesystem::create_directories(folder, error);
        std::ofstream out(folder / (screen_ + ".json"), std::ios::binary);
        out.write(json.data(), std::streamsize(json.size()));
        if (!out)
            __android_log_print(ANDROID_LOG_ERROR, kTag, "Cannot record the interface %s in %s", screen_.c_str(), folder.c_str());
    }

    std::unique_ptr<storm::ui::FileSource> files_;
    jobject assets_ = nullptr; // keeps the Java AssetManager alive
    std::unique_ptr<storm::ui::BgfxBackend> backend_;
    std::unique_ptr<storm::ui::Host> host_;
    std::mutex host_mutex_; // the host's lifetime against the platform thread's queries
    bool failed_ = false;
    uint64_t generation_ = 0;
    uint64_t loop_ = 0; // engine loop iterations (Tick)
    bool trace_input_ = false;
    // With the input trace: which loop iteration each presented UI frame belongs to, and every
    // 5 s what the UI frames cost on the CPU (GPU work is the renderer's, in RenderMetrics).
    struct FrameTrace final : storm::render::FrameClient
    {
        void OnUiRender(const storm::render::FrameContext &frame) override
        {
            EngineUi &ui = EngineUi::Get();
            ui.TraceInput("frame", std::to_string(frame.frame_index));
            if (!ui.host_)
                return;
            const storm::ui::FrameStats stats = ui.host_->LastFrameStats();
            ++frames;
            update_ms += stats.update_ms;
            render_ms += stats.render_ms;
            draws += stats.draws;
            layers += stats.layers;
            const uint32_t now = SDL_GetTicks();
            if (!since)
                since = now;
            if (now - since < 5000)
                return;
            __android_log_print(ANDROID_LOG_DEBUG, "StormUiInput", "%u UI frames in %.1f s: CPU per frame update %.2f ms + render %.2f ms, %.0f draws, %.1f layers",
                                frames, (now - since) / 1000.0, update_ms / frames, render_ms / frames, double(draws) / frames, double(layers) / frames);
            frames = 0, update_ms = render_ms = 0.0, draws = layers = 0, since = now;
        }
        uint32_t since = 0, frames = 0;
        double update_ms = 0.0, render_ms = 0.0;
        long draws = 0, layers = 0;
    } frame_trace_;
    std::map<std::string, std::string> states_;
    std::string screen_;
    std::string interface_;
    uint32_t close_at_ = 0; // SDL ticks; 0 = no close pending
    std::atomic<bool> interface_active_{false};
    std::vector<std::string> commands_;
    std::map<std::string, size_t> slots_; // slot -> index in commands_
    static constexpr int kMove = 1; // storm::ui::TouchPhase::Move on the JNI surface
    // What the platform thread sent: a call, or (no call) a pointer step.
    struct Step
    {
        std::function<void(storm::ui::Host &)> call;
        int phase = 0, pointer = 0;
        float x = 0.0f, y = 0.0f;
    };
    std::mutex inbox_mutex_;
    std::vector<Step> inbox_;
    std::array<float, 4> safe_area_{};
    bool has_safe_area_ = false;
};

} // namespace

void state(std::string_view channel, std::string json)
{
    EngineUi::Get().State(channel, std::move(json));
}

void screen(const std::string &screen, uint64_t revision)
{
    EngineUi::Get().Screen(screen, revision);
}

void interfaceModel(std::string json)
{
    EngineUi::Get().Interface(std::move(json));
}

void toast(std::string text)
{
    EngineUi::Get().Toast(std::move(text));
}

void tick()
{
    EngineUi::Get().Tick();
}

std::vector<std::string> takeCommands()
{
    return EngineUi::Get().TakeCommands();
}

uint64_t generation()
{
    return EngineUi::Get().Generation();
}

void shutdown()
{
    EngineUi::Get().Shutdown();
}

} // namespace storm::android::ui

// ---- org.stormengine.port.EngineUi (platform threads) ----------------------------------
using storm::android::ui::EngineUi;

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeWantsTouch(JNIEnv *, jclass, jfloat x, jfloat y)
{
    return EngineUi::Get().WantsTouch(x, y) ? JNI_TRUE : JNI_FALSE;
}

// phase: 0 down, 1 move, 2 up, 3 cancel; physical pixels of the surface
JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeTouch(JNIEnv *, jclass, jint phase, jint pointer, jfloat x, jfloat y)
{
    if (phase < 0 || phase > 3)
        return;
    EngineUi::Get().Touch(phase, pointer, x, y);
}

JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeCancelInput(JNIEnv *, jclass)
{
    EngineUi::Get().Post([](storm::ui::Host &host) { host.CancelInput(); });
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeWantsBack(JNIEnv *, jclass)
{
    return EngineUi::Get().WantsBack() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeBack(JNIEnv *, jclass)
{
    EngineUi::Get().Post([](storm::ui::Host &host) { host.Back(); });
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeInterfaceActive(JNIEnv *, jclass)
{
    return EngineUi::Get().InterfaceActive() ? JNI_TRUE : JNI_FALSE;
}

// The channels the platform writes itself: "presentation", "appSettings".
JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSetState(JNIEnv *env, jclass, jstring channel, jbyteArray json)
{
    std::string name = storm::android::ui::Text(env, channel), text = storm::android::ui::Bytes(env, json);
    EngineUi::Get().Post([name = std::move(name), text = std::move(text)](storm::ui::Host &host) { host.SetState(name, text); });
}

JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSubmitText(JNIEnv *env, jclass, jstring token, jstring text, jboolean accepted)
{
    std::string id = storm::android::ui::Text(env, token), answer = storm::android::ui::Text(env, text);
    EngineUi::Get().Post([id = std::move(id), answer = std::move(answer), accepted](storm::ui::Host &host) {
        host.SubmitText(id, answer, accepted == JNI_TRUE);
    });
}

JNIEXPORT void JNICALL Java_org_stormengine_port_EngineUi_nativeSetSafeArea(JNIEnv *, jclass, jfloat left, jfloat top, jfloat right, jfloat bottom)
{
    EngineUi::Get().SafeArea(left, top, right, bottom);
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_EngineUi_nativeHostOwnsHud(JNIEnv *env, jclass, jstring mode)
{
    return storm::ui::HostOwnsHud(storm::android::ui::Text(env, mode)) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
