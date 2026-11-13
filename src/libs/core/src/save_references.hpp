#pragma once

// Where the script references of a save point.
//
// A save stores a `ref` as the global variable (and array element) it names, and an `aref`
// as the global object variable that owns the attribute plus the attribute's path. The
// script language does not track references: deleting an attribute (DeleteAttribute, a
// rebuilt branch), resizing an array or reloading a segment leaves every `ref` / `aref` to it
// pointing at freed memory. A save visits every global variable, so it meets those pointers
// even when no script would use them again.
//
// The pointers are therefore never followed. Before the variables are written, the index
// records the address of every global variable and array element, and looks for the target
// of every global `aref` among the attributes that global object variables own. A pointer
// found there is alive (or names the live variable or attribute that now occupies that
// address, which is also what a script reading the reference would get); a pointer not found
// is a reference to nothing that can be saved and is written as empty, with an error line.
//
// The original engine walked up from the attribute through its parents instead
// (TraceARoot) and relied on Windows structured exceptions for the dangling case. Elsewhere
// that walk is a call through freed memory: the save crashed (Android, SIGSEGV in
// COMPILER::SaveVariable).

#include "attributes.h"
#include "s_vartab.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

namespace storm::save
{
inline constexpr uint32_t kNoElement = 0xffffffff;

// A global variable, or one element of a global array.
struct VariablePlace
{
    uint32_t variable = 0;
    uint32_t element = kNoElement;
};

// An attribute of a global object variable.
struct AttributePlace
{
    VariablePlace owner;
    std::string path; // "a.b.c" below the object's root attribute; empty for the root itself
    // An attribute on the way has name code 0. The save format has always written such a
    // reference as empty (TraceARoot, "fix crash at NewGame start"); files stay compatible.
    bool nameless = false;
};

class ReferenceIndex
{
  public:
    explicit ReferenceIndex(const VarTable &variables)
    {
        const auto count = static_cast<uint32_t>(variables.GetVarNum());
        for (uint32_t n = 0; n < count; ++n)
        {
            const VarInfo *info = variables.GetVarX(n);
            if (info == nullptr || info->value == nullptr)
                continue;
            DATA *value = info->value.get();
            if (!value->IsArray())
            {
                Add(value, VariablePlace{n, kNoElement});
                continue;
            }
            const auto elements = static_cast<uint32_t>(std::min<size_t>(info->elements, value->GetElementsNum()));
            for (uint32_t i = 0; i < elements; ++i)
                Add(value->GetArrayElement(i), VariablePlace{n, i});
        }

        // Only attributes of global object variables can be saved as a reference target.
        unresolved_ = attributes_.size();
        for (uint32_t n = 0; n < count && unresolved_ != 0; ++n)
        {
            const VarInfo *info = variables.GetVarX(n);
            if (info == nullptr || info->value == nullptr || info->type != VAR_OBJECT)
                continue;
            DATA *value = info->value.get();
            if (!value->IsArray())
            {
                Visit(value->AttributesClass, VariablePlace{n, kNoElement});
                continue;
            }
            const auto elements = static_cast<uint32_t>(std::min<size_t>(info->elements, value->GetElementsNum()));
            for (uint32_t i = 0; i < elements && unresolved_ != 0; ++i)
                Visit(value->GetArrayElement(i)->AttributesClass, VariablePlace{n, i});
        }
    }

    // The variable the reference chain starting at the `ref` variable `reference` ends in;
    // nothing when the chain is empty or leaves the global variables (a ghost reference).
    // `reference` itself must be alive; nothing else is assumed about the chain.
    std::optional<VariablePlace> Variable(DATA *reference) const
    {
        DATA *current = reference;
        // A chain longer than the number of variables is a cycle.
        for (size_t step = 0; step <= variables_.size(); ++step)
        {
            DATA *next = current->GetReference();
            if (next == nullptr)
                return {};
            const auto found = variables_.find(next);
            if (next == current) // a reference to itself names itself
                return found == variables_.end() ? std::nullopt : std::optional(found->second);
            if (found == variables_.end())
                return {};
            if (next->GetType() != VAR_REFERENCE)
                return found->second;
            current = next;
        }
        return {};
    }

    // Where the target of an `aref` is; nullptr when no global object variable owns it.
    const AttributePlace *Attribute(const ATTRIBUTES *target) const
    {
        const auto found = attributes_.find(target);
        return found == attributes_.end() || !found->second ? nullptr : &*found->second;
    }

  private:
    void Add(DATA *value, VariablePlace place)
    {
        variables_.try_emplace(value, place);
        if (value->GetType() == VAR_AREFERENCE && value->AttributesClass != nullptr)
            attributes_.try_emplace(value->AttributesClass, std::nullopt);
    }

    void Visit(const ATTRIBUTES *root, VariablePlace owner)
    {
        if (root == nullptr)
            return;
        std::string path;
        Visit(root, owner, path, false, true);
    }

    void Visit(const ATTRIBUTES *node, VariablePlace owner, std::string &path, bool nameless, bool is_root)
    {
        const size_t length = path.size();
        if (!is_root)
        {
            nameless = nameless || node->GetThisNameCode() == 0;
            if (!path.empty())
                path += '.';
            path += node->GetThisName();
        }
        if (const auto found = attributes_.find(node); found != attributes_.end() && !found->second)
        {
            found->second = AttributePlace{owner, path, nameless};
            --unresolved_;
        }
        const auto children = static_cast<uint32_t>(node->GetAttributesNum());
        for (uint32_t n = 0; n < children && unresolved_ != 0; ++n)
            Visit(node->GetAttributeClass(n), owner, path, nameless, false);
        path.resize(length);
    }

    std::unordered_map<const DATA *, VariablePlace> variables_;
    // The targets of the global `aref` variables; a value once a global object owns the address.
    std::unordered_map<const ATTRIBUTES *, std::optional<AttributePlace>> attributes_;
    size_t unresolved_ = 0;
};
} // namespace storm::save
