#pragma once
#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <jni.h>
#include <string>
#include <vector>
#include "android_ship_telemetry.hpp"
#include "android_sea_orders.hpp"

namespace storm::android
{
struct UiTouch { float x, y; int phase; };
// All native state is accessed only by the game thread. Java marshals its UI queue.
inline std::string mobileScreen;
inline uint64_t mobileScreenRevision = 1;
inline std::deque<std::string> dialogueActions;
inline std::deque<UiTouch> uiTouches;
struct UiWidgetAction { std::string screen, node, value; int index; };
inline std::deque<UiWidgetAction> uiWidgetActions;
inline std::string lastWidgetSnapshot;
inline std::string contextAction, requestedContextAction;
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
// Optional gameplay-state v1 bridge. Cached per Activity, never per process:
// activity recreation must receive an identical snapshot again. One weak ref,
// one method id and one bounded snapshot, all owned by the game thread.
inline jweak gameplayActivity = nullptr;
inline jmethodID gameplayMethod = nullptr;
inline bool gameplayMethodKnown = false;
inline std::string lastGameplayState;
inline void publishGameplayState(const std::string &json)
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    if (!gameplayActivity || !env->IsSameObject(gameplayActivity, activity))
    {
        if (gameplayActivity) env->DeleteWeakGlobalRef(gameplayActivity);
        gameplayActivity = env->NewWeakGlobalRef(activity);
        gameplayMethodKnown = false;
        gameplayMethod = nullptr;
        lastGameplayState.clear();
    }
    if (!gameplayMethodKnown && !env->ExceptionCheck())
    {
        auto type = env->GetObjectClass(activity);
        if (type)
        {
            gameplayMethod = env->GetMethodID(type, "showGameplayState", "([B)V");
            env->DeleteLocalRef(type);
            gameplayMethodKnown = true;
        }
        // Old apps intentionally omit the optional method.
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (gameplayMethod && lastGameplayState != json && json.size() <= 8192 && !env->ExceptionCheck())
    {
        auto bytes = env->NewByteArray(static_cast<jsize>(json.size()));
        if (bytes && !env->ExceptionCheck())
            env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
        if (bytes && !env->ExceptionCheck()) env->CallVoidMethod(activity, gameplayMethod, bytes);
        if (bytes && !env->ExceptionCheck()) lastGameplayState = json;
        if (bytes) env->DeleteLocalRef(bytes);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}
// Optional live ship telemetry channel. Independent from action revisions and
// replayed to every Activity. Rendering acknowledgements are never inherited.
inline jweak shipTelemetryActivity = nullptr;
inline jmethodID shipTelemetryMethod = nullptr;
inline bool shipTelemetryMethodKnown = false;
inline void publishShipTelemetry()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    if (!shipTelemetryActivity || !env->IsSameObject(shipTelemetryActivity, activity))
    {
        if (shipTelemetryActivity) env->DeleteWeakGlobalRef(shipTelemetryActivity);
        shipTelemetryActivity = env->NewWeakGlobalRef(activity);
        shipTelemetryMethod = nullptr;
        shipTelemetryMethodKnown = false;
        shipTelemetry.resetActivity();
    }
    if (!shipTelemetryMethodKnown && !env->ExceptionCheck())
    {
        auto type = env->GetObjectClass(activity);
        if (type)
        {
            shipTelemetryMethod = env->GetMethodID(type, "showShipTelemetry", "([B)V");
            shipTelemetryMethodKnown = true;
            env->DeleteLocalRef(type);
        }
        if (env->ExceptionCheck()) env->ExceptionClear(); // Old app: retain all native indicators.
    }
    if (shipTelemetryMethod && !env->ExceptionCheck())
    {
        const auto tick = SDL_GetTicks();
        if (const auto json = shipTelemetry.publication(tick))
        {
            auto bytes = env->NewByteArray(static_cast<jsize>(json->size()));
            if (bytes && !env->ExceptionCheck())
                env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json->size()), reinterpret_cast<const jbyte *>(json->data()));
            if (bytes && !env->ExceptionCheck()) env->CallVoidMethod(activity, shipTelemetryMethod, bytes);
            if (bytes && !env->ExceptionCheck()) shipTelemetry.published(*json, tick);
            if (bytes) env->DeleteLocalRef(bytes);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}
inline jweak seaOrdersActivity = nullptr;
inline jmethodID seaOrdersMethod = nullptr;
inline bool seaOrdersMethodKnown = false;
inline std::string lastSeaOrders;
inline void publishSeaOrders()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    if (!seaOrdersActivity || !env->IsSameObject(seaOrdersActivity, activity))
    {
        if (seaOrdersActivity) env->DeleteWeakGlobalRef(seaOrdersActivity);
        seaOrdersActivity = env->NewWeakGlobalRef(activity);
        seaOrdersMethod = nullptr; seaOrdersMethodKnown = false; lastSeaOrders.clear();
        seaOrders.resetActivity();
    }
    if (!seaOrdersMethodKnown && !env->ExceptionCheck())
    {
        auto type = env->GetObjectClass(activity);
        if (type)
        {
            seaOrdersMethod = env->GetMethodID(type, "showSeaOrders", "([B)V");
            env->DeleteLocalRef(type); seaOrdersMethodKnown = true;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    const auto json = seaOrders.json();
    if (seaOrdersMethod && json != lastSeaOrders && json.size() <= 65536 && !env->ExceptionCheck())
    {
        auto bytes = env->NewByteArray(static_cast<jsize>(json.size()));
        if (bytes && !env->ExceptionCheck())
            env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
        if (bytes && !env->ExceptionCheck()) env->CallVoidMethod(activity, seaOrdersMethod, bytes);
        if (bytes && !env->ExceptionCheck()) lastSeaOrders = json;
        if (bytes) env->DeleteLocalRef(bytes);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}
inline std::string lastVitals;
inline uint32_t lastVitalsTick=0;
inline void publishVitals(const std::string &json) {
    const auto now=SDL_GetTicks();
    if(lastVitals==json || (!json.empty() && now-lastVitalsTick<100))return;
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"showVitals","([B)V") : nullptr;
    auto bytes=env->NewByteArray(static_cast<jsize>(json.size()));
    if(bytes)env->SetByteArrayRegion(bytes,0,static_cast<jsize>(json.size()),reinterpret_cast<const jbyte *>(json.data()));
    if(method && bytes)env->CallVoidMethod(activity,method,bytes);
    if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();}
    else {lastVitals=json;lastVitalsTick=now;}
    if(bytes)env->DeleteLocalRef(bytes);
    if(type)env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
inline std::string lastWorldInfo;
inline void publishWorldInfo(const std::string &text) {
    if (lastWorldInfo == text) return;
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"showWorldInfo","([B)V") : nullptr;
    auto bytes=env->NewByteArray(static_cast<jsize>(text.size()));
    if (bytes) env->SetByteArrayRegion(bytes,0,static_cast<jsize>(text.size()),reinterpret_cast<const jbyte *>(text.data()));
    if (method && bytes) env->CallVoidMethod(activity,method,bytes);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    else lastWorldInfo=text;
    if (bytes) env->DeleteLocalRef(bytes);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
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
    contextAction = name; requestedContextAction.clear();
    auto *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    auto type=env->GetObjectClass(activity);
    auto method=type ? env->GetMethodID(type,"showContextAction","(Ljava/lang/String;)V") : nullptr;
    auto text=env->NewStringUTF(name.c_str());
    if (method && text) env->CallVoidMethod(activity,method,text);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (text) env->DeleteLocalRef(text);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
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
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    const auto type = env->GetObjectClass(activity);
    const auto method = type ? env->GetMethodID(type, "showMobileWidgets", "([B)V") : nullptr;
    const auto bytes = env->NewByteArray(static_cast<jsize>(json.size()));
    if (bytes) env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
    if (method && bytes) env->CallVoidMethod(activity, method, bytes);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    else lastWidgetSnapshot = json;
    if (bytes) env->DeleteLocalRef(bytes);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
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
    uiTouches.clear();
    uiWidgetActions.clear();
    lastWidgetSnapshot.clear();
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    const auto type = env->GetObjectClass(activity);
    const auto method = type ? env->GetMethodID(type, "showMobileScreen", "(Ljava/lang/String;)V") : nullptr;
    const auto value = env->NewStringUTF(screen.c_str());
    if (method && value) env->CallVoidMethod(activity, method, value);
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
