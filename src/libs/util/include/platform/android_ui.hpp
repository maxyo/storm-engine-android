#pragma once
#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <jni.h>
#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
#include <vector>

namespace storm::android
{
struct UiTouch { float x, y; int phase; };
// All native state is accessed only by the game thread. Java marshals its UI queue.
inline std::string mobileScreen;
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

inline void showMobileScreen(const std::string &name)
{
    auto screen = name;
    std::replace(screen.begin(), screen.end(), '\\', '/');
    screen = screen.substr(screen.find_last_of('/') + 1);
    const auto suffix = screen.find_last_of('.');
    if (suffix != std::string::npos) screen.resize(suffix);
    for (auto &c : screen) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    mobileScreen = screen;
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
    env->DeleteLocalRef(activity);
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
