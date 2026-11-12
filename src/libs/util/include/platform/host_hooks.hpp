#pragma once

// Engine -> host OS integration hooks. Platform-neutral interface; the Android
// implementation talks to the app's Java side, every other platform is a no-op.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#endif

namespace storm::platform
{
// Time the game thread blocked in presents since the last FrameWorkClock lap: the frame pacing waiting for the
// frame's vsync slot (renderer_bgfx shim/frame_pacing.cpp), which is not work. Game thread only.
inline std::chrono::nanoseconds presentWait{0};

inline void presentWaited(std::chrono::nanoseconds wait)
{
    presentWait += wait;
}

// CPU work of one main-loop iteration that ran a frame: the wall time between two
// loop tops minus the time its presents waited (presentWait). Iterations that did not
// run a frame (backgrounded, paused) report nothing. Game thread only.
class FrameWorkClock
{
  public:
    using Clock = std::chrono::steady_clock;

    // Call at the top of every loop iteration. Returns the previous iteration's work
    // if it ran a frame.
    std::optional<Clock::duration> lap(Clock::time_point now)
    {
        std::optional<Clock::duration> work;
        if (started_ && frame_)
            work = std::max(Clock::duration::zero(),
                            now - start_ - std::chrono::duration_cast<Clock::duration>(presentWait));
        start_ = now;
        started_ = true;
        frame_ = false;
        presentWait = std::chrono::nanoseconds::zero();
        return work;
    }

    // The current iteration runs a frame.
    void frame()
    {
        frame_ = true;
    }

  private:
    Clock::time_point start_{};
    bool started_ = false;
    bool frame_ = false;
};

// Number of save files being written right now; readable from any thread. The
// Android host's bounded exit wait keeps waiting while it is non-zero, so its
// deliberate process kill never lands in the middle of a save (the atomic save
// would keep the previous file, but the new one would be lost).
inline std::atomic<int> savesInProgress{0};

class SaveInProgress
{
  public:
    SaveInProgress() { savesInProgress.fetch_add(1, std::memory_order_acq_rel); }
    ~SaveInProgress() { savesInProgress.fetch_sub(1, std::memory_order_acq_rel); }
    SaveInProgress(const SaveInProgress &) = delete;
    SaveInProgress &operator=(const SaveInProgress &) = delete;
};

inline bool saveInProgress()
{
    return savesInProgress.load(std::memory_order_acquire) > 0;
}

#ifdef __ANDROID__
// Feeds the app's ADPF PerformanceHint session through two static methods of
// org.stormengine.port.GamePerformance: reportFrameWork(long nanos) and updateTargetWork(long nanos), "(J)V".
// The class and methods are resolved once through the activity's class loader (FindClass
// on a native thread would only see the system loader) and cached as a global ref, so a
// call is one static call without allocations. If the class or a method is missing, or
// a call throws, the reason is logged once and the hint stays disabled. Game thread only.
class FrameWorkHint
{
  public:
    void report(std::chrono::nanoseconds work)
    {
        call(&FrameWorkHint::report_, work);
    }

    void target(std::chrono::nanoseconds target)
    {
        call(&FrameWorkHint::target_, target);
    }

  private:
    enum class State
    {
        Unresolved,
        Ready,
        Disabled
    };

    void call(jmethodID FrameWorkHint::*method, std::chrono::nanoseconds value)
    {
        if (state_ == State::Disabled)
            return;
        // SDL attaches the calling thread on first use and caches the env per thread.
        auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        if (!env)
            return disable("no JNIEnv for the game thread");
        if (state_ == State::Unresolved && !resolve(env))
            return;
        env->CallStaticVoidMethod(class_, this->*method, static_cast<jlong>(value.count()));
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
            disable(method == &FrameWorkHint::report_ ? "GamePerformance.reportFrameWork threw"
                                                      : "GamePerformance.updateTargetWork threw");
        }
    }

    bool resolve(JNIEnv *env)
    {
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!activity)
        {
            disable("no activity");
            return false;
        }
        jclass found = nullptr;
        if (auto activityClass = env->GetObjectClass(activity))
        {
            auto classClass = env->FindClass("java/lang/Class");
            auto getClassLoader =
                classClass ? env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
            auto loader = getClassLoader ? env->CallObjectMethod(activityClass, getClassLoader) : nullptr;
            auto loaderClass = loader && !env->ExceptionCheck() ? env->GetObjectClass(loader) : nullptr;
            auto loadClass = loaderClass
                                 ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
                                 : nullptr;
            auto name = loadClass ? env->NewStringUTF("org.stormengine.port.GamePerformance") : nullptr;
            found = name ? static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name)) : nullptr;
            for (jobject local : {static_cast<jobject>(name), static_cast<jobject>(loaderClass), loader,
                                  static_cast<jobject>(classClass), static_cast<jobject>(activityClass)})
                if (local)
                    env->DeleteLocalRef(local);
        }
        env->DeleteLocalRef(activity);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (!found)
        {
            disable("class org.stormengine.port.GamePerformance not found");
            return false;
        }
        report_ = env->GetStaticMethodID(found, "reportFrameWork", "(J)V");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        target_ = env->GetStaticMethodID(found, "updateTargetWork", "(J)V");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (report_ && target_)
            class_ = static_cast<jclass>(env->NewGlobalRef(found));
        env->DeleteLocalRef(found);
        if (!report_ || !target_ || !class_)
        {
            disable("static GamePerformance.reportFrameWork(J)V or updateTargetWork(J)V not found");
            return false;
        }
        state_ = State::Ready;
        __android_log_print(ANDROID_LOG_INFO, "StormPerformance", "Reporting frame work to the ADPF hint session");
        return true;
    }

    void disable(const char *reason)
    {
        state_ = State::Disabled;
        __android_log_print(ANDROID_LOG_WARN, "StormPerformance", "Frame work reporting disabled: %s", reason);
    }

    State state_ = State::Unresolved;
    jclass class_ = nullptr;
    jmethodID report_ = nullptr;
    jmethodID target_ = nullptr;
};

inline FrameWorkHint &frameWorkHint()
{
    static FrameWorkHint hint;
    return hint;
}
#endif

// Reports the CPU work of the last frame to the host's performance hint. Game thread only.
inline void reportFrameWork(std::chrono::nanoseconds work)
{
#ifdef __ANDROID__
    frameWorkHint().report(work);
#else
    (void)work;
#endif
}

// The work duration the host's performance hint aims at: the frame pacing's preferred swap interval. Game thread only.
inline void frameWorkTarget(std::chrono::nanoseconds target)
{
#ifdef __ANDROID__
    frameWorkHint().target(target);
#else
    (void)target;
#endif
}
} // namespace storm::platform
