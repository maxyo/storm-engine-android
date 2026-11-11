#pragma once
#include <deque>
#include <string>
#include <string_view>
#include <vector>

// The production bridge is tested with in-memory delivery on the game thread: the dialogue
// state goes to the UI host (ui::state), the activity only learns whether a dialogue is shown.
using jobject = void *;
using jboolean = unsigned char;
inline std::vector<std::string> publications;
struct JNIEnv {
    jobject GetObjectClass(jobject value) { return value; }
    jobject GetMethodID(jobject value, const char *, const char *) { return value; }
    bool ExceptionCheck() { return false; }
    void ExceptionDescribe() {}
    void ExceptionClear() {}
    void CallVoidMethod(jobject, jobject, jboolean active) { shown.push_back(active != 0); }
    std::vector<bool> shown;
    void DeleteLocalRef(jobject) {}
};
inline JNIEnv env;
inline void *SDL_AndroidGetJNIEnv() { return &env; }
inline void *SDL_AndroidGetActivity() { return &env; }
namespace storm::android { inline std::deque<std::string> dialogueActions; }
namespace storm::android::ui { inline void state(std::string_view, std::string json) { publications.push_back(std::move(json)); } }
namespace spdlog { template <typename... Args> void error(Args &&...) {} }
