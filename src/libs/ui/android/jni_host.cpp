// JNI surface of the UI runtime for the Android host (org.stormengine.port.StormUi).
// Every call arrives on the GL thread of the host view, with its context current.
// Actions are queued and handed to Java in batches after a frame, so no native code
// ever calls back into the VM.
#include "asset_files.h"
#include "storm_ui/ui.h"

#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Session
{
    std::unique_ptr<storm::ui::FileSource> files;
    std::unique_ptr<storm::ui::Host> host;
    std::vector<storm::ui::Action> actions;
    std::vector<storm::ui::TextRequest> text_requests;
    jobject assets_reference = nullptr; // keeps the Java AssetManager alive
};

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

} // namespace

extern "C"
{

JNIEXPORT jlong JNICALL Java_org_stormengine_port_StormUi_nativeCreate(JNIEnv *env, jclass, jobject assets, jstring directory, jint width,
                                                                        jint height, jfloat density, jstring game_root)
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
    std::string error;
    session->host = storm::ui::Host::Create(config, &error);
    if (!session->host)
    {
        __android_log_print(ANDROID_LOG_ERROR, "StormUi", "UI host failed: %s", error.c_str());
        env->DeleteGlobalRef(session->assets_reference);
        return 0;
    }
    return reinterpret_cast<jlong>(session.release());
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeDestroy(JNIEnv *env, jclass, jlong handle)
{
    std::unique_ptr<Session> session(From(handle));
    if (!session)
        return;
    session->host.reset();
    env->DeleteGlobalRef(session->assets_reference);
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeLoadDocument(JNIEnv *env, jclass, jlong handle, jstring path)
{
    return From(handle)->host->LoadDocument(Text(env, path)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeResize(JNIEnv *, jclass, jlong handle, jint width, jint height, jfloat density)
{
    From(handle)->host->Resize(width, height, density);
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeSetGameRoot(JNIEnv *env, jclass, jlong handle, jstring game_root)
{
    From(handle)->host->SetGameRoot(Text(env, game_root));
}

// phase: 0 down, 1 move, 2 up, 3 cancel
JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeTouch(JNIEnv *, jclass, jlong handle, jint phase, jint pointer, jfloat x, jfloat y)
{
    if (phase < 0 || phase > 3)
        return;
    From(handle)->host->Touch(static_cast<storm::ui::TouchPhase>(phase), pointer, x, y);
}

JNIEXPORT jdouble JNICALL Java_org_stormengine_port_StormUi_nativeFrame(JNIEnv *, jclass, jlong handle)
{
    return From(handle)->host->Frame();
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeToast(JNIEnv *env, jclass, jlong handle, jstring text, jstring tone)
{
    From(handle)->host->Toast(Text(env, text), Text(env, tone));
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeSetProps(JNIEnv *env, jclass, jlong handle, jstring element_id, jstring json)
{
    return From(handle)->host->SetProps(Text(env, element_id), Text(env, json)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeScrollTo(JNIEnv *env, jclass, jlong handle, jstring element_id)
{
    From(handle)->host->ScrollTo(Text(env, element_id));
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeHostOwnsInterface(JNIEnv *env, jclass, jstring screen)
{
    return storm::ui::HostOwnsInterface(Text(env, screen)) ? JNI_TRUE : JNI_FALSE;
}

// The engine's interface snapshot, forwarded as it arrived.
JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeSetInterface(JNIEnv *env, jclass, jlong handle, jbyteArray json)
{
    const jsize size = env->GetArrayLength(json);
    std::string text(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(json, 0, size, reinterpret_cast<jbyte *>(text.data()));
    return From(handle)->host->SetInterface(text) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeCloseInterface(JNIEnv *, jclass, jlong handle)
{
    From(handle)->host->CloseInterface();
}

JNIEXPORT jboolean JNICALL Java_org_stormengine_port_StormUi_nativeSetState(JNIEnv *env, jclass, jlong handle, jstring channel, jstring json)
{
    return From(handle)->host->SetState(Text(env, channel), Text(env, json)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeSubmitText(JNIEnv *env, jclass, jlong handle, jstring token, jstring text,
                                                                         jboolean accepted)
{
    From(handle)->host->SubmitText(Text(env, token), Text(env, text), accepted == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_org_stormengine_port_StormUi_nativeSetSafeArea(JNIEnv *, jclass, jlong handle, jfloat left, jfloat top,
                                                                          jfloat right, jfloat bottom)
{
    From(handle)->host->SetSafeArea(left, top, right, bottom);
}

// The oldest pending editor request as {token, text, title, numeric, x, y, width, height}, or null.
JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_StormUi_nativeTakeTextRequest(JNIEnv *env, jclass, jlong handle)
{
    Session *session = From(handle);
    if (session->text_requests.empty())
        return nullptr;
    const storm::ui::TextRequest request = session->text_requests.front();
    session->text_requests.erase(session->text_requests.begin());
    const std::string parts[] = {request.token,           request.text,           request.title,              request.numeric ? "1" : "0",
                                 std::to_string(request.x), std::to_string(request.y), std::to_string(request.width), std::to_string(request.height)};
    jobjectArray result = env->NewObjectArray(8, env->FindClass("java/lang/String"), nullptr);
    for (jsize i = 0; i < 8; ++i)
    {
        jstring value = env->NewStringUTF(parts[i].c_str());
        env->SetObjectArrayElement(result, i, value);
        env->DeleteLocalRef(value);
    }
    return result;
}

// Returns the queued actions as a flat array {node, verb, value, engine command, …}, or null.
// The engine command is empty for actions of a plain document (gallery).
JNIEXPORT jobjectArray JNICALL Java_org_stormengine_port_StormUi_nativeTakeActions(JNIEnv *env, jclass, jlong handle)
{
    Session *session = From(handle);
    if (session->actions.empty())
        return nullptr;
    const jclass string_class = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(session->actions.size() * 4), string_class, nullptr);
    jsize index = 0;
    for (const storm::ui::Action &action : session->actions)
    {
        const std::string command = storm::ui::EngineCommand(action);
        for (const std::string *part : {&action.node, &action.verb, &action.value, &command})
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
