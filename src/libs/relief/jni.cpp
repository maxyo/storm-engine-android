// Android JNI: org.stormengine.port.NativeRelief (the launcher's import step). Contract: README.md.
#include "relief.hpp"
#include "storm/fx/relief_store.hpp"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <string>

namespace
{
std::string Utf8(JNIEnv *env, jstring value)
{
    if (!value)
        return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    std::string text = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(value, chars);
    return text;
}

// The model from the APK's assets, mapped while the job runs.
struct Asset
{
    AAsset *asset = nullptr;
    ~Asset()
    {
        if (asset)
            AAsset_close(asset);
    }
};
} // namespace

extern "C" JNIEXPORT jstring JNICALL Java_org_stormengine_port_NativeRelief_prepare(
    JNIEnv *env, jclass, jobject assets, jstring model_asset, jstring textures, jstring output, jint threads,
    jobject progress)
{
    using namespace storm::relief;
    Result result;
    Asset model;
    AAssetManager *manager = assets ? AAssetManager_fromJava(env, assets) : nullptr;
    if (manager)
        model.asset = AAssetManager_open(manager, Utf8(env, model_asset).c_str(), AASSET_MODE_BUFFER);
    const void *buffer = model.asset ? AAsset_getBuffer(model.asset) : nullptr;
    if (!buffer)
    {
        result.status = 2;
        result.error = "the DeepBump model asset " + Utf8(env, model_asset) + " is missing from the APK";
        Log("ERROR: %s", result.error.c_str());
        return env->NewStringUTF(result.Json().c_str());
    }
    Job job;
    job.textures = Utf8(env, textures);
    job.output = Utf8(env, output);
    job.model = {static_cast<const uint8_t *>(buffer), static_cast<size_t>(AAsset_getLength64(model.asset))};
    job.map_size = storm::fx::kReliefMapSize;
    job.threads = threads;
    jclass type = progress ? env->GetObjectClass(progress) : nullptr;
    jmethodID on_progress = type ? env->GetMethodID(type, "onProgress", "(IILjava/lang/String;)Z") : nullptr;
    result = Prepare(job, [&](const Progress &state) {
        if (!on_progress)
            return false;
        jstring name = env->NewStringUTF(state.current.c_str());
        const bool cancel = env->CallBooleanMethod(progress, on_progress, state.done, state.total, name);
        env->DeleteLocalRef(name);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return true;
        }
        return cancel;
    });
    if (type)
        env->DeleteLocalRef(type);
    return env->NewStringUTF(result.Json().c_str());
}

extern "C" JNIEXPORT jintArray JNICALL Java_org_stormengine_port_NativeRelief_check(JNIEnv *env, jclass, jstring textures,
                                                                                    jstring output)
{
    const storm::relief::Status status = storm::relief::Check(Utf8(env, textures), Utf8(env, output), storm::fx::kReliefMapSize);
    const jint values[2] = {status.total, status.missing};
    jintArray array = env->NewIntArray(2);
    env->SetIntArrayRegion(array, 0, 2, values);
    return array;
}
