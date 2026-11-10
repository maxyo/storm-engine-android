#pragma once

#include "s_import_func.h"
#include "s_vartab.h"
#include "string_compare.hpp"
#include <deque>
#include <unordered_map>
#include <vector>

#define INVALID_FUNC_CODE 0xffffffff
#define INVALID_FUNC_OFFSET 0xffffffff
#define INTERNAL_SEGMENT_ID 0xffffffff
#define IMPORTED_SEGMENT_ID 0xfffffffe

// when offset value of function is INVALID_FUNC_OFFSET, function segment
// isnt currently loaded and function call is impossible
// when segment_id is INTERNAL_SEGMENT_ID, this function is internal and offset
// means internal function number

struct LocalVarInfo
{
    LocalVarInfo() = default;

    bool IsArray()
    {
        return array || elements != 1; // 0: an array sized at run time
    }

    std::string name;
    size_t hash; // for faster comparison
    S_TOKEN_TYPE type;
    size_t elements;
    bool array{}; // declared with [], also "int a[1]"
};

// value of a defaulted trailing parameter: NUMBER (int), FLOAT_NUMBER, STRING or, for a ref/aref
// parameter declared "= nullptr", VAR_REFERENCE (null reference)
struct DefaultArgument
{
    S_TOKEN_TYPE type;
    int32_t int_value{};
    float float_value{};
    std::string string_value;
};

struct FuncInfo
{
    FuncInfo();

    std::string name;

    std::vector<LocalVarInfo> local_vars;

    // compiler info
    uint32_t segment_id;
    uint32_t offset;
    uint32_t stack_offset;
    uint32_t arguments;
    S_TOKEN_TYPE return_type;

    // debug info
    std::string decl_file_name;
    uint32_t decl_line;

    // profile info
    uint64_t usage_time;
    uint32_t number_of_calls;

    // imported func info
    SIMPORTFUNC imported_func;
    uint32_t extern_arguments;

    // values of the last default_args.size() arguments, filled in when a call passes fewer
    std::vector<DefaultArgument> default_args;
    // declared with "..." after its named arguments: GetArg/GetArgCount read the extra ones
    bool varargs{};
    // declared "native": implemented by an engine library (segment_id becomes IMPORTED_SEGMENT_ID
    // when a library provides it)
    bool native{};
};

// Per-call view of a function. The interpreter used to deep-copy FuncInfo
// (two strings and the local variable vector) on every script call; a frame
// only snapshots the scalar fields and points at the table entry, whose
// address is stable for the lifetime of the table (see FuncTable::funcs_).
struct FuncFrame
{
    FuncFrame() = default;
    explicit FuncFrame(const FuncInfo &fi)
        : info(&fi), segment_id(fi.segment_id), offset(fi.offset), stack_offset(fi.stack_offset),
          arguments(fi.arguments), return_type(fi.return_type)
    {
    }

    const FuncInfo *info{};
    uint32_t segment_id{INVALID_SEGMENT_ID};
    uint32_t offset{INVALID_FUNC_OFFSET};
    uint32_t stack_offset{};
    uint32_t arguments{};
    S_TOKEN_TYPE return_type{TVOID};
    // arguments passed after the declared ones to a function declared with "..." (GetArg)
    std::vector<DATA> varargs;
};

class FuncTable
{
  public:
    FuncTable() = default;
    ~FuncTable();

    // get func num
    size_t GetFuncNum() const;
    // get func index by name
    size_t FindFunc(const std::string &func_name) const;
    // add func to table, returns func index
    size_t AddFunc(const FuncInfo &fi);
    // get func by index, returns true if func is registered and loaded
    bool GetFunc(FuncInfo &fi, size_t func_index) const;
    // get func by index, returns true if func is registered
    bool GetFuncX(FuncInfo &fi, size_t func_index) const;
    // same validity rules as GetFunc, but without copying: returns the table entry
    // or nullptr. The pointer stays valid until Release(); the entry itself may be
    // updated when its segment is unloaded or reloaded.
    const FuncInfo *GetFuncPtr(size_t func_index) const;
    // the table entry whether loaded or not, nullptr for an invalid index (compiler checks)
    const FuncInfo *GetFuncInfo(size_t func_index) const;
    // defaults of the trailing arguments and "..." from the declaration
    void SetFuncSignature(size_t func_index, std::vector<DefaultArgument> default_args, bool varargs);
    // invalidate all segment's functions
    void InvalidateBySegmentID(uint32_t segment_id);

    // set func's compiler offset
    bool SetFuncOffset(const std::string &func_name, uint32_t offset);
    // add local var to func
    bool AddFuncVar(size_t func_index, const LocalVarInfo &lvi);
    // add arg to func (must precede all regular local vars in order not to break compiler logic)
    bool AddFuncArg(size_t func_index, const LocalVarInfo &lvi, bool is_extern = false);
    // get func's local var or arg index by name
    size_t FindVar(size_t func_index, const std::string &var_name) const;
    // get local var or arg by index
    bool GetVar(LocalVarInfo &lvi, size_t func_index, size_t var_index) const;

    bool AddTime(size_t func_index, uint64_t time);
    bool AddCall(size_t func_index);
    void ResetTimeAndCalls();

    void Release(); // clear table

  private:
    // deque: entries never move when functions are added, so FuncFrame::info and
    // GetFuncPtr results survive runtime segment loading
    std::deque<FuncInfo> funcs_;
    storm::iStrHasher hasher_;
    std::unordered_map<std::string, size_t, storm::iStrHasher, storm::iStrComparator> hash_table_;
};
