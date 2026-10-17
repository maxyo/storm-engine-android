#pragma once
#ifdef __ANDROID__
#include <platform/android_ui.hpp>
#include <platform/android_dialogue_input.hpp>
#include <core.h>
#include <string_compare.hpp>

namespace storm::android {
// Runtime dialogue data only; neither dialogue strings nor game files are bundled.
inline const void *dialogueOwner = nullptr;
inline std::string dialogueSnapshot;
inline std::string dialogueIdentity;
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
    dialogueOwner = nullptr; dialogueSnapshot.clear(); dialogueIdentity.clear(); dialogueActions.clear();
}
inline size_t dialogueEditIndex(ATTRIBUTES *links) {
    // DlgLinkDescribe retains the last editable link when rebuilding a node.
    size_t index = size_t(-1);
    if (links) for (size_t i = 0; i < links->GetAttributesNum(); ++i) {
        auto *link = links->GetAttributeClass(i);
        if (link && link->GetAttribute("edit")) index = i;
    }
    return index;
}
inline std::string dialogueIdentityField(const char *value) {
    if (!value) return "-:";
    const std::string_view text(value);
    return std::to_string(text.size()) + ':' + std::string(text);
}
inline bool publishDialogue(const void *owner, ATTRIBUTES *attrs, const char *speaker) {
    if (!attrs) { closeDialogue(owner); return false; }
    auto *links = attrs->GetAttributeClass("Links");
    const auto editIndex = dialogueEditIndex(links);
    // Hidden routing metadata participates in generation changes, even if all
    // displayed text is identical. It is never accepted from the Android UI.
    std::string identity = dialogueIdentityField(attrs->GetAttribute("CurrentNode"));
    std::string body = "\"speaker\":" + dialogueJsonString(speaker) + ",\"text\":" + dialogueJsonString(attrs->GetAttribute("Text")) + ",\"choices\":[";
    bool first = true;
    if (links) for (size_t i=0; i<links->GetAttributesNum(); ++i) {
        auto *link = links->GetAttributeClass(i);
        identity += ',' + std::to_string(i) + ':';
        if (link) identity += dialogueIdentityField(link->GetAttribute("go")) +
            dialogueIdentityField(link->GetAttribute("edit"));
        if (!link || !link->GetAttribute("go") || !link->HasValue()) continue;
        if (!first) body += ',';
        first = false;
        body += "{\"index\":" + std::to_string(i) + ",\"text\":" + dialogueJsonString(link->GetValue());
        if (i == editIndex) body += ",\"editable\":true,\"maxCodePoints\":" + std::to_string(dialogueEditMaxCodePoints);
        body += '}';
    }
    body += ']';
    if (dialogueOwner == owner && dialogueSnapshot == body && dialogueIdentity == identity) return true;
    auto generation = dialogueGeneration + 1;
    if (!sendDialogue("{\"active\":true,\"generation\":" + std::to_string(generation) + ',' + body + '}')) return false;
    dialogueOwner=owner; dialogueSnapshot=body; dialogueIdentity=identity; dialogueGeneration=generation; dialogueActions.clear();
    return true;
}
inline void rejectDialogueEdit(unsigned index, std::string_view error) {
    // Always acknowledge a valid submission failure, including identical retries.
    sendDialogue("{\"active\":true,\"generation\":" + std::to_string(dialogueGeneration) + ',' +
        dialogueSnapshot + ",\"editIndex\":" + std::to_string(index) + ",\"editError\":" +
        dialogueJsonString(error) + '}');
}
template<class FitsWidth>
inline ATTRIBUTES *takeDialogueChoice(const void *owner, ATTRIBUTES *attrs, FitsWidth fitsWidth) {
    while (!dialogueActions.empty()) {
        auto request=std::move(dialogueActions.front()); dialogueActions.pop_front();
        const auto input = parseDialogueInput(request);
        if (!input || dialogueOwner != owner || input->generation != dialogueGeneration || !attrs) continue;
        auto *links=attrs->GetAttributeClass("Links");
        if (!links || input->index >= links->GetAttributesNum()) continue;
        auto *link=links->GetAttributeClass(input->index);
        if (!link || !link->GetAttribute("go") || !link->HasValue()) continue;
        const bool editable = input->index == dialogueEditIndex(links);
        if (editable != input->text.has_value()) continue;
        if (editable) {
            auto error = dialogueEditError(*input->text);
            if (error.empty() && !fitsWidth(*input->text)) error = "too_long";
            if (!error.empty()) { rejectDialogueEdit(input->index, error); continue; }
            const std::string text(*input->text);
            const char *mode = link->GetAttribute("edit");
            const auto slot = link->GetAttributeAsDword("edit", 0);
            if (!storm::iEquals(mode, "string") && slot < 10) {
                auto *data = static_cast<VDATA *>(core.GetScriptVariable("dialogEditStrings"));
                if (!data || !data->Set(text.c_str(), slot)) {
                    rejectDialogueEdit(input->index, "unavailable");
                    continue;
                }
            }
            attrs->SetAttribute("value", text);
        }
        dialogueActions.clear(); dialogueSnapshot.clear(); dialogueIdentity.clear(); return link;
    }
    return nullptr;
}
} // namespace storm::android
#endif
