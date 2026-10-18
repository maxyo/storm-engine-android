#pragma once
#include <deque>
#include <string>
#include <vector>

// The production bridge is tested with in-memory JNI delivery on the game thread.
using jobject = void *;
using jsize = int;
using jbyte = signed char;
inline std::vector<std::string> publications;
struct JNIEnv {
    std::string bytes;
    jobject GetObjectClass(jobject value) { return value; }
    jobject GetMethodID(jobject value, const char *, const char *) { return value; }
    jobject NewByteArray(jsize size) { bytes.resize(size); return this; }
    void SetByteArrayRegion(jobject, jsize, jsize size, const jbyte *data) {
        bytes.assign(reinterpret_cast<const char *>(data), size);
    }
    bool ExceptionCheck() { return false; }
    void ExceptionDescribe() {}
    void ExceptionClear() {}
    void CallVoidMethod(jobject, jobject, jobject) { publications.push_back(bytes); }
    void DeleteLocalRef(jobject) {}
};
inline JNIEnv env;
inline void *SDL_AndroidGetJNIEnv() { return &env; }
inline void *SDL_AndroidGetActivity() { return &env; }
namespace storm::android { inline std::deque<std::string> dialogueActions; }
