// A save of script globals whose references point at freed memory (see README.md).
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include "data.h"
#include "platform/platform.hpp"
#include "s_vartab.h"
#include "utf8.h"
#ifndef STORM_SAVE_BASELINE
#include "save_references.hpp"
#endif

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
} // namespace

// A bounded compiler fixture: the save writer and reader methods below come verbatim from
// compiler.cpp (run.py extracts them); it supplies the variable table, codec and buffer.
class COMPILER final : public VIRTUAL_COMPILER
{
  public:
    Codec SCodec;
    VarTable VarTab;
    char *pBuffer{};
    uint32_t dwCurPointer{}, dwMaxSize{};
    std::vector<std::string> errors;
#ifndef STORM_SAVE_BASELINE
    const storm::save::ReferenceIndex *save_references_{};
    const char *save_variable_name_ = "";
#endif
    COMPILER()
    {
        VarTab.SetVCompiler(this);
        SCodec.Convert("code-zero"); // an attribute of name code 0 is saved as no reference
    }
    ~COMPILER() { delete[] pBuffer; }
    VSTRING_CODEC *GetVSC() override { return &SCodec; }
    void SetError(const char *format, ...) override
    {
        char text[512];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        errors.emplace_back(text);
    }
    void SaveData(const void *, uint32_t);
    void SaveString(const char *);
    void WriteVDword(uint32_t);
    void SaveVariable(DATA *, bool = false);
    void SaveAttributesData(ATTRIBUTES *);
#ifdef STORM_SAVE_BASELINE
    bool FindReferencedVariable(DATA *, uint32_t &, uint32_t &);
    bool FindReferencedVariableByRootA(ATTRIBUTES *, uint32_t &, uint32_t &);
    ATTRIBUTES *TraceARoot(ATTRIBUTES *, const char *&);
#endif
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
    // The variables part of COMPILER::SaveState.
    std::vector<char> save()
    {
#ifndef STORM_SAVE_BASELINE
        const storm::save::ReferenceIndex references(VarTab);
        save_references_ = &references;
#endif
        for (size_t n = 0; n < VarTab.GetVarNum(); ++n)
        {
            const VarInfo *variable = VarTab.GetVar(n);
            SaveString(variable->name.c_str());
#ifndef STORM_SAVE_BASELINE
            save_variable_name_ = variable->name.c_str();
#endif
            SaveVariable(variable->value.get());
        }
        return {pBuffer, pBuffer + dwCurPointer};
    }
    void load(std::vector<char> bytes)
    {
        delete[] pBuffer;
        pBuffer = new char[bytes.size()];
        memcpy(pBuffer, bytes.data(), bytes.size());
        dwCurPointer = 0;
        dwMaxSize = bytes.size();
        for (size_t n = 0; n < VarTab.GetVarNum(); ++n)
        {
            char *name = ReadString();
            const bool read = name && ReadVariable(name);
            delete[] name;
            require(read, "the reader rejected a saved variable");
        }
        require(dwCurPointer == dwMaxSize, "the saved stream has a different length than the reader consumed");
    }
};

#include "methods.inc"

namespace
{
struct Game
{
    COMPILER compiler;
    DATA &locations = compiler.add("Locations", VAR_OBJECT, 3);
    DATA &hero = compiler.add("Hero", VAR_OBJECT);
    DATA &counter = compiler.add("counter", VAR_INTEGER);
    DATA &reloadLocator = compiler.add("reload_locator_ref", VAR_AREFERENCE);
    DATA &cannons = compiler.add("hero_cannons", VAR_AREFERENCE);
    DATA &heroRoot = compiler.add("hero_root", VAR_AREFERENCE);
    DATA &zeroNamed = compiler.add("zero_named", VAR_AREFERENCE);
    DATA &localAttribute = compiler.add("local_attribute", VAR_AREFERENCE);
    DATA &unset = compiler.add("unset", VAR_AREFERENCE);
    DATA &arefs = compiler.add("locators", VAR_AREFERENCE, 2);
    DATA &current = compiler.add("current", VAR_REFERENCE);
    DATA &chain = compiler.add("chain", VAR_REFERENCE);
    DATA &freed = compiler.add("freed", VAR_REFERENCE);
    DATA &empty = compiler.add("empty", VAR_REFERENCE);
};

ATTRIBUTES *at(DATA &object, const char *path)
{
    return object.GetAClass()->CreateSubAClass(object.GetAClass(), path);
}

// The owner's crash: reload.c keeps the last reload locator in the global `aref
// reload_locator_ref`; a script later deletes or rebuilds that location's `reload` branch
// (DeleteAttribute(loc, "reload...")). The next save meets the freed attribute.
void danglingReferences()
{
    Game game;
    game.counter.Set(static_cast<int32_t>(42));
    DATA &town = *game.locations.GetArrayElement(1);
    at(town, "reload.l1.name")->SetValue("reload1");
    at(town, "reload.l1.go")->SetValue("Town_tavern");
    at(town, "id")->SetValue("Town");
    at(game.hero, "ship.cannons")->SetValue("16");
    at(game.hero, "code-zero.inner")->SetValue("x");

    game.reloadLocator.SetAReference(at(town, "reload.l1"));
    game.arefs.GetArrayElement(0)->SetAReference(at(town, "reload.l1.go"));
    game.arefs.GetArrayElement(1)->SetAReference(at(town, "id"));
    game.cannons.SetAReference(at(game.hero, "ship.cannons"));
    game.heroRoot.SetAReference(game.hero.GetAClass());
    game.zeroNamed.SetAReference(at(game.hero, "code-zero.inner"));
    ATTRIBUTES local(game.compiler.SCodec); // a local object's attributes: alive, not saved
    game.localAttribute.SetAReference(local.CreateSubAClass(&local, "some.where"));
    game.current.SetReference(&town);
    game.chain.SetReference(&game.current);
    auto *temporary = new DATA(VAR_OBJECT); // e.g. a global of a segment that was reloaded
    game.freed.SetReference(temporary);

    // DeleteAttribute(&Locations[1], "reload"), then the allocator hands the memory on.
    require(town.GetAClass()->DeleteAttributeClassX(town.GetAClass()->GetAttributeClass("reload")), "fixture: delete failed");
    delete temporary;
    std::vector<std::string> reuse(64, std::string(96, '\xd0'));

    const std::vector<char> image = game.compiler.save();

    const auto reported = [&](const char *kind, const char *name) {
        return std::count_if(game.compiler.errors.begin(), game.compiler.errors.end(), [&](const std::string &line) {
            return line.rfind(kind, 0) == 0 && line.find(std::string("'") + name + "'") != std::string::npos;
        });
    };
    require(reported("Ghost A reference", "reload_locator_ref") == 1, "the deleted reload locator was not reported");
    require(reported("Ghost A reference", "locators") == 1, "the deleted array aref target was not reported");
    require(reported("Ghost A reference", "local_attribute") == 1, "an aref to a local object was not reported");
    require(reported("Ghost reference", "freed") == 1, "the freed ref target was not reported");
    require(game.compiler.errors.size() == 4, "a valid reference was reported as a ghost");

    Game loaded;
    loaded.compiler.SCodec = game.compiler.SCodec; // SaveState stores the name table in front of the variables
    at(*loaded.locations.GetArrayElement(1), "stale")->SetValue("must go");
    loaded.compiler.load(image);
    require(loaded.compiler.errors.empty(), "loading the save reported errors");
    int32_t counter = 0;
    require(loaded.counter.Get(counter) && counter == 42, "an integer was lost");
    DATA &loadedTown = *loaded.locations.GetArrayElement(1);
    require(loadedTown.GetAClass()->GetAttributeClass("reload") == nullptr, "the deleted branch came back");
    require(loaded.reloadLocator.AttributesClass == nullptr, "the dangling aref was not saved empty");
    require(loaded.arefs.GetArrayElement(0)->AttributesClass == nullptr, "the dangling array aref was not saved empty");
    require(loaded.arefs.GetArrayElement(1)->AttributesClass == at(loadedTown, "id") &&
                at(loadedTown, "id")->GetValue() == "Town", "a live aref of an array element was lost");
    require(loaded.cannons.AttributesClass == at(loaded.hero, "ship.cannons") &&
                loaded.cannons.AttributesClass->GetValue() == "16", "a live aref was lost");
    require(loaded.localAttribute.AttributesClass == nullptr && loaded.unset.AttributesClass == nullptr &&
                loaded.zeroNamed.AttributesClass == nullptr, "an unsaveable aref was saved as something");
    require(loaded.current.GetReference() == &loadedTown, "a ref to an array element was lost");
    require(loaded.chain.GetReference() == &loadedTown, "a ref through a ref no longer names the final variable");
    require(loaded.freed.GetReference() == nullptr && loaded.empty.GetReference() == nullptr, "a ghost ref was saved as something");
}

#ifndef STORM_SAVE_BASELINE
void referenceChains()
{
    Game game;
    require(!storm::save::ReferenceIndex(game.compiler.VarTab).Variable(&game.empty), "an empty ref resolved");
    game.current.SetReference(&game.current);
    auto self = storm::save::ReferenceIndex(game.compiler.VarTab).Variable(&game.current);
    require(self && self->variable == game.compiler.VarTab.FindVar("current") && self->element == storm::save::kNoElement,
            "a ref to itself does not name itself");
    game.current.SetReference(&game.chain);
    game.chain.SetReference(&game.current);
    require(!storm::save::ReferenceIndex(game.compiler.VarTab).Variable(&game.current), "a reference cycle resolved");
    game.chain.SetReference(&game.empty);
    require(!storm::save::ReferenceIndex(game.compiler.VarTab).Variable(&game.current), "a chain into an empty ref resolved");
    game.heroRoot.SetAReference(game.hero.GetAClass());
    const storm::save::ReferenceIndex index(game.compiler.VarTab);
    const auto *root = index.Attribute(game.hero.GetAClass());
    require(root && root->path.empty() && root->owner.variable == game.compiler.VarTab.FindVar("Hero"), "an aref to an object's root is not found");
}
#endif
} // namespace

int main()
{
    danglingReferences();
#ifndef STORM_SAVE_BASELINE
    referenceChains();
#endif
    std::cout << "PASS: a save with refs and arefs to freed memory is written without touching it, reports each one, "
                 "and loads with every live reference intact\n";
}
