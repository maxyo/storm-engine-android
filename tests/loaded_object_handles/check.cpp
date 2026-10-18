#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>
#include "data.h"
#include "entity_manager.h"
#include "message.h"
#include "s_vartab.h"
#include "utf8.h"
#include "vma.hpp"

namespace
{
void require(bool condition, const char *why)
{
    if (!condition)
        throw std::runtime_error(why);
}

class Codec final : public VSTRING_CODEC
{
    std::vector<std::string> names;
public:
    uint32_t GetNum() override { return names.size(); }
    uint32_t Convert(const char *name) override
    {
        if (!name) return 0xffffffff;
        auto found = std::find(names.begin(), names.end(), name);
        if (found != names.end()) return found - names.begin();
        names.emplace_back(name);
        return names.size() - 1;
    }
    uint32_t Convert(const char *name, int32_t length) override
    {
        const std::string value(name, length);
        return Convert(value.c_str());
    }
    const char *Convert(uint32_t code) override { return names.at(code).c_str(); }
    void VariableChanged() override {}
};

struct TraceOnly
{
    void Trace(const char *, ...) { throw std::runtime_error("unexpected reader warning"); }
} core_internal;
}

// A bounded compiler fixture: the reader methods below come verbatim from the
// selected production compiler.cpp, including recursive objects/arrays/refs.
class SaveReader final : public VIRTUAL_COMPILER
{
public:
    Codec SCodec;
    VarTable VarTab;
    std::vector<char> input;
    char *pBuffer{};
    uint32_t dwCurPointer{}, dwMaxSize{};
    SaveReader() { VarTab.SetVCompiler(this); }
    VSTRING_CODEC *GetVSC() override { return &SCodec; }
    void SetError(const char *, ...) override { throw std::runtime_error("unexpected reader error"); }
    bool ReadData(void *, uint32_t);
    uint32_t ReadVDword();
    char *ReadString();
    bool ReadVariable(char *, bool = false, uint32_t = 0);
    void ReadAttributesData(ATTRIBUTES *, ATTRIBUTES *);
    DATA &add(const char *name, S_TOKEN_TYPE type, uint32_t count = 1)
    {
        VarInfo info;
        info.name = name;
        info.type = type;
        info.elements = count;
        info.segment_id = 0;
        return *VarTab.GetVarX(VarTab.AddVar(info))->value;
    }
    void load(const char *name, std::vector<char> bytes)
    {
        input = std::move(bytes);
        pBuffer = input.data();
        dwCurPointer = 0;
        dwMaxSize = input.size();
        require(ReadVariable(const_cast<char *>(name)), "reader rejected variable");
        require(dwCurPointer == dwMaxSize, "legacy stream alignment changed");
    }
};

#include "reader.inc"

namespace
{
struct Bytes
{
    std::vector<char> data;
    template<class T> void raw(T value)
    {
        const auto *start = reinterpret_cast<const char *>(&value);
        data.insert(data.end(), start, start + sizeof(value));
    }
    void compact(uint32_t value)
    {
        if (value < 0xfe) raw<uint8_t>(value);
        else if (value < 0xffff) { raw<uint8_t>(0xfe); raw<uint16_t>(value); }
        else { raw<uint8_t>(0xff); raw(value); }
    }
    void string(const char *value)
    {
        const uint32_t size = strlen(value) + 1;
        compact(size);
        data.insert(data.end(), value, value + size);
    }
    void header(S_TOKEN_TYPE type, uint32_t count = 1) { raw(type); raw(count); }
    void object(Codec &codec, entid_t id, const char *value = "persisted")
    {
        raw(id);
        // The same legacy attribute wire format as SaveAttributesData:
        // child-count/name-code/string, followed by children recursively.
        compact(1); compact(codec.Convert("root")); string("");
        compact(1); compact(codec.Convert("savedata")); string("");
        compact(0); compact(codec.Convert("relation")); string(value);
    }
};

struct UnrelatedModel final : Entity
{
    static inline int deliveries;
    bool Init() override { return true; }
    void ProcessStage(Stage, uint32_t) override {}
    uint64_t ProcessMessage(MESSAGE &message) override
    {
        ++deliveries;
        message.Long(); // MODELR's numeric protocol must not receive group strings.
        return 0;
    }
};
CREATE_CLASS(UnrelatedModel)

struct RestoredGroups final : Entity
{
    static inline int deliveries;
    bool Init() override
    {
        auto *saved = AttributesPointer->FindAClass(AttributesPointer, "savedata.relation");
        require(saved && saved->GetValue() == "persisted", "CreateEntity lost group savedata");
        return true;
    }
    void ProcessStage(Stage, uint32_t) override {}
    uint64_t ProcessMessage(MESSAGE &message) override
    {
        require(message.String() == "SetRelation", "wrong group command");
        require(message.String() == "player", "wrong group1");
        require(message.String() == "citizens", "wrong group2");
        const std::string value = message.String();
        AttributesPointer->FindAClass(AttributesPointer, "savedata.relation")->SetValue(value);
        ++deliveries;
        return 1;
    }
};
CREATE_CLASS(RestoredGroups)

void sendRelation(EntityManager &manager, DATA &target)
{
    entid_t id;
    target.Get(id);
    if (auto *receiver = manager.GetEntityPointer(id))
    {
        MESSAGE message;
        message.Reset("ssss");
        message.Set("SetRelation"); message.Set("player");
        message.Set("citizens"); message.Set("friend");
        message.Move2Start();
        receiver->ProcessMessage(message);
    }
}

void collisionAndRebind()
{
    EntityManager oldProcess;
    const entid_t saved = oldProcess.CreateEntity("UnrelatedModel");
    oldProcess.EraseAll();
    EntityManager newProcess;
    const entid_t model = newProcess.CreateEntity("UnrelatedModel");
    require(model == saved, "fixture must reproduce cross-process handle collision");

    SaveReader compiler;
    DATA &groups = compiler.add("groups", VAR_OBJECT);
    DATA &ref = compiler.add("ref", VAR_REFERENCE);
    DATA &aref = compiler.add("aref", VAR_AREFERENCE);
    Bytes object; object.header(VAR_OBJECT); object.object(compiler.SCodec, saved);
    compiler.load("groups", object.data);
    entid_t restored;
    groups.Get(restored);
    require(newProcess.IsEntityValid(model), "restore disturbed unrelated runtime model");
    auto *savedata = groups.GetAClass()->FindAClass(groups.GetAClass(), "savedata.relation");
    require(savedata && savedata->GetValue() == "persisted", "object attributes lost");

    Bytes reference; reference.header(VAR_REFERENCE); reference.compact(0); reference.compact(0xffffffff);
    compiler.load("ref", reference.data);
    require(ref.GetReference() == &groups && ref.GetAClass() == groups.GetAClass(), "object ref lost");
    Bytes attributeRef; attributeRef.header(VAR_AREFERENCE); attributeRef.compact(0);
    attributeRef.compact(0xffffffff); attributeRef.string("savedata.relation");
    compiler.load("aref", attributeRef.data);
    require(aref.GetAClass() == savedata, "attribute ref lost");

    sendRelation(newProcess, ref);
    require(UnrelatedModel::deliveries == 0, "stale group message reached MODELR protocol");
    require(restored == invalid_entity, "saved entity handle rebound to a different process");
    // Match script FUNC_CREATE_ENTITY: create with the object's existing attrs,
    // then replace its runtime ID. Existing refs must observe the new binding.
    groups.Set(newProcess.CreateEntity("RestoredGroups", groups.GetAClass()));
    sendRelation(newProcess, ref);
    require(RestoredGroups::deliveries == 1, "explicit recreation failed to receive relation");
    require(aref.GetAClass() == savedata && savedata->GetValue() == "friend", "rebind broke savedata/aref");
    newProcess.EraseAll();
}

void arrays()
{
    SaveReader compiler;
    DATA &objects = compiler.add("objects", VAR_OBJECT, 2);
    Bytes array; array.header(VAR_OBJECT, 2);
    array.object(compiler.SCodec, 0x610000003bULL, "first");
    array.object(compiler.SCodec, invalid_entity, "second");
    compiler.load("objects", array.data);
    for (uint32_t index = 0; index < 2; ++index)
    {
        DATA *object = objects.GetArrayElement(index);
        entid_t id; object->Get(id);
        require(id == invalid_entity, "array object retained runtime handle");
        auto *attribute = object->GetAClass()->FindAClass(object->GetAClass(), "savedata.relation");
        require(attribute->GetValue() == (index ? "second" : "first"), "array attributes mixed");
    }
}
}

int main()
{
    UnrelatedModelvmaci.SetHash(MakeHashValue("UnrelatedModel"));
    RestoredGroupsvmaci.SetHash(MakeHashValue("RestoredGroups"));
    collisionAndRebind();
    arrays();
    std::cout << "PASS: actual save reader rejects cross-process handles, retains attrs/refs/arrays and permits explicit entity recreation\n";
}
