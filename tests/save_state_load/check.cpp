// Loading a script state that the program cannot take as a whole (see README.md).
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
#include "save_references.hpp"
#include "save_state.hpp"

namespace
{
void require(bool condition, const std::string &why)
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
} // namespace

// A bounded compiler fixture: the save writer methods come verbatim from compiler.cpp (run.py
// extracts them), the reader is save_state.hpp, as COMPILER::LoadState runs it.
class COMPILER final : public VIRTUAL_COMPILER
{
  public:
    Codec SCodec;
    VarTable VarTab;
    char *pBuffer{};
    uint32_t dwCurPointer{}, dwMaxSize{};
    std::vector<std::string> errors;
    const storm::save::ReferenceIndex *save_references_{};
    const char *save_variable_name_ = "";
    COMPILER()
    {
        VarTab.SetVCompiler(this);
        SCodec.Convert("code-zero");
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

    DATA &add(const char *name, S_TOKEN_TYPE type, uint32_t count = 1)
    {
        VarInfo info;
        info.name = name;
        info.type = type;
        info.elements = count;
        info.segment_id = 0;
        return *VarTab.GetVarX(VarTab.AddVar(info))->value;
    }
    // COMPILER::SaveState: program directory, name codec, segments, variables.
    std::vector<char> save()
    {
        SaveString("PROGRAM\\");
        // The name codec in its order: LoadState registers it again, which gives every name its code.
        WriteVDword(SCodec.GetNum());
        for (uint32_t n = 0; n < SCodec.GetNum(); ++n)
            SaveString(SCodec.Convert(n));
        WriteVDword(1);
        SaveString("seadogs.c");
        WriteVDword(VarTab.GetVarNum());
        const storm::save::ReferenceIndex references(VarTab);
        save_references_ = &references;
        for (size_t n = 0; n < VarTab.GetVarNum(); ++n)
        {
            const VarInfo *variable = VarTab.GetVar(n);
            SaveString(variable->name.c_str());
            save_variable_name_ = variable->name.c_str();
            SaveVariable(variable->value.get());
        }
        save_references_ = nullptr;
        return {pBuffer, pBuffer + dwCurPointer};
    }
    // CoreImpl::ProcessStateLoading and COMPILER::LoadState: the whole state parses before
    // anything is touched, then the variables are applied.
    void load(const std::vector<char> &bytes)
    {
        storm::save::ParsedState state;
        std::string error;
        const bool parsed = storm::save::parseState(bytes.data(), bytes.size(), state, error);
        require(parsed, "the state does not parse: " + error);
        require(state.segments.size() == 1 && state.segments[0] == "seadogs.c", "the segment list was misread");
        for (const auto &name : state.codec)
            SCodec.Convert(name.c_str());
        storm::save::applyVariables(bytes.data(), bytes.size(), state.variables, VarTab, &SCodec,
                                    [this](const std::string &line) { errors.push_back(line); });
    }
};

#include "methods.inc"

namespace
{
int32_t integer(DATA &value)
{
    int32_t result = 0;
    require(value.GetType() == VAR_INTEGER && value.Get(result), "not an int");
    return result;
}
std::string text(DATA &value)
{
    const char *result = nullptr;
    require(value.GetType() == VAR_STRING && value.Get(result) && result, "not a string");
    return result;
}
float real(DATA &value)
{
    float result = 0;
    require(value.GetType() == VAR_FLOAT && value.Get(result), "not a float");
    return result;
}
bool reported(const COMPILER &compiler, const char *part)
{
    return std::any_of(compiler.errors.begin(), compiler.errors.end(),
                       [&](const std::string &line) { return line.find(part) != std::string::npos; });
}

// `iMoney += 0.5` on a global int (DATA::Plus converts the variable), `sName += ...` on an int:
// the save writes the run-time type. The variables after them must load, and the drifted
// ones come back as they were saved.
void typeDrift()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &money = game.add("iMoney", VAR_INTEGER);
        DATA &label = game.add("iLabel", VAR_INTEGER);
        DATA &name = game.add("sName", VAR_STRING);
        DATA &day = game.add("iDay", VAR_INTEGER);
        money.Set(static_cast<int32_t>(100));
        DATA half;
        half.SetVCompiler(&game);
        half.Set(0.5f);
        money.Plus(&half);
        require(money.GetType() == VAR_FLOAT, "fixture: += float no longer changes the variable's type");
        label.Set("north");
        name.Set("Charles");
        day.Set(static_cast<int32_t>(7));
        image = game.save();
    }
    COMPILER loaded;
    DATA &money = loaded.add("iMoney", VAR_INTEGER);
    DATA &label = loaded.add("iLabel", VAR_INTEGER);
    DATA &name = loaded.add("sName", VAR_STRING);
    DATA &day = loaded.add("iDay", VAR_INTEGER);
    loaded.load(image);
    require(loaded.errors.empty(), "a drifted scalar was reported as lost: " + (loaded.errors.empty() ? "" : loaded.errors[0]));
    require(real(money) == 100.5f && text(label) == "north", "a drifted scalar was not restored as saved");
    require(text(name) == "Charles" && integer(day) == 7, "the variables after a drifted one were misread");
}

// A global ref declared before the array it points into, the array resized at run time
// (SetArraySize) beyond its declared size: the ref must be bound to the resized array.
void referenceBeforeResize()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &current = game.add("rCurrent", VAR_REFERENCE);
        DATA &attribute = game.add("arCurrent", VAR_AREFERENCE);
        DATA &table = game.add("Table", VAR_INTEGER, 4);
        DATA &objects = game.add("Objects", VAR_OBJECT, 4);
        table.SetElementsNum(8);
        game.VarTab.SetElementsNum(game.VarTab.FindVar("Table"), 8);
        objects.SetElementsNum(8);
        game.VarTab.SetElementsNum(game.VarTab.FindVar("Objects"), 8);
        table.GetArrayElement(6)->Set(static_cast<int32_t>(42));
        current.SetReference(table.GetArrayElement(6));
        ATTRIBUTES *root = objects.GetArrayElement(6)->GetAClass();
        root->CreateSubAClass(root, "ship.name")->SetValue("Black Pearl");
        attribute.SetAReference(root->FindAClass(root, "ship.name"));
        image = game.save();
    }
    COMPILER loaded;
    DATA &current = loaded.add("rCurrent", VAR_REFERENCE);
    DATA &attribute = loaded.add("arCurrent", VAR_AREFERENCE);
    DATA &table = loaded.add("Table", VAR_INTEGER, 4);
    DATA &objects = loaded.add("Objects", VAR_OBJECT, 4);
    loaded.load(image);
    require(loaded.errors.empty(), "the resized arrays were reported: " + (loaded.errors.empty() ? "" : loaded.errors[0]));
    require(table.GetElementsNum() == 8 && objects.GetElementsNum() == 8, "the arrays did not take their saved size");
    require(current.GetReference() == table.GetArrayElement(6), "the ref does not name the element of the resized array");
    int32_t value = 0;
    require(current.Get(value) && value == 42, "the ref reads a wrong value");
    ATTRIBUTES *root = objects.GetArrayElement(6)->GetAClass();
    require(attribute.GetAClass() == root->FindAClass(root, "ship.name") &&
                attribute.GetAClass()->GetValue() == "Black Pearl",
            "the aref does not name the attribute of the resized array's element");
}

// Saved variables the program no longer declares, of every type including arrays of refs,
// arefs and pointers (a dynamically loaded segment's globals), and variables the program
// declares differently: each is reported and left, every other variable loads.
void variablesTheProgramCannotTake()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &first = game.add("iFirst", VAR_INTEGER);
        DATA &target = game.add("Target", VAR_OBJECT);
        DATA &refs = game.add("goneRefs", VAR_REFERENCE, 3);
        DATA &arefs = game.add("goneArefs", VAR_AREFERENCE, 2);
        DATA &pointers = game.add("gonePointers", VAR_PTR, 2);
        DATA &strings = game.add("goneStrings", VAR_STRING, 2);
        DATA &object = game.add("nowInt", VAR_OBJECT);
        DATA &array = game.add("nowSingle", VAR_INTEGER, 3);
        DATA &last = game.add("sLast", VAR_STRING);
        first.Set(static_cast<int32_t>(1));
        ATTRIBUTES *root = target.GetAClass();
        root->CreateSubAClass(root, "a.b")->SetValue("kept");
        refs.GetArrayElement(1)->SetReference(&first);
        arefs.GetArrayElement(0)->SetAReference(root->FindAClass(root, "a.b"));
        pointers.GetArrayElement(1)->SetPtr(0x1234);
        strings.GetArrayElement(0)->Set("gone");
        object.GetAClass()->SetAttribute("x", "y");
        array.GetArrayElement(2)->Set(static_cast<int32_t>(5));
        last.Set("end");
        image = game.save();
    }
    COMPILER loaded;
    DATA &first = loaded.add("iFirst", VAR_INTEGER);
    DATA &target = loaded.add("Target", VAR_OBJECT);
    DATA &object = loaded.add("nowInt", VAR_INTEGER);
    DATA &array = loaded.add("nowSingle", VAR_INTEGER);
    DATA &last = loaded.add("sLast", VAR_STRING);
    object.Set(static_cast<int32_t>(-1));
    array.Set(static_cast<int32_t>(-2));
    loaded.load(image);
    for (const char *name : {"'goneRefs'", "'goneArefs'", "'gonePointers'", "'goneStrings'", "'nowInt'", "'nowSingle'"})
        require(reported(loaded, name), std::string("a variable the program cannot take was not reported: ") + name);
    require(loaded.errors.size() == 6, "unexpected load reports");
    require(integer(first) == 1 && text(last) == "end", "the variables the program can take were not all loaded");
    require(integer(object) == -1 && integer(array) == -2, "a variable the program cannot take was changed");
    ATTRIBUTES *root = target.GetAClass();
    require(root->FindAClass(root, "a.b") && root->FindAClass(root, "a.b")->GetValue() == "kept", "an object was lost");
}

// References to what the program does not have: a variable index beyond the table, an
// element beyond the array. Reported and left empty.
void referencesBeyondTheProgram()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &ref = game.add("rFar", VAR_REFERENCE);
        DATA &aref = game.add("arFar", VAR_AREFERENCE);
        DATA &table = game.add("Table", VAR_INTEGER, 2);
        DATA &objects = game.add("Objects", VAR_OBJECT, 2);
        DATA &extra = game.add("Extra", VAR_INTEGER);
        ref.SetReference(&extra);
        ATTRIBUTES *root = objects.GetArrayElement(1)->GetAClass();
        aref.SetAReference(root->CreateSubAClass(root, "deep"));
        (void)table;
        image = game.save();
    }
    COMPILER loaded;
    DATA &ref = loaded.add("rFar", VAR_REFERENCE);
    DATA &aref = loaded.add("arFar", VAR_AREFERENCE);
    loaded.add("Table", VAR_INTEGER, 2);
    loaded.add("Objects", VAR_OBJECT, 3); // the saved Objects of 2 resizes it; Extra is gone
    loaded.load(image);
    require(reported(loaded, "'Extra' is not declared") && reported(loaded, "reference 'rFar' names variable #4"),
            "a reference to a variable the program does not have was not reported");
    require(ref.GetReference() == nullptr, "a reference to nothing was bound");
    require(aref.GetAClass() != nullptr && loaded.errors.size() == 2, "the aref into the resized array was lost");
}

// An element whose type changed at run time (`Table[1] += 0.5`, `Table[2] = "x" + ...` in an int
// array): elements carry no type in the save, so it is written converted, loudly.
void arrayElementDrift()
{
    std::vector<char> image;
    std::vector<std::string> saveErrors;
    {
        COMPILER game;
        DATA &table = game.add("Table", VAR_INTEGER, 3);
        DATA &after = game.add("sAfter", VAR_STRING);
        table.GetArrayElement(0)->Set(static_cast<int32_t>(3));
        table.GetArrayElement(1)->Set(2.5f);
        table.GetArrayElement(2)->Set("17");
        after.Set("after");
        image = game.save();
        saveErrors = game.errors;
    }
    require(saveErrors.size() == 2 && saveErrors[0].find("element 1 of 'Table' is float in an array of int") != std::string::npos,
            "a drifted array element was not reported on save");
    COMPILER loaded;
    DATA &table = loaded.add("Table", VAR_INTEGER, 3);
    DATA &after = loaded.add("sAfter", VAR_STRING);
    loaded.load(image);
    require(loaded.errors.empty(), "the converted elements were reported on load");
    require(integer(*table.GetArrayElement(0)) == 3 && integer(*table.GetArrayElement(1)) == 2 &&
                integer(*table.GetArrayElement(2)) == 17,
            "the drifted elements were not saved converted to the array's type");
    require(text(after) == "after", "the variable after a drifted array was misread");
}

// An aref to an object's root attribute is saved with no path: it names the root again.
void rootAttributeReference()
{
    std::vector<char> image;
    {
        COMPILER game;
        DATA &hero = game.add("Hero", VAR_OBJECT);
        DATA &root = game.add("arHero", VAR_AREFERENCE);
        hero.GetAClass()->SetAttribute("name", "Blaze");
        root.SetAReference(hero.GetAClass());
        image = game.save();
    }
    COMPILER loaded;
    DATA &hero = loaded.add("Hero", VAR_OBJECT);
    DATA &root = loaded.add("arHero", VAR_AREFERENCE);
    loaded.load(image);
    require(root.GetAClass() == hero.GetAClass() && hero.GetAClass()->GetAttributeClass("name") && hero.GetAClass()->GetAttributeClass("name")->GetValue() == "Blaze",
            "an aref to an object's root was not restored");
}

// Damaged states are refused by parseState, before the caller erases the running game.
void damagedStates()
{
    COMPILER game;
    game.add("iFirst", VAR_INTEGER).Set(static_cast<int32_t>(1));
    DATA &hero = game.add("Hero", VAR_OBJECT);
    hero.GetAClass()->CreateSubAClass(hero.GetAClass(), "a.b.c")->SetValue("v");
    game.add("sLast", VAR_STRING).Set("end");
    const std::vector<char> image = game.save();
    storm::save::ParsedState state;
    std::string error;
    require(storm::save::parseState(image.data(), image.size(), state, error), "the intact state does not parse");
    require(state.variables.size() == 3 && state.programDirectory == "PROGRAM\\", "the intact state was misread");
    for (size_t size = 0; size < image.size(); ++size)
        require(!storm::save::parseState(image.data(), size, state, error), "a truncated state parsed");
    std::vector<char> longer = image;
    longer.push_back(0);
    require(!storm::save::parseState(longer.data(), longer.size(), state, error) && error.find("after the last variable") != std::string::npos,
            "a state with trailing bytes parsed");
    std::vector<char> unknownType = image;
    // The first variable's type follows its name.
    const char name[] = "iFirst";
    const size_t type = std::search(image.begin(), image.end(), name, name + sizeof(name)) - image.begin() + sizeof(name);
    require(unknownType[type] == VAR_INTEGER, "fixture: the type offset moved");
    unknownType[type] = 99;
    const bool parsed = storm::save::parseState(unknownType.data(), unknownType.size(), state, error);
    require(!parsed && error.find("unknown value type 99") != std::string::npos, "a state with an unknown type parsed: " + error);
}
} // namespace

int main()
{
    typeDrift();
    referenceBeforeResize();
    variablesTheProgramCannotTake();
    referencesBeyondTheProgram();
    arrayElementDrift();
    rootAttributeReference();
    damagedStates();
    std::cout << "PASS: a state is parsed whole before it is applied; drifted scalars and elements, resized arrays, "
                 "references into them and variables the program cannot take load without desynchronising the rest\n";
}
