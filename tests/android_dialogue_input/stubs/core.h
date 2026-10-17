#pragma once
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ATTRIBUTES {
    std::map<std::string, std::string> values;
    std::vector<std::unique_ptr<ATTRIBUTES>> children;
    std::string value;
    ATTRIBUTES *links = nullptr;
    bool hasValue = true;
    const char *GetAttribute(const char *name) {
        const auto it = values.find(name);
        return it == values.end() ? nullptr : it->second.c_str();
    }
    ATTRIBUTES *GetAttributeClass(const char *) { return links; }
    ATTRIBUTES *GetAttributeClass(size_t index) { return children.at(index).get(); }
    size_t GetAttributesNum() { return children.size(); }
    bool HasValue() { return hasValue; }
    const char *GetValue() { return value.c_str(); }
    uint32_t GetAttributeAsDword(const char *name, uint32_t fallback) {
        const auto text = GetAttribute(name);
        return text ? static_cast<uint32_t>(std::atol(text)) : fallback;
    }
    void SetAttribute(const char *name, std::string text) { values[name] = std::move(text); }
};
struct VDATA {
    bool writable = true;
    std::map<uint32_t, std::string> slots;
    bool Set(const char *value, uint32_t index) {
        if (!writable) return false;
        slots[index] = value;
        return true;
    }
};
struct Core {
    VDATA *variable = nullptr;
    void *GetScriptVariable(const char *) { return variable; }
};
inline Core core;
