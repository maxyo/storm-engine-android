#pragma once
#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <jni.h>
#include <spdlog/spdlog.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <string>
#include <vector>
#include "android_ship_telemetry.hpp"
#include "android_spyglass.hpp"
#include "android_sea_navigator.hpp"
#include "android_sea_orders.hpp"
#include "android_sea_quick_action.hpp"
#include "android_land_commands.hpp"
#include "android_worldmap.hpp"
#include "android_interface_presentation.hpp"
#include "android_ui_sink.hpp"

namespace storm::android
{
struct UiTouch { float x, y; int phase; };
// All native state is accessed only by the game thread. Java marshals its UI queue.
inline std::string mobileScreen;
// The engine's plain text as a JSON string (what the text channels carry), control characters escaped.
inline std::string uiJsonText(std::string_view text)
{
    std::string out = "\"";
    for (const unsigned char c : text)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { char code[8]; std::snprintf(code, sizeof code, "\\u%04x", c); out += code; }
        else out += char(c);
    }
    return out + '"';
}
inline uint64_t mobileScreenRevision = 1;
inline InterfacePresentation interfacePresentation;
inline std::deque<std::string> dialogueActions;
inline std::deque<UiTouch> uiTouches;
struct UiWidgetAction { std::string screen, node, value; int index; };
inline std::deque<UiWidgetAction> uiWidgetActions;
inline std::string lastWidgetSnapshot;
inline std::string contextAction, requestedContextAction;
// Opt-in, bounded diagnostics for the Android interaction bridge. Game thread only.
inline void traceContextAction(const char *stage, const std::string &action, const std::string &detail)
{
    static unsigned budget = 192;
    if (!budget) return;
    char setting[PROP_VALUE_MAX] = {};
    __system_property_get("log.tag.StormInput", setting);
    if (std::string_view(setting) != "DEBUG") return;
    --budget;
    __android_log_print(ANDROID_LOG_DEBUG, "StormInput", "context %s action=%s %s",
                        stage, action.c_str(), detail.c_str());
}
// JNI admission and native Present run on different threads; keep this bounded
// diagnostic independent of the game-thread-only input trace above.
inline void traceWorldMapModal(const char *event,const std::string &detail)
{
    static std::atomic<unsigned> budget{128};
    unsigned remaining=budget.load(std::memory_order_relaxed);
    while(remaining&&!budget.compare_exchange_weak(remaining,remaining-1,std::memory_order_relaxed)){}
    if(remaining)__android_log_print(ANDROID_LOG_INFO,"StormMapModalNative","%s tick=%u %s",event,SDL_GetTicks(),detail.c_str());
}
inline int cinematicState = -1;
inline void showSaveError() {
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if(!env||!activity)return;
    auto type=env->GetObjectClass(activity);
    auto method=type?env->GetMethodID(type,"showSaveError","()V"):nullptr;
    if(method)env->CallVoidMethod(activity,method);
    if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();}
    if(type)env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
// Gameplay state: channel "gameplay" of the UI host, and the activity's copy for its own controls.
inline std::string lastGameplayState;
inline void publishGameplayState(const std::string &json)
{
    if (lastGameplayState == json) return;
    if (json.size() > 8192) { spdlog::error("gameplay state of {} bytes is over the 8192-byte bound: not published", json.size()); return; }
    lastGameplayState = json;
    ui::state("gameplay", json);
    // The activity keeps the gameplay mode for its own controls (look surface, virtual keys).
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type = env->GetObjectClass(activity);
    auto method = type ? env->GetMethodID(type, "showGameplayState", "([B)V") : nullptr;
    auto bytes = env->NewByteArray(static_cast<jsize>(json.size()));
    if (bytes) env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
    if (method && bytes) env->CallVoidMethod(activity, method, bytes);
    else spdlog::error("gameplay state: GameActivity.showGameplayState is missing");
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (bytes) env->DeleteLocalRef(bytes);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
// Live ship telemetry (channel "ship"). Independent from action revisions; rendering
// acknowledgements are never inherited by a new UI host.
inline uint64_t shipTelemetryHost = 0;
inline void publishShipTelemetry()
{
    if (shipTelemetryHost != ui::generation())
    {
        shipTelemetryHost = ui::generation();
        shipTelemetry.resetActivity(); // a new UI host: nothing drawn or answered by the old one counts
    }
    const auto tick = SDL_GetTicks();
    if (const auto json = shipTelemetry.publication(tick))
    {
        ui::state("ship", *json);
        shipTelemetry.published(*json, tick);
    }
}
inline uint64_t worldMapHost = 0;
inline uint32_t worldMapPublishedAt = 0;
inline uint64_t worldMapPublishedRevision = UINT64_MAX;
inline uint64_t worldMapPublishedReady=0,worldMapPublishedActive=0,worldMapPublishedFailure=0;
inline void publishWorldMapHud(bool force=false)
{
    if(worldMapHost!=ui::generation()){worldMapHost=ui::generation();worldMapHud.resetActivity();worldMapPublishedAt=0;}
    const auto tick=SDL_GetTicks();
    const auto modal=worldMapHud.modal.receipt(tick);
    if(force || tick-worldMapPublishedAt>=200 || worldMapPublishedRevision!=worldMapHud.revision() ||
        modal.token!=worldMapPublishedReady || modal.active!=worldMapPublishedActive || modal.failed!=worldMapPublishedFailure)
    {
        ui::state("worldMap",worldMapHud.json());
        worldMapPublishedAt=tick;worldMapPublishedRevision=worldMapHud.revision();
        worldMapPublishedReady=modal.token;worldMapPublishedActive=modal.active;worldMapPublishedFailure=modal.failed;
    }
}
inline uint64_t seaOrdersHost = 0;
inline std::string lastSeaOrders;
inline void publishSeaOrders()
{
    if (seaOrdersHost != ui::generation()) { seaOrdersHost = ui::generation(); lastSeaOrders.clear(); seaOrders.resetActivity(); }
    const auto json = seaOrders.json();
    if (json == lastSeaOrders) return;
    lastSeaOrders = json;
    ui::state("seaOrders", json);
}
// The sea quick action (U1). A new UI host gets a new activity generation: nothing drawn or
// tapped by the old one counts.
inline uint64_t seaQuickActionHost = 0;
inline std::string lastSeaQuickAction;
inline void publishSeaQuickAction()
{
    if (seaQuickActionHost != ui::generation()) { seaQuickActionHost = ui::generation(); lastSeaQuickAction.clear(); seaQuickAction.resetActivity(); }
    const auto json = seaQuickAction.json();
    if (json == lastSeaQuickAction) return;
    lastSeaQuickAction = json;
    ui::state("seaQuickAction", json);
}
inline uint64_t landCommandsHost = 0;
inline std::string lastLandCommands;
inline void publishLandCommands()
{
    if (landCommandsHost != ui::generation()) { landCommandsHost = ui::generation(); lastLandCommands.clear(); landCommands.resetActivity(); }
    const auto json = landCommands.json();
    if (json == lastLandCommands) return;
    lastLandCommands = json;
    ui::state("landCommands", json);
}
inline std::string lastVitals;
inline uint32_t lastVitalsTick=0;
inline void publishVitals(const std::string &json) {
    const auto now=SDL_GetTicks();
    if(lastVitals==json || (!json.empty() && now-lastVitalsTick<100))return;
    lastVitals=json;lastVitalsTick=now;
    // Nothing at all when the engine draws no land hero: JSON null for the UI.
    ui::state("vitals",json.empty()?std::string("null"):json);
}
inline std::string lastWorldInfo;
inline void publishWorldInfo(const std::string &text) {
    if (lastWorldInfo == text) return;
    lastWorldInfo=text;
    ui::state("worldInfo",uiJsonText(text)); // the engine's text as the JSON string the channel carries
}
// A passing line of the game's event log (Log_Info and alike): the portable UI shows it as
// a toast (the HUD's top-centre log over a free game). The same text again within the
// engine log's own line life is one line, as the engine's list keeps it.
inline std::string lastGameMessage;
inline uint32_t lastGameMessageAt = 0;
inline void publishGameMessage(const std::string &text) {
    const uint32_t now=SDL_GetTicks();
    if (text.empty() || (text == lastGameMessage && now - lastGameMessageAt < 5000)) return;
    lastGameMessage=text; lastGameMessageAt=now;
    ui::toast(text);
}
// Set from the game thread's command queue ("graphics:reload", sent by the platform host after
// the settings screen stored new renderer preferences); the renderer re-applies the graphics
// effects from the preferences at its next frame (s_device.cpp).
inline std::atomic<bool> graphicsSettingsChanged{false};
inline int graphicsSetting(const char *key, int fallback) {
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return fallback;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"getGraphicsSetting","(Ljava/lang/String;I)I") : nullptr;
    auto name=env->NewStringUTF(key);
    int value=fallback;
    if (method && name) value=env->CallIntMethod(activity,method,name,static_cast<jint>(fallback));
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); value=fallback; }
    if (name) env->DeleteLocalRef(name);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return value;
}
inline void publishContextAction(const std::string &name) {
    if (contextAction == name) return;
    traceContextAction("publish", name, "previous=" + contextAction);
    contextAction = name; requestedContextAction.clear();
    ui::state("contextAction",uiJsonText(name));
}
inline int loadingState = -1;
inline void publishLoading(bool active) {
    if (loadingState == static_cast<int>(active)) return;
    loadingState=active;
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"showLoading","(Z)V") : nullptr;
    if (method) env->CallVoidMethod(activity,method,static_cast<jboolean>(active));
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
// A state channel of the portable UI (JSON as is, "null" = nothing), for channels without a
// publisher of their own here.
inline void publishUiState(const char *channel, const std::string &json) {
    ui::state(channel,json);
}
// Channel "spyglass" (android_spyglass.hpp): sent through the generic state channel when it changes.
inline std::string lastSpyglass;
inline void publishSpyglass() {
    const std::string json=spyglass.json();
    if(lastSpyglass==json)return;
    lastSpyglass=json;
    publishUiState("spyglass",json);
}
// Channel "video": {"fullscreen": true} while a game video plays over the whole screen (the
// scripts' StartVideo player), null when it ends. A player embedded in an interface node
// (xi_video_rect, SetShowVideo(false)) never sets it: its screen stays. Back is then the
// game's Cancel to the video (main.cpp).
inline const void *fullscreenVideoOwner = nullptr;
inline void publishFullscreenVideo(const void *owner, bool active) {
    if (active) {
        if (fullscreenVideoOwner == owner) return;
        fullscreenVideoOwner = owner;
        publishUiState("video", "{\"fullscreen\":true}");
    } else if (fullscreenVideoOwner == owner) {
        fullscreenVideoOwner = nullptr;
        publishUiState("video", "null");
    }
}
inline bool fullscreenVideo() { return fullscreenVideoOwner != nullptr; }
inline void publishCinematic(bool active) {
    if (cinematicState == static_cast<int>(active)) return;
    cinematicState=active;
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"showCinematic","(Z)V") : nullptr;
    if (method) env->CallVoidMethod(activity,method,static_cast<jboolean>(active));
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}


inline void publishMobileWidgets(const std::string &json)
{
    if (lastWidgetSnapshot == json) return;
    lastWidgetSnapshot = json;
    ui::interfaceModel(json);
}

// Optional v2 image bridge. All access and local JNI refs stay on the game thread.
struct MobileImageRequest
{
    int64_t requestId, screenRevision, handle, imageRevision, maxEdge;
    uint64_t activityRevision;
};
inline jweak imageActivity = nullptr;
inline uint64_t imageActivityRevision = 0;
inline jmethodID imagePoll = nullptr, imageReply = nullptr;
inline bool imageMethodsKnown = false;
inline void prepareImageActivity(JNIEnv *env, jobject activity)
{
    if (imageActivity && env->IsSameObject(imageActivity, activity))
        return;
    if (imageActivity)
        env->DeleteWeakGlobalRef(imageActivity);
    imageActivity = env->NewWeakGlobalRef(activity);
    ++imageActivityRevision;
    imageMethodsKnown = false;
    imagePoll = nullptr;
    imageReply = nullptr;
    lastWidgetSnapshot.clear();
}
inline bool imageMethods(JNIEnv *env, jobject activity)
{
    prepareImageActivity(env, activity);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    if (!imageMethodsKnown)
    {
        imageMethodsKnown = true;
        auto type = env->GetObjectClass(activity);
        if (type)
        {
            imagePoll = env->GetMethodID(type, "pollMobileImageRequest", "()[J");
            if (env->ExceptionCheck())
                env->ExceptionClear(); // Old apps intentionally omit these methods.
            imageReply = env->GetMethodID(type, "onMobileImage", "(JJJJII[BLjava/lang/String;)V");
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(type);
        }
    }
    return imagePoll && imageReply;
}
inline std::vector<MobileImageRequest> pollMobileImageRequests(size_t limit)
{
    std::vector<MobileImageRequest> out;
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        return out;
    if (imageMethods(env, activity))
        for (size_t i = 0; i < std::min(size_t(4), limit); ++i)
        {
            auto value = static_cast<jlongArray>(env->CallObjectMethod(activity, imagePoll));
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                if (value)
                    env->DeleteLocalRef(value);
                break;
            }
            if (!value)
                break;
            if (env->GetArrayLength(value) == 5)
            {
                std::array<jlong, 5> fields{};
                env->GetLongArrayRegion(value, 0, 5, fields.data());
                if (!env->ExceptionCheck())
                    out.push_back({fields[0], fields[1], fields[2], fields[3], fields[4], imageActivityRevision});
            }
            env->DeleteLocalRef(value);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                break;
            }
        }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(activity);
    return out;
}
inline void publishMobileImage(const MobileImageRequest &request, int width, int height,
                               const std::vector<uint8_t> &rgba, const std::string &error)
{
    if (request.screenRevision != int64_t(mobileScreenRevision) || rgba.size() > 512 * 512 * 4)
        return;
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        return;
    if (imageMethods(env, activity) && request.activityRevision == imageActivityRevision)
    {
        auto bytes = env->NewByteArray(static_cast<jsize>(rgba.size()));
        if (!bytes || env->ExceptionCheck())
        {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            if (bytes)
                env->DeleteLocalRef(bytes);
            env->DeleteLocalRef(activity);
            return;
        }
        auto message = env->NewStringUTF(error.c_str());
        if (bytes && !env->ExceptionCheck() && !rgba.empty())
            env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(rgba.size()),
                                    reinterpret_cast<const jbyte *>(rgba.data()));
        if (bytes && message && !env->ExceptionCheck())
            env->CallVoidMethod(activity, imageReply, jlong(request.requestId), jlong(request.screenRevision),
                                jlong(request.handle), jlong(request.imageRevision), jint(width), jint(height), bytes,
                                message);
        if (bytes)
            env->DeleteLocalRef(bytes);
        if (message)
            env->DeleteLocalRef(message);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(activity);
}

inline void showMobileScreen(const std::string &name)
{
    auto screen = name;
    std::replace(screen.begin(), screen.end(), '\\', '/');
    screen = screen.substr(screen.find_last_of('/') + 1);
    const auto suffix = screen.find_last_of('.');
    if (suffix != std::string::npos) screen.resize(suffix);
    for (auto &c : screen) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    mobileScreen = screen;
    ++mobileScreenRevision;
    interfacePresentation.begin(mobileScreenRevision, !screen.empty(), interfacePresentation.negotiated());
    uiTouches.clear();
    uiWidgetActions.clear();
    lastWidgetSnapshot.clear();
    ui::screen(screen, mobileScreenRevision);
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    const auto type = env->GetObjectClass(activity);
    const auto versionedMethod = type ? env->GetMethodID(type, "showMobileScreen", "(Ljava/lang/String;J)V") : nullptr;
    // Old applications intentionally omit the overload and retain native UI.
    if (env->ExceptionCheck()) env->ExceptionClear();
    const auto method = !versionedMethod && type ? env->GetMethodID(type, "showMobileScreen", "(Ljava/lang/String;)V") : nullptr;
    interfacePresentation.begin(mobileScreenRevision, !screen.empty(), versionedMethod != nullptr);
    const auto value = env->NewStringUTF(screen.c_str());
    if (versionedMethod && value) env->CallVoidMethod(activity, versionedMethod, value, static_cast<jlong>(mobileScreenRevision));
    else if (method && value) env->CallVoidMethod(activity, method, value);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (value) env->DeleteLocalRef(value);
    if (type) env->DeleteLocalRef(type);
    if (screen.empty() && imageActivity)
    {
        env->DeleteWeakGlobalRef(imageActivity);
        imageActivity = nullptr;
        imageMethodsKnown = false;
        imagePoll = nullptr;
        imageReply = nullptr;
        ++imageActivityRevision;
    }
    env->DeleteLocalRef(activity);
}

inline uint32_t mobileShipHudMask()
{
    if (!mobileScreen.empty() || loadingState != 0 || cinematicState == 1) return 0;
    return shipTelemetry.mask();
}

inline std::vector<std::string> pollMobileActions()
{
    std::vector<std::string> result;
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return result;
    const auto type = env->GetObjectClass(activity);
    const auto method = type ? env->GetMethodID(type, "pollMobileAction", "()[B") : nullptr;
    if (method)
    {
        for (int i = 0; i < 32 && !env->ExceptionCheck(); ++i)
        {
            const auto value = static_cast<jbyteArray>(env->CallObjectMethod(activity, method));
            if (!value) break;
            const auto size = env->GetArrayLength(value);
            if (size <= 8192)
            {
                std::string text(size, '\0');
                env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte *>(text.data()));
                if (!env->ExceptionCheck()) result.push_back(std::move(text));
            }
            env->DeleteLocalRef(value);
        }
    }
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return result;
}
} // namespace storm::android
#endif
