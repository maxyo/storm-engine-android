#include "strings_list.h"

#include <cstring>
#include <stdexcept>

#include "string_compare.hpp"

STRINGS_LIST::STRINGS_LIST()
{
    used_data_size = 0;
    for (uint32_t n = 0; n < CACHE_SIZE; n++)
        Cache[n] = INVALID_ORDINAL_NUMBER;
    Cache_Pos = 0;
}

STRINGS_LIST::~STRINGS_LIST()
{
    Release();
}

void STRINGS_LIST::CacheString(uint32_t code)
{
    if (Cache_Pos >= CACHE_SIZE)
        Cache_Pos = 0;
    Cache[Cache_Pos] = code;
    Cache_Pos++;
}

uint32_t STRINGS_LIST::GetStringsCount()
{
    return static_cast<uint32_t>(strings_.size());
}

char *STRINGS_LIST::GetString(uint32_t code)
{
    if (code >= strings_.size())
        return nullptr;
    return strings_[code].get() + used_data_size + sizeof(uint32_t);
}

bool STRINGS_LIST::AddString(const char *_char_PTR)
{
    // GUARD(STRINGS_LIST::AddString)
    if (_char_PTR == nullptr)
        throw std::runtime_error("zero string");
    const auto hash = MakeHashValue(_char_PTR);
    const auto len = strlen(_char_PTR) + 1;
    auto record = std::make_unique<char[]>(len + used_data_size + sizeof(uint32_t));
    memcpy(record.get(), &hash, sizeof(hash));
    memcpy(record.get() + used_data_size + sizeof(uint32_t), _char_PTR, len);
    strings_.push_back(std::move(record));
    // UNGUARD
    return true;
}

void STRINGS_LIST::Release()
{
    decltype(strings_){}.swap(strings_);
    Cache.fill(INVALID_ORDINAL_NUMBER);
    Cache_Pos = 0;
}

uint32_t STRINGS_LIST::GetStringCode(const char *_char_PTR)
{
    if (_char_PTR == nullptr)
        return INVALID_ORDINAL_NUMBER;
    const auto hash = MakeHashValue(_char_PTR);
    for (uint32_t n = 0; n < strings_.size(); n++)
    {
        uint32_t storedHash;
        memcpy(&storedHash, strings_[n].get(), sizeof(storedHash));
        if (hash == storedHash && storm::iEquals(GetString(n), _char_PTR))
            return n;
    }
    return INVALID_ORDINAL_NUMBER;
}

bool STRINGS_LIST::AddUnicalString(const char *_char_PTR)
{
    if (GetStringCode(_char_PTR) != INVALID_ORDINAL_NUMBER)
        return false;
    return AddString(_char_PTR);
}

bool STRINGS_LIST::GetStringData(uint32_t code, void *data_PTR)
{
    if (code >= strings_.size() || data_PTR == nullptr)
        return false;
    memcpy(data_PTR, strings_[code].get() + sizeof(uint32_t), used_data_size);
    return true;
}

bool STRINGS_LIST::SetStringData(uint32_t code, void *data_PTR)
{
    // GUARD(STRINGS_LIST::SetStringData)
    if (code >= strings_.size() || data_PTR == nullptr)
        return false;
    memcpy(strings_[code].get() + sizeof(uint32_t), data_PTR, used_data_size);
    // UNGUARD
    return true;
}

void STRINGS_LIST::SetStringDataSize(uint32_t _size)
{
    if (used_data_size == _size)
        return;
    Release();
    used_data_size = _size;
}

void STRINGS_LIST::DeleteString(uint32_t code)
{
    if (code >= strings_.size())
        return;
    // Stable erase preserves the compiler's ordinal file/jump-table ordering.
    strings_.erase(strings_.begin() + code);
    Cache.fill(INVALID_ORDINAL_NUMBER);
    Cache_Pos = 0;
}

uint32_t STRINGS_LIST::MakeHashValue(const char *string)
{
    uint32_t hval = 0;

    while (*string != 0)
    {
        auto v = *string++;
        if ('A' <= v && v <= 'Z')
            v += 'a' - 'A';

        hval = (hval << 4) + static_cast<uint32_t>(v);
        const uint32_t g = hval & (static_cast<uint32_t>(0xf) << (32 - 4));
        if (g != 0)
        {
            hval ^= g >> (32 - 8);
            hval ^= g;
        }
    }
    return hval;
}
