#pragma once

//-------------------------------------------------------------------------------------------------
// Simple class for creating and keeping in memory strings list
// Also, each string can has its own data. Amount of memory for this data you can assign
// using SetStringDataSize(..) function. When you changing the size for this data, all items in list
// cleared. This data can be set or retrivied

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#define INVALID_ORDINAL_NUMBER 0xffffffff
#define CACHE_SIZE 8

class STRINGS_LIST
{
  protected:
    // Each record remains separately allocated: GetString pointers survive table
    // growth and erasing other entries. Record bytes retain the legacy layout:
    // [uint32_t hash][used_data_size payload bytes][NUL-terminated text].
    std::vector<std::unique_ptr<char[]>> strings_;
    uint32_t used_data_size;
    std::array<uint32_t, CACHE_SIZE> Cache;
    uint32_t Cache_Pos;

  public:
    STRINGS_LIST();
    ~STRINGS_LIST();
    STRINGS_LIST(const STRINGS_LIST &) = delete;
    STRINGS_LIST &operator=(const STRINGS_LIST &) = delete;

    uint32_t GetStringsCount();
    char *GetString(uint32_t code);
    uint32_t GetStringCode(const char *_char_PTR);
    bool AddString(const char *_char_PTR);
    bool AddUnicalString(const char *_char_PTR);
    void Release();
    void DeleteString(uint32_t code);

    bool GetStringData(uint32_t code, void *data_PTR);
    bool SetStringData(uint32_t code, void *data_PTR);
    void SetStringDataSize(uint32_t size);

    void CacheString(uint32_t code);
    uint32_t MakeHashValue(const char *string);
};
