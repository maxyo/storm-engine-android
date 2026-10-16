#pragma once
#ifdef __ANDROID__
#include <platform/android_ui.hpp>
#include <core.h>
#include <charconv>

namespace storm::android {
// Runtime dialogue data only; neither dialogue strings nor game files are bundled.
inline const void *dialogueOwner = nullptr;
inline std::string dialogueSnapshot;
inline uint64_t dialogueGeneration = 0;
inline std::string dialogueJsonString(std::string_view value) {
    std::string out = "\"";
    for (const unsigned char c : value) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c >= 32) out += c;
    }
    return out + '"';
}
inline std::string dialogueJsonString(const char *value) { return dialogueJsonString(std::string_view(value ? value : "")); }
inline bool sendDialogue(const std::string &json) {
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return false;
    auto type = env->GetObjectClass(activity);
    auto method = type ? env->GetMethodID(type, "showMobileDialogue", "([B)V") : nullptr;
    auto bytes = env->NewByteArray(static_cast<jsize>(json.size()));
    if (bytes) env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
    bool success = method && bytes && !env->ExceptionCheck();
    if (success) env->CallVoidMethod(activity, method, bytes);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); success = false; }
    if (bytes) env->DeleteLocalRef(bytes);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return success;
}
inline void closeDialogue(const void *owner) {
    if (dialogueOwner != owner) return;
    sendDialogue("{\"active\":false}");
    dialogueOwner = nullptr; dialogueSnapshot.clear(); dialogueActions.clear();
}
inline bool publishDialogue(const void *owner, ATTRIBUTES *attrs, const char *speaker) {
    if (!attrs) { closeDialogue(owner); return false; }
    auto *links = attrs->GetAttributeClass("Links");
    // Editable links use the engine's variable editor until an Android equivalent exists.
    if (links) for (size_t i=0; i<links->GetAttributesNum(); ++i) {
        auto *link = links->GetAttributeClass(i);
        if (link && link->GetAttribute("edit")) { closeDialogue(owner); return false; }
    }
    std::string body = "\"speaker\":" + dialogueJsonString(speaker) + ",\"text\":" + dialogueJsonString(attrs->GetAttribute("Text")) + ",\"choices\":[";
    bool first = true;
    if (links) for (size_t i=0; i<links->GetAttributesNum(); ++i) {
        auto *link = links->GetAttributeClass(i);
        if (!link || !link->GetAttribute("go") || !link->HasValue()) continue;
        if (!first) body += ',';
        first = false;
        body += "{\"index\":" + std::to_string(i) + ",\"text\":" + dialogueJsonString(link->GetValue()) + '}';
    }
    body += ']';
    if (dialogueOwner == owner && dialogueSnapshot == body) return true;
    auto generation = dialogueGeneration + 1;
    if (!sendDialogue("{\"active\":true,\"generation\":" + std::to_string(generation) + ',' + body + '}')) return false;
    dialogueOwner=owner; dialogueSnapshot=body; dialogueGeneration=generation; dialogueActions.clear();
    return true;
}
inline ATTRIBUTES *takeDialogueChoice(const void *owner, ATTRIBUTES *attrs) {
    while (!dialogueActions.empty()) {
        auto request=std::move(dialogueActions.front()); dialogueActions.pop_front();
        auto split=request.find(':'); if (split==std::string::npos) continue;
        uint64_t generation=0; unsigned index=0;
        auto a=std::from_chars(request.data(),request.data()+split,generation);
        auto b=std::from_chars(request.data()+split+1,request.data()+request.size(),index);
        if (a.ec!=std::errc{} || a.ptr!=request.data()+split || b.ec!=std::errc{} || b.ptr!=request.data()+request.size() || dialogueOwner!=owner || generation!=dialogueGeneration || !attrs) continue;
        auto *links=attrs->GetAttributeClass("Links");
        if (!links || index>=links->GetAttributesNum()) continue;
        auto *link=links->GetAttributeClass(index);
        if (link && link->GetAttribute("go") && !link->GetAttribute("edit")) { dialogueActions.clear(); dialogueSnapshot.clear(); return link; }
    }
    return nullptr;
}
} // namespace storm::android
#endif
