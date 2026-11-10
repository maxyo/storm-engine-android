// `call name()` stores 1/0 in the compiler's expression register after the callee returns. The
// callee can leave a reference or an attribute reference in that register; storing the status
// must not write through it. StoreDynamicCallStatus comes verbatim from compiler.cpp.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "data.h"

namespace
{
void require(bool condition, const char *what)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << what << '\n';
        std::exit(1);
    }
}

class Codec final : public VSTRING_CODEC
{
    std::vector<std::string> names;

  public:
    uint32_t GetNum() override { return names.size(); }
    uint32_t Convert(const char *name) override
    {
        if (!name)
            return 0xffffffff;
        auto found = std::find(names.begin(), names.end(), name);
        if (found != names.end())
            return found - names.begin();
        names.emplace_back(name);
        return names.size() - 1;
    }
    uint32_t Convert(const char *name, int32_t length) override { return Convert(std::string(name, length).c_str()); }
    const char *Convert(uint32_t code) override { return names.at(code).c_str(); }
    void VariableChanged() override {}
};

Codec codec; // outlives every DATA and ATTRIBUTES of the test

struct TraceOnly
{
    void Trace(const char *, ...) { require(false, "unexpected DATA trace"); }
} core_internal;
} // namespace

class DynamicCall final : public VIRTUAL_COMPILER
{
  public:
    DATA ExpressionResult;
    DynamicCall() { ExpressionResult.SetVCompiler(this); }
    VSTRING_CODEC *GetVSC() override { return &codec; }
    void SetError(const char *, ...) override { require(false, "unexpected script error"); }
    void StoreDynamicCallStatus(bool returned_value);
};

#include "method.inc"

namespace
{
// A character object as `ref sld = CharacterFromID("Blaze")` leaves it referenced.
void character(DynamicCall &compiler, DATA &object)
{
    object.SetVCompiler(&compiler);
    object.SetType(VAR_OBJECT);
    object.GetAClass()->SetAttribute("id", "Blaze");
}

void intact(DATA &object)
{
    require(object.GetType() == VAR_OBJECT, "referenced object changed type");
    auto *attributes = object.GetAClass();
    auto *id = attributes ? attributes->GetAttributeClass("id") : nullptr;
    require(id && id->GetValue() == "Blaze", "referenced object lost its attributes");
}

void status(DynamicCall &compiler, int32_t expected)
{
    int32_t value = -1;
    require(compiler.ExpressionResult.GetType() == VAR_INTEGER, "status is not an integer");
    compiler.ExpressionResult.Get(value);
    require(value == expected, "wrong status");
}

void reference()
{
    DynamicCall compiler;
    DATA object;
    character(compiler, object);
    compiler.ExpressionResult.SetType(VAR_REFERENCE);
    compiler.ExpressionResult.SetReference(&object);
    compiler.StoreDynamicCallStatus(true);
    intact(object);
    status(compiler, 1);
}

void attributeReference()
{
    DATA object;
    {
        DynamicCall compiler;
        character(compiler, object);
        compiler.ExpressionResult.SetType(VAR_AREFERENCE);
        compiler.ExpressionResult.SetAReference(object.GetAClass());
        compiler.StoreDynamicCallStatus(false);
        status(compiler, 0);
    } // The register must not own (and delete) the object's attributes.
    intact(object);
}
} // namespace

int main()
{
    reference();
    attributeReference();
    std::cout << "PASS dynamic call status leaves referenced objects and attributes intact\n";
}
