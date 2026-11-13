#include "attributes.h"
#include "script_state.hpp"

#include "string_compare.hpp"
#include "platform/platform.hpp"

#include <atomic>
#include <unordered_set>

namespace storm
{
namespace
{
std::atomic<uint64_t> scriptStateGeneration{0};
std::unordered_set<const ATTRIBUTES *> watchedTrees; // ScriptStateReads since the last ScriptStateForget
bool watching = false; // an adapter ran since the last ScriptStateForget: changes of watchedTrees count
std::unordered_set<const ATTRIBUTES *> *readTrees = nullptr; // an open ScriptStateReads

const ATTRIBUTES *TreeOf(const ATTRIBUTES *attribute)
{
    while (const ATTRIBUTES *parent = attribute->GetParent())
        attribute = parent;
    return attribute;
}

// An attribute of `attribute`'s tree changed.
void TreeChanged(const ATTRIBUTES *attribute)
{
    if (!watching || readTrees)
        return; // no cached facts to keep, or an adapter's own output
    if (watchedTrees.count(TreeOf(attribute)))
        ScriptStateChanged();
}

void TreeRead(const ATTRIBUTES *attribute)
{
    if (readTrees)
        readTrees->insert(TreeOf(attribute));
}
} // namespace

uint64_t ScriptStateGeneration() noexcept
{
    return scriptStateGeneration.load(std::memory_order_relaxed);
}

void ScriptStateChanged() noexcept
{
    scriptStateGeneration.fetch_add(1, std::memory_order_relaxed);
}

ScriptStateReads::ScriptStateReads()
{
    Assert(readTrees == nullptr);
    readTrees = new std::unordered_set<const ATTRIBUTES *>;
}

ScriptStateReads::~ScriptStateReads()
{
    watchedTrees.insert(readTrees->begin(), readTrees->end());
    watching = true;
    delete readTrees;
    readTrees = nullptr;
}

void ScriptStateForget()
{
    watchedTrees.clear();
    watching = false;
}
} // namespace storm

ATTRIBUTES::ATTRIBUTES(VSTRING_CODEC *p): ATTRIBUTES(*p)
{
}

ATTRIBUTES::ATTRIBUTES(ATTRIBUTES &&other) noexcept
    : stringCodec_(other.stringCodec_), nameCode_(other.stringCodec_.Convert("root")), value_(std::move(other.value_)),
      attributes_(std::move(other.attributes_)), break_(other.break_)
{
    AdoptChildren();
}

// Children moved in from another object still named it as their parent; it may be a temporary (Copy()) that is
// destroyed right after the move, and every later walk up from a child (GetParent, the script state's tree of a
// write) would read freed memory.
void ATTRIBUTES::AdoptChildren() noexcept
{
    for (const auto &attribute : attributes_)
        attribute->parent_ = this;
}

ATTRIBUTES & ATTRIBUTES::operator=(ATTRIBUTES &&other) noexcept
{
    stringCodec_ = other.stringCodec_;
    // Do not update name code
    // nameCode_ = other.nameCode_;
    value_ = std::move(other.value_);
    attributes_ = std::move(other.attributes_);
    AdoptChildren();
    // Do not update parent
    // parent_ = other.parent_;
    break_ = other.break_;
    storm::TreeChanged(this);
    return *this;
}

ATTRIBUTES::~ATTRIBUTES()
{
    // A watched tree going away is a change; its address may be reused by a tree nobody read.
    if (!parent_ && (storm::watching || storm::readTrees))
    {
        if (storm::readTrees)
            storm::readTrees->erase(this); // a script's local object: gone when the adapter ends
        if (storm::watchedTrees.erase(this) && !storm::readTrees)
            storm::ScriptStateChanged();
    }
    Release();
}

void ATTRIBUTES::SetBreak(bool set_break)
{
    break_ = set_break;
}

ATTRIBUTES *ATTRIBUTES::GetParent() const
{
    return parent_;
}

bool ATTRIBUTES::operator==(const char *str) const
{
    storm::TreeRead(this);
    if (!str || !str[0])
        return false;
    return storm::iEquals(stringCodec_.Convert(nameCode_), str);
}

const char *ATTRIBUTES::GetThisName() const
{
    storm::TreeRead(this);
    return stringCodec_.Convert(nameCode_);
}

bool ATTRIBUTES::HasValue() const noexcept
{
    storm::TreeRead(this);
    return value_.has_value();
}

const std::string & ATTRIBUTES::GetValue() const
{
    storm::TreeRead(this);
    return *value_;
}

ATTRIBUTES::LegacyProxy ATTRIBUTES::GetThisAttr() const
{
    storm::TreeRead(this);
    return value_;

}

void ATTRIBUTES::SetName(const std::string_view &new_name)
{
    nameCode_ = stringCodec_.Convert(new_name.data());
    storm::TreeChanged(this);
}

void ATTRIBUTES::SetValue(const char *new_value)
{
    if (new_value == nullptr)
    {
        if (value_)
            storm::TreeChanged(this);
        value_.reset();
    }
    else {
        if (!value_ || *value_ != new_value)
            storm::TreeChanged(this);
        value_ = new_value;
    }

    if (break_)
        stringCodec_.VariableChanged();
}

void ATTRIBUTES::SetValue(const std::string_view &new_value)
{
    if (!value_ || *value_ != new_value)
        storm::TreeChanged(this);
    value_ = new_value;

    if (break_)
        stringCodec_.VariableChanged();
}

size_t ATTRIBUTES::GetAttributesNum() const
{
    storm::TreeRead(this);
    return attributes_.size();
}

ATTRIBUTES * ATTRIBUTES::GetAttributeClass(const std::string_view &name) const
{
    storm::TreeRead(this);
    for (const auto &attribute : attributes_)
        if (storm::iEquals(name, attribute->GetThisName()))
            return attribute.get();
    return nullptr;
}

ATTRIBUTES * ATTRIBUTES::GetAttributeClass(uint32_t n) const
{
    storm::TreeRead(this);
    return n >= attributes_.size() ? nullptr : attributes_[n].get();
}

ATTRIBUTES *ATTRIBUTES::VerifyAttributeClass(const std::string_view &name)
{
    storm::TreeRead(this);
    auto *const pTemp = GetAttributeClass(name);
    return (pTemp) ? pTemp : CreateAttribute(name, "");
}

const char *ATTRIBUTES::GetAttributeName(size_t n) const
{
    storm::TreeRead(this);
    if (n < attributes_.size())
    {
        return attributes_[n]->GetThisName();
    }
    else
    {
        return nullptr;
    }
}

ATTRIBUTES::LegacyProxy ATTRIBUTES::GetAttribute(size_t n) const
{
    storm::TreeRead(this);
    if (n < attributes_.size()) {
        return attributes_[n]->value_;
    }
    else {
        return {};
    }
}


ATTRIBUTES::LegacyProxy ATTRIBUTES::GetAttribute(const std::string_view &name) const
{
    storm::TreeRead(this);
    for (const auto &attribute : attributes_)
        if (storm::iEquals(name, attribute->GetThisName())) {
            return attribute->value_;
        }
    return {};
}

uint32_t ATTRIBUTES::GetAttributeAsDword(const char *name, uint32_t def) const
{
    storm::TreeRead(this);
    uint32_t vDword = def;
    if (name)
    {
        const char *pAttribute = GetAttribute(name);
        if (pAttribute)
            vDword = atol(pAttribute);
    }
    else
    {
        vDword = atol( value_->c_str());
    }
    return vDword;
}

uintptr_t ATTRIBUTES::GetAttributeAsPointer(const char *name, uintptr_t def) const
{
    storm::TreeRead(this);
    uintptr_t ptr = def;
    if (name)
    {
        const char *pAttribute = GetAttribute(name);
        if (pAttribute)
            ptr = atoll(pAttribute);
    }
    else
    {
        ptr = atoll(value_->c_str());
    }
    return ptr;
}

float ATTRIBUTES::GetAttributeAsFloat(const char *name, float def) const
{
    storm::TreeRead(this);
    float vFloat = def;
    if (name)
    {
        if (const char *pAttribute = GetAttribute(name))
            vFloat = static_cast<float>(atof(pAttribute));
    }
    else
    {
        vFloat = static_cast<float>(atof(value_->c_str()));
    }
    return vFloat;
}

bool ATTRIBUTES::SetAttributeUseDword(const char *name, uint32_t val)
{
    std::string buffer;
    buffer = std::to_string(val);
    if (name)
        return SetAttribute(name, buffer) != 0;
    SetValue(buffer);
    return true;
}

bool ATTRIBUTES::SetAttributeUseFloat(const char *name, float val)
{
    char buffer[128];
    sprintf_s(buffer, "%g", val);
    if (name)
        return SetAttribute(name, buffer) != 0;
    SetValue(buffer);

    return true;
}

ATTRIBUTES & ATTRIBUTES::CreateAttribute(const std::string_view &name)
{
    storm::TreeChanged(this);
    const auto &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name));
    return *attr;
}

ATTRIBUTES * ATTRIBUTES::CreateAttribute(const std::string_view &name, const char *attribute)
{
    storm::TreeChanged(this);
    const auto &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name));

    if (attribute)
    {
        attr->value_ = attribute;
    }

    return attr.get();
}

size_t ATTRIBUTES::SetAttribute(const std::string_view &name, const char *attribute)
{
    return SetAttribute(stringCodec_.Convert(name.data()), attribute);
}

size_t ATTRIBUTES::SetAttribute(const std::string_view &name, const std::string_view &attribute)
{
    return SetAttribute(stringCodec_.Convert(name.data()), attribute);
}

bool ATTRIBUTES::DeleteAttributeClassX(ATTRIBUTES *pA)
{
    if (pA == nullptr)
        return false;
    if (pA == this)
    {
        if (!attributes_.empty())
            storm::TreeChanged(this);
        attributes_.clear();
    }
    else
    {
        //            auto removed_it = std::remove_if(pAttributes.begin(), pAttributes.end(), [&](const auto&
        //            item){
        //                return item.get() == pA;
        //            });
        //            return removed_it != pAttributes.end();
        for (uint32_t n = 0; n < attributes_.size(); n++)
        {
            if (attributes_[n].get() == pA)
            {
                for (auto i = n; i < attributes_.size() - 1; i++)
                    attributes_[i] = std::move(attributes_[i + 1]);

                attributes_.pop_back();
                storm::TreeChanged(this);
                return true;
            }
            if (attributes_[n]->DeleteAttributeClassX(pA))
                return true;
        }
    }
    return false;
}

ATTRIBUTES * ATTRIBUTES::CreateSubAClass(ATTRIBUTES *pRoot, const char *access_string)
{
    uint32_t dwNameCode;
    uint32_t n = 0;
    ATTRIBUTES *pResult = nullptr;
    ATTRIBUTES *pTemp;
    if (pRoot == nullptr)
        return nullptr;
    if (access_string == nullptr)
        return nullptr;

    while (true)
    {
        switch (access_string[n])
        {
        case '.':
            dwNameCode = stringCodec_.Convert(access_string, n);
            pTemp = pRoot->GetAttributeClassByCode(dwNameCode);
            if (!pTemp)
                pTemp = pRoot->CreateNewAttribute(dwNameCode);
            pResult = CreateSubAClass(pTemp, &access_string[n + 1]);
            return pResult;

        case 0:
            dwNameCode = stringCodec_.Convert(access_string);
            pResult = pRoot->GetAttributeClassByCode(dwNameCode);
            return (pResult) ? pResult : pRoot->CreateNewAttribute(dwNameCode);
        default: ;
        }
        n++;
    }
    return nullptr;
}

ATTRIBUTES * ATTRIBUTES::FindAClass(ATTRIBUTES *pRoot, const char *access_string)
{
    if (pRoot)
        storm::TreeRead(pRoot);
    uint32_t n = 0;
    ATTRIBUTES *pResult = nullptr;
    ATTRIBUTES *pTemp = nullptr;

    if (!pRoot || !access_string)
        return nullptr;
    if (!access_string[0])
        return pRoot;

    while (true)
    {
        switch (access_string[n])
        {
        case '.':
            pTemp = pRoot->GetAttributeClassByCode(stringCodec_.Convert(access_string, n));
            if (!pTemp)
                return nullptr;
            pResult = FindAClass(pTemp, &access_string[n + 1]);
            return pResult;
        case 0:
            pResult = pRoot->GetAttributeClassByCode(stringCodec_.Convert(access_string));
            return pResult;
        default: ;
        }
        n++;
    }
    return nullptr;
}

ATTRIBUTES * ATTRIBUTES::GetAttributeClassByCode(uint32_t name_code) const
{
    storm::TreeRead(this);
    for (const auto &attribute : attributes_)
        if (name_code == attribute->nameCode_)
            return attribute.get();
    return nullptr;
}

ATTRIBUTES * ATTRIBUTES::VerifyAttributeClassByCode(uint32_t name_code)
{
    storm::TreeRead(this);
    ATTRIBUTES *pTemp = GetAttributeClassByCode(name_code);
    if (pTemp)
        return pTemp;
    return CreateAttribute(name_code, "");
}

ATTRIBUTES * ATTRIBUTES::CreateAttribute(uint32_t name_code, const char *attribute)
{
    storm::TreeChanged(this);
    const auto &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name_code));

    if (attribute)
    {
        attr->value_ = attribute;
    }

    return attr.get();
}

size_t ATTRIBUTES::SetAttribute(uint32_t name_code, const char *attribute)
{
    size_t n;
    for (n = 0; n < attributes_.size(); n++)
    {
        if (attributes_[n]->nameCode_ == name_code)
        {
            auto &value = attributes_[n]->value_;
            if (attribute ? !value || *value != attribute : value.has_value())
                storm::TreeChanged(this);
            if (attribute)
            {
                value = attribute;
            }
            else
            {
                value.reset();
            }
            return n;
        }
    }

    storm::TreeChanged(this);
    auto &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name_code));

    if (attribute)
    {
        attributes_[n]->value_ = attribute;
    }
    else {
        attributes_[n]->value_.reset();
    }

    return attributes_.size() - 1;
}

size_t ATTRIBUTES::SetAttribute(uint32_t name_code, const std::string_view &attribute)
{
    size_t n;
    for (n = 0; n < attributes_.size(); n++)
    {
        if (attributes_[n]->nameCode_ == name_code)
        {
            auto &value = attributes_[n]->value_;
            if (!value || *value != attribute)
                storm::TreeChanged(this);
            value = attribute;
            return n;
        }
    }

    storm::TreeChanged(this);
    auto &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name_code));

    attributes_[n]->value_ = attribute;

    return attributes_.size() - 1;
}

uint32_t ATTRIBUTES::GetThisNameCode() const noexcept
{
    storm::TreeRead(this);
    return nameCode_;
}

void ATTRIBUTES::SetNameCode(uint32_t n) noexcept
{
    if (nameCode_ != n)
        storm::TreeChanged(this);
    nameCode_ = n;
}

VSTRING_CODEC & ATTRIBUTES::GetStringCodec() const noexcept
{
    return stringCodec_;
}

ATTRIBUTES * ATTRIBUTES::CreateNewAttribute(uint32_t name_code)
{
    storm::TreeChanged(this);
    const std::unique_ptr<ATTRIBUTES> &attr = attributes_.emplace_back(new ATTRIBUTES(stringCodec_, this, name_code));
    return attr.get();
}

ATTRIBUTES::ATTRIBUTES(VSTRING_CODEC &p) : ATTRIBUTES(p, nullptr, "root")
{
}

ATTRIBUTES::ATTRIBUTES(VSTRING_CODEC &string_codec, ATTRIBUTES *parent, const std::string_view &name)
    : ATTRIBUTES(string_codec, parent, string_codec.Convert(name.data()))
{
}

ATTRIBUTES::ATTRIBUTES(VSTRING_CODEC &string_codec, ATTRIBUTES *parent, const uint32_t name_code)
    : stringCodec_(string_codec), nameCode_(name_code), parent_(parent)
{
}

ATTRIBUTES ATTRIBUTES::Copy() const
{
    // A detached tree: no script state changes until it is assigned somewhere.
    ATTRIBUTES result(stringCodec_, nullptr, nameCode_);
    const auto copy = [](const auto &self, const ATTRIBUTES &from, ATTRIBUTES &to) -> void {
        to.value_ = from.value_;
        for (const auto &attribute : from.attributes_)
            self(self, *attribute, *to.attributes_.emplace_back(new ATTRIBUTES(from.stringCodec_, &to, attribute->nameCode_)));
    };
    copy(copy, *this, result);
    return result;
}

void ATTRIBUTES::Release() const noexcept
{
    if (break_)
        stringCodec_.VariableChanged();
}
