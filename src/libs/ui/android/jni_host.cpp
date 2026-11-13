// JNI surface of the UI runtime for the application screens without the engine
// (org.stormengine.port.StormUi; the game hosts the UI in the engine: src/libs/ui_host).
// Every call arrives on the GL thread of the host view (GLSurfaceView), with its context
// current. bgfx draws on that context (bgfx::PlatformData::context: bgfx neither creates nor
// swaps the surface) through the same RmlUi backend as the game; this file plays the frame owner.
// Actions are queued and handed to Java in batches after a frame, so no native code
// ever calls back into the VM.
#include "storm_ui/asset_files.h"
#include "storm_ui/backend_bgfx.h"
#include "storm_ui/ui.h"

#include <EGL/egl.h>
#include <android/asset_manager_jni.h>
#include <bgfx/bgfx.h>
#include <cstdarg>
#include <cstdlib>
#include <android/log.h>
#include <jni.h>
#include <memory>
#include <string>
#include <time.h>
#include <vector>

namespace
{

// bgfx's fatal errors end the process loudly; its traces are not needed here.
struct Callback final : bgfx::CallbackI
{
    void fatal(const char *file, uint16_t line, bgfx::Fatal::Enum, const char *message) override
    {
        __android_log_print(ANDROID_LOG_FATAL, "StormUi", "bgfx fatal %s:%u: %s", file, line, message);
        std::abort();
    }
    void traceVargs(const char *, uint16_t, const char *, va_list) override
    {
    }
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override
    {
    }
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override
    {
    }
    void profilerEnd() override
    {
    }
    uint32_t cacheReadSize(uint64_t) override
    {
        return 0;
    }
    bool cacheRead(uint64_t, void *, uint32_t) override
    {
        return false;
    }
    void cacheWrite(uint64_t, const void *, uint32_t) override
    {
    }
    void screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *, uint32_t, bool) override
    {
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override
    {
    }
    void captureEnd() override
    {
    }
    void captureFrame(const void *, uint32_t) override
    {
    }
};
Callback g_callback;

struct Session
{
    std::unique_ptr<storm::ui::FileSource> files;
    std::unique_ptr<storm::ui::BgfxBackend> backend; // bgfx on the host view's GLES 3 context
    std::unique_ptr<storm::ui::Host> host;
    std::vector<storm::ui::Action> actions;
    std::vector<storm::ui::TextRequest> text_requests;
    jobject assets_reference = nullptr; // keeps the Java AssetManager alive
    bool bgfx_started = false;
    uint16_t width = 0, height = 0;
    uint64_t frame_index = 0;
    // Debug hosts log what the UI frames cost, every few seconds (logcat tag StormUiFrames).
    bool log_frames = false;
    struct
    {
        double since = -1.0; // monotonic seconds
        int prepares = 0, drawn = 0, cached_layers = 0, layers = 0, textures = 0, rebuilds = 0;
        double update_ms = 0.0, render_ms = 0.0, frame_ms = 0.0, worst_ms = 0.0;
    } frames;
};

double Seconds()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

void LogFrames(Session &session, double now)
{
    auto &f = session.frames;
    if (f.since < 0.0)
        f.since = now;
    if (now - f.since < 5.0)
        return;
    const double drawn = f.drawn > 0 ? f.drawn : 1;
    __android_log_print(ANDROID_LOG_INFO, "StormUiFrames",
                        "%.1f s: %d prepares, %d frames drawn (%.1f/s); per drawn frame: %.2f ms in Frame (worst %.2f) = update %.2f + render %.2f "
                        "(CPU; GPU work is asynchronous), rebuilds %.1f, layers %.1f (cached %.1f), textures %.2f",
                        now - f.since, f.prepares, f.drawn, f.drawn / (now - f.since), f.frame_ms / drawn, f.worst_ms, f.update_ms / drawn,
                        f.render_ms / drawn, f.rebuilds / drawn, f.layers / drawn, f.cached_layers / drawn, f.textures / drawn);
    f = {};
    f.since = now;
}

std::string Bytes(JNIEnv *env, jbyteArray value)
{
    const jsize size = env->GetArrayLength(value);
    std::string text(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte *>(text.data()));
    return text;
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

Session *From(jlong handle)
{
    return reinterpret_cast<Session *>(handle);
}

void Destroy(JNIEnv *env, std::unique_ptr<Session> session)
{
    session->host.reset();
    session->backend.reset();
    if (session->bgfx_started)
        bgfx::shutdown();
    env->DeleteGlobalRef(session->assets_reference);
}

// One frame of the UI over the surface: the UI views draw into the back buffer of the view's
// EGL surface (GLSurfaceView swaps it after onDrawFrame).
double DrawFrame(Session &session)
{
    const storm::render::FrameContext frame = [&] {
        storm::render::FrameContext context;
        context.frame_index = ++session.frame_index;
        return context;
    }();
    for (uint16_t view = frame.ui_view_begin; view < frame.ui_view_begin + frame.ui_view_count; ++view)
    {
        bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(view, 0, 0, session.width, session.height);
        bgfx::setViewClear(view, BGFX_CLEAR_NONE);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    }
    // The screen has no game under it: the surface starts clear.
    bgfx::setViewClear(frame.ui_view_begin, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0x000000ff, 1.0f, 0);
    bgfx::touch(frame.ui_view_begin);
    session.backend->OnUiRender(frame);
    bgfx::frame();
    return session.backend->NextFrameDelay();
}

} // namespace

extern "C"
{

JNIEXPORT jlong JNICALL Java_org_stormengine_port_StormUi_nativeCreate(JNIEnv *env, jclass, jobject assets, jstring directory, jint width,
                                                                        jint height, jfloat density, jstring game_root, jboolean show_errors)
{
    auto session = std::make_unique<Session>();
    session->assets_reference = env->NewGlobalRef(assets);
    session->files = storm::ui::AssetFiles(AAssetManager_fromJava(env, session->assets_reference), Text(env, directory));
    storm::ui::Config config;
    config.files = session->files.get();
    config.width = width;
    config.height = height;
    config.density = density;
    config.game_root = Text(env, game_root);
    Session *raw = session.get();
    config.on_action = [raw](const storm::ui::Action &action) { raw->actions.push_back(action); };
    config.on_text_request = [raw](const storm::ui::TextRequest &request) { raw->text_requests.push_back(request); };
    config.show_errors = show_errors == JNI_TRUE;
    session->log_frames = config.show_errors;
    std::string error;
    const double started = Seconds();
    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGLES;
    init.platformData.context = eglGetCurrentContext(); // the view's: bgfx draws on it, the view swaps
    init.swapChain.width = uint32_t(width); // no window handle: bgfx draws to the current surface
    init.swapChain.height = uint32_t(height);
    init.callback = &g_callback;
    session->width = uint16_t(width);
    session->height = uint16_t(height);
    // bgfx is built multithreaded (the game's render thread); here it must draw on this thread, whose context
    // the view made current: a renderFrame() call before init is bgfx's switch for that (no render thread).
    bgfx::renderFrame();
    if (init.platformData.context == EGL_NO_CONTEXT)
        error = "no current EGL context on the UI thread";
    else if (!(session->bgfx_started = bgfx::init(init)))
        error = "bgfx::init failed on the view's GLES context";
    const double device_ready = Seconds();
    if (session->bgfx_started && (session->backend = storm::ui::BgfxBackend::Create({}, &error)))
    {
        session->backend->SetViewport(width, height);
        config.backend = session->backend.get();
        session->host = storm::ui::Host::Create(config, &error);
    }
    if (!session->host)
    {
        __android_log_print(ANDROID_LOG_ERROR, "StormUi", "UI host failed: %s", error.c_str());
        Destroy(env, std::move(session));
        return 0;
    }
    session->backend->AttachHost(session->host.get());
    __android_log_print(ANDROID_LOG_INFO, "StormUi", "UI host started on bgfx: device %.1f ms, host %.1f ms (%dx%d)", (device_ready - started) * 1000.0,
                        (Seconds() - device_ready) * 1000.0, width, height);
    return reinterpret_cast<jlong>(session.release());
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeDestroy(JNIEnv *env, jclass, jlong handle)
{
    std::unique_ptr<Session> session(From(handle));
    if (session)
        Destroy(env, std::move(session));
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeLoadDocument(JNIEnv *env, jclass, jlong handle, jstring path)
{
    return From(handle)->host->LoadDocument(Text(env, path)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeResize(JNIEnv *, jclass, jlong handle, jint width, jint height, jfloat density)
{
    Session *session = From(handle);
    session->width = uint16_t(width);
    session->height = uint16_t(height);
    bgfx::SwapChain chain;
    chain.width = uint32_t(width);
    chain.height = uint32_t(height);
    bgfx::reset(BGFX_RESET_NONE, &chain);
    session->host->Resize(width, height, density);
}

// phase: 0 down, 1 move, 2 up, 3 cancel
// time: the sample's MotionEvent time (SystemClock.uptimeMillis, CLOCK_MONOTONIC ms): every
// sample counts with its own time for drag velocities (the view sends a batched move's
// historical samples too).
JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeTouch(JNIEnv *, jclass, jlong handle, jint phase, jint pointer, jfloat x, jfloat y,
                                                                      jlong time)
{
    if (phase < 0 || phase > 3)
        return;
    storm::ui::Host &host = *From(handle)->host;
    host.Touch(static_cast<storm::ui::TouchPhase>(phase), pointer, x, y, host.Now() - (Seconds() - static_cast<double>(time) / 1000.0));
}

// Settles the UI without drawing: < 0 when the frame on the screen is out of date (ask for
// a frame), otherwise the seconds until the next Prepare.
JNIEXPORT jdouble JNICALL Java_org_stormengine_port_StormUi_nativePrepare(JNIEnv *, jclass, jlong handle)
{
    Session *session = From(handle);
    double delay = 0.0;
    const bool draw = session->host->Prepare(delay);
    if (session->log_frames)
    {
        ++session->frames.prepares;
        LogFrames(*session, Seconds());
    }
    return draw ? -1.0 : delay;
}

JNIEXPORT jdouble JNICALL Java_org_stormengine_port_StormUi_nativeFrame(JNIEnv *, jclass, jlong handle)
{
    Session *session = From(handle);
    const double started = session->log_frames ? Seconds() : 0.0;
    const double delay = DrawFrame(*session);
    if (session->log_frames)
    {
        const double now = Seconds();
        const storm::ui::FrameStats stats = session->host->LastFrameStats();
        auto &f = session->frames;
        const double ms = (now - started) * 1000.0;
        ++f.drawn;
        f.frame_ms += ms;
        f.worst_ms = ms > f.worst_ms ? ms : f.worst_ms;
        f.update_ms += stats.update_ms;
        f.render_ms += stats.render_ms;
        f.rebuilds += stats.rebuilds;
        f.layers += stats.layers;
        f.cached_layers += stats.cached_layers;
        f.textures += stats.textures;
        LogFrames(*session, now);
    }
    return delay;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeToast(JNIEnv *env, jclass, jlong handle, jstring text, jstring tone)
{
    From(handle)->host->Toast(Text(env, text), Text(env, tone));
}

// A state channel of the screen ("launcher", "downloads", …).
JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeSetState(JNIEnv *env, jclass, jlong handle, jstring channel, jbyteArray json)
{
    return From(handle)->host->SetState(Text(env, channel), Bytes(env, json)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeBack(JNIEnv *, jclass, jlong handle)
{
    From(handle)->host->Back();
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeSubmitText(JNIEnv *env, jclass, jlong handle, jstring token, jstring text,
                                                                         jboolean accepted)
{
    From(handle)->host->SubmitText(Text(env, token), Text(env, text), accepted == JNI_TRUE);
}

// The oldest pending editor request as {token, text, title, numeric, x, y, width, height, secret}, or null.
JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_StormUi_nativeTakeTextRequest(JNIEnv *env, jclass, jlong handle)
{
    Session *session = From(handle);
    if (session->text_requests.empty())
        return nullptr;
    const storm::ui::TextRequest request = session->text_requests.front();
    session->text_requests.erase(session->text_requests.begin());
    const std::string parts[] = {request.token,           request.text,           request.title,              request.numeric ? "1" : "0",
                                 std::to_string(request.x), std::to_string(request.y), std::to_string(request.width), std::to_string(request.height), request.secret ? "1" : "0"};
    jobjectArray result = env->NewObjectArray(9, env->FindClass("java/lang/String"), nullptr);
    for (jsize i = 0; i < 9; ++i)
    {
        jstring value = env->NewStringUTF(parts[i].c_str());
        env->SetObjectArrayElement(result, i, value);
        env->DeleteLocalRef(value);
    }
    return result;
}

// Returns the queued actions as a flat array {screen, node, verb, value, engine command, …},
// or null. The engine command is empty for actions of a plain document (gallery). HUD input
// has the screen "hud"; its node names a latest-value slot (see storm::ui::Action).
JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_StormUi_nativeTakeActions(JNIEnv *env, jclass, jlong handle)
{
    Session *session = From(handle);
    if (session->actions.empty())
        return nullptr;
    const jclass string_class = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(session->actions.size() * 5), string_class, nullptr);
    jsize index = 0;
    for (const storm::ui::Action &action : session->actions)
    {
        const std::string command = storm::ui::EngineCommand(action);
        for (const std::string *part : {&action.screen, &action.node, &action.verb, &action.value, &command})
        {
            jstring value = env->NewStringUTF(part->c_str());
            env->SetObjectArrayElement(result, index++, value);
            env->DeleteLocalRef(value);
        }
    }
    session->actions.clear();
    return result;
}

} // extern "C"
