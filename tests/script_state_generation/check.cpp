#include "attributes.h"
#include "script_state.hpp"
#include "mobile_facts_cache.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
int failures = 0;

class Codec final : public VSTRING_CODEC
{
  public:
    uint32_t GetNum() override
    {
        return static_cast<uint32_t>(names_.size());
    }
    uint32_t Convert(const char *text) override
    {
        return Convert(text, static_cast<int32_t>(std::string(text).size()));
    }
    uint32_t Convert(const char *text, int32_t length) override
    {
        const std::string name(text, length);
        for (uint32_t i = 0; i < names_.size(); ++i)
            if (names_[i] == name)
                return i;
        names_.push_back(name);
        return static_cast<uint32_t>(names_.size() - 1);
    }
    const char *Convert(uint32_t code) override
    {
        return names_[code].c_str();
    }
    void VariableChanged() override
    {
    }

  private:
    std::vector<std::string> names_;
};

// `what` changes script state iff `changes`.
template <typename F> void Expect(bool changes, const char *what, F &&write)
{
    const auto before = storm::ScriptStateGeneration();
    write();
    const bool changed = storm::ScriptStateGeneration() != before;
    if (changed != changes)
    {
        std::fprintf(stderr, "FAIL %s: %s\n", what, changed ? "counted as a change" : "not counted");
        ++failures;
    }
}
} // namespace

int main()
{
    Codec codec;
    ATTRIBUTES root(codec);
    ATTRIBUTES *chr = nullptr;
    Expect(true, "create", [&] { chr = root.CreateSubAClass(&root, "chr.ship"); });
    Expect(true, "set a new attribute", [&] { chr->SetAttribute("type", "12"); });
    Expect(false, "set the same value", [&] { chr->SetAttribute("type", "12"); });
    Expect(true, "set a different value", [&] { chr->SetAttribute("type", "13"); });
    Expect(true, "clear a value", [&] { chr->SetAttribute("type", static_cast<const char *>(nullptr)); });
    Expect(false, "clear a cleared value", [&] { chr->SetAttribute("type", static_cast<const char *>(nullptr)); });
    Expect(true, "SetValue new", [&] { chr->SetValue(std::string_view("x")); });
    Expect(false, "SetValue same", [&] { chr->SetValue(std::string_view("x")); });
    Expect(false, "SetValue same (char *)", [&] { chr->SetValue("x"); });
    Expect(true, "SetValue nullptr", [&] { chr->SetValue(static_cast<const char *>(nullptr)); });
    Expect(true, "dword write", [&] { chr->SetAttributeUseDword("crew", 40); });
    Expect(false, "same dword", [&] { chr->SetAttributeUseDword("crew", 40); });
    Expect(true, "float write", [&] { chr->SetAttributeUseFloat("hp", 0.5f); });
    Expect(true, "rename", [&] { chr->SetName("vessel"); });
    Expect(false, "rename code to itself", [&] { chr->SetNameCode(chr->GetThisNameCode()); });
    Expect(false, "reads", [&] {
        (void)root.FindAClass(&root, "chr.vessel.crew");
        (void)chr->GetAttributeAsDword("crew");
        (void)chr->GetAttribute("hp");
        (void)root.GetAttributesNum();
        (void)root.Copy();
    });
    Expect(false, "verify an existing child", [&] { (void)chr->VerifyAttributeClass("crew"); });
    Expect(true, "verify creates a child", [&] { (void)chr->VerifyAttributeClass("morale"); });
    Expect(false, "CreateSubAClass of an existing path", [&] { (void)root.CreateSubAClass(&root, "chr.vessel.crew"); });
    Expect(true, "delete a child", [&] { root.DeleteAttributeClassX(chr->GetAttributeClass("crew")); });
    Expect(false, "delete nothing", [&] { root.DeleteAttributeClassX(nullptr); });
    Expect(true, "delete all children", [&] { chr->DeleteAttributeClassX(chr); });
    Expect(false, "delete all of nothing", [&] { chr->DeleteAttributeClassX(chr); });
    Expect(true, "move-assign a tree", [&] {
        ATTRIBUTES other(codec);
        other.SetAttribute("a", "1");
        *chr = std::move(other);
    });

    // The facts cache: the same key finds the facts, any part of the key changing recollects.
    storm::mobile::FactsCache<std::vector<std::string>> cache;
    using Key = decltype(cache)::Key;
    Key key{7, 3, "russian", storm::ScriptStateGeneration()};
    if (cache.Find(key))
        ++failures, std::fputs("FAIL an empty cache found facts\n", stderr);
    // Collecting writes the adapter's own facts; the stored generation is the one after that.
    root.SetAttribute("AndroidFacts", "rows");
    key.state = storm::ScriptStateGeneration();
    cache.Store(key, {"row"});
    const auto now = [&] { return Key{7, 3, "russian", storm::ScriptStateGeneration()}; };
    if (!cache.Find(now()) || cache.Find(now())->front() != "row")
        ++failures, std::fputs("FAIL unchanged input does not find the facts (its own writes counted)\n", stderr);
    for (const Key &other : {Key{8, 3, "russian", key.state}, Key{7, 4, "russian", key.state}, Key{7, 3, "english", key.state}})
        if (cache.Find(other))
            ++failures, std::fputs("FAIL a new opening, subject or language reused the facts\n", stderr);
    root.SetAttribute("money", "100");
    if (cache.Find(now()))
        ++failures, std::fputs("FAIL an attribute write reused the facts\n", stderr);
    if (cache.Collections() != 1)
        ++failures, std::fputs("FAIL collections counted wrong\n", stderr);

    if (failures)
        return EXIT_FAILURE;
    std::puts("PASS script state generation: changing writes count, reads and same-value writes do not; facts cache follows it");
    return EXIT_SUCCESS;
}
