#pragma once

#include <set>
#include <string_view>
#include <tuple>

#include "data.h"
#include "message.h"
#include "s_deftab.h"
#include "s_eventmsg.h"
#include "s_eventtab.h"
#include "s_functab.h"
#include "s_postevents.h"
#include "s_stack.h"
#include "s_vartab.h"
#include "script_libriary.h"
#include "string_codec.h"
#include "strings_list.h"
#include "token.h"
#include "logging.hpp"
#include "script_cache.h"
#include "platform/platform.hpp"

#include "ringbuffer_stack.hpp"

#define BCODE_BUFFER_BLOCKSIZE 4096
#define IOBUFFER_SIZE 65535

struct SEGMENT_DESC
{
    std::string name;
    uint32_t offset;
    uint32_t size;
    uint32_t id;
    uint32_t lines;
    bool bUnload;
    //--------------
    char *pData;
    char *pCode;
    uint32_t BCode_Program_size;
    uint32_t BCode_Buffer_size;

    STRINGS_LIST *Files_list;
};

struct OFFSET_INFO
{
    uint32_t offset;
    uint32_t segment_id;
};

/*struct DEFINE_INFO
{
    uint32_t deftype;
    uint32_t data4b;
};*/

struct DOUBLE_DWORD
{
    uint32_t dw1;
    uint32_t dw2;
};

class SLIBHOLDER
{
  public:
    std::unique_ptr<SCRIPT_LIBRIARY> library;
    std::string name;

    SLIBHOLDER(SCRIPT_LIBRIARY *library, std::string name)
        : library(library), name(std::move(name))
    {
    }
};

class S_DEBUG;

enum COMPILER_STAGE
{
    CS_SYSTEM,
    CS_COMPILATION,
    CS_RUNTIME
};

class CoreImpl;

class COMPILER : public VIRTUAL_COMPILER
{
    friend CoreImpl;
    friend S_DEBUG;

  public:
    bool bBreakOnError;

    COMPILER();
    ~COMPILER();

    VSTRING_CODEC *GetVSC()
    {
        return &SCodec;
    }

    char *LoadFile(const char *file_name, uint32_t &file_size, bool bFullPath = false);
    // char *    AppendProgram(char * base_program, int32_t base_program_size, char * append_program, int32_t
    // append_program_size, int32_t& new_program_size);
    bool AppendProgram(char *&pBase_program, uint32_t &Base_program_size, const char *pAppend_program,
                       uint32_t &Append_program_size, bool bAddLinefeed);
    void Trace(const char *data_PTR, ...);
    void DTrace(const char *data_PTR, ...);
    void SetError(const char *data_PTR, ...);
    void SetWarning(const char *data_PTR, ...);
    bool CreateProgram(const char *file_name);
    bool AddProgramFile(const char *file_name);

    void UnloadSegment(const char *segment_name);
    uint32_t GetSegmentIndex(uint32_t segment_id);

    void ProcessFrame(uint32_t DeltaTime);
    bool Run();
    void Release();
    void SetProgramDirectory(const char *dir_name);
    VDATA *ProcessEvent(const char *event_name, MESSAGE message);
    VDATA *ProcessEvent(const char *event_name);
    void SetEventHandler(const char *event_name, const char *func_name, int32_t flag, bool bStatic = false);
    void DelEventHandler(const char *event_name, const char *func_name);

    bool Completed()
    {
        return bCompleted;
    }

    void ExitProgram();
    void ClearEvents();

    uint32_t GetIntFunctionCode(const char *func_name);
    void DeleteScriptEntity(DATA &object);
    DATA *BC_CallIntFunction(uint32_t func_code, DATA *&pVResult, uint32_t arguments);

    uint32_t GetIntFunctionsNum();
    bool InitInternalFunctions();

    bool ProcessDebugExpression(const char *pExpression, DATA &Result);
    bool SetOnDebugExpression(const char *pLValue, const char *pRValue, DATA &Result);
    bool ProcessDebugExpression0(const char *pExpression, DATA &Result);
    bool Compile(SEGMENT_DESC &Segment, char *pInternalCode = nullptr, uint32_t pInternalCodeSize = 0);
    void CompileToken(SEGMENT_DESC &Segment, S_TOKEN_TYPE Token_type, uint32_t data_blocks_num = 0, ...);
    void ResizeBCodeBuffer(SEGMENT_DESC &Segment, uint32_t add_size);
    bool BC_LoadSegment(const char *file_name);
    bool BC_SegmentIsLoaded(const char *file_name);
    // passed_args: number of arguments the caller pushed (kArgsAsDeclared: as many as declared)
    static constexpr uint32_t kArgsAsDeclared = 0xffffffff;
    bool BC_Execute(uint32_t function_code, DATA *&pVReturnResult, const char *pDbgExpSource = nullptr,
                    uint32_t passed_args = kArgsAsDeclared);
    bool BindPassedArguments(const FuncInfo &fi, FuncFrame &frame, uint32_t passed_args);
    bool BC_CallFunction(uint32_t func_code, uint32_t &ip, DATA *&pVResult);
    void StoreDynamicCallStatus(bool returned_value);
    void FindErrorSource();
    S_TOKEN_TYPE BC_TokenGet(uint32_t &ip, uint32_t &token_data_size);
    S_TOKEN_TYPE BC_TokenGet();

    S_TOKEN_TYPE TokenLastReadResult;
    uint32_t TLR_DataOffset;
    const char *pRunCodeBase;
    bool TokenIs(S_TOKEN_TYPE test);

    S_TOKEN_TYPE TokenType()
    {
        return TokenLastReadResult;
    }

    S_TOKEN_TYPE NextTokenType();
    S_TOKEN_TYPE SkipAuxiliaryTokens();
    bool SkipToTopLevel(uint32_t depth);
    static bool IsIdentifier(const char *text);
    static std::string LowerName(const std::string &name);
    static std::string LocalSlotName(const std::string &name, uint32_t occurrence);
    static std::string LabelKey(const std::string &func_name, const char *label);
    static constexpr uint32_t kRuntimeArraySize = 0; // elements of an array sized at run time
    uint32_t ScanLocalArraySize();
    uint32_t ConstantArraySize();
    uint32_t SkipInitializer(bool &resumed);
    // pass 2: block scoped locals
    uint32_t FindLocal(const char *name) const;
    bool AssignValue(DATA *destination, DATA *value);
    int32_t IntValue(DATA *pV);
    static DATA *Deref(DATA *pV);
    static ATTRIBUTES *AttributesOf(DATA *pV);
    int32_t TypeidOf(DATA *pV);
    bool CastValue(DATA *pV, S_TOKEN_TYPE target);
    static bool ReturnTypesMatch(S_TOKEN_TYPE declared, S_TOKEN_TYPE engine);
    const std::string &CurrentFunctionName() const;
    // script function name of a CALL_FUNCTION code (codes depend on the order functions were seen)
    const char *GetFunctionName(uint32_t code) const
    {
        const auto *fi = FuncTab.GetFuncInfo(code);
        return fi ? fi->name.c_str() : "?";
    }
    uint32_t BindLocal(const char *name, uint32_t depth);
    bool CompileLocalDeclarator(SEGMENT_DESC &Segment, uint32_t depth, bool first);
    bool CompileArrayInitializer(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code);
    void CloseLocalScopes(uint32_t depth);
    bool ScanParameterList(uint32_t func_code, bool bExtern, FuncInfo &declared);
    bool ScanDefaultArgument(S_TOKEN_TYPE param_type, DefaultArgument &value);
    static int32_t ParseIntLiteral(const char *text);
    bool CompileCallArguments(SEGMENT_DESC &Segment, uint32_t &func_args);
    // in_expression: a count mismatch only warns, as calls inside expressions were never checked
    // (TEHO has a few) and still compile as before
    bool CheckCallArguments(uint32_t func_code, uint32_t func_args, bool in_expression = false);
    bool ReportMissingNative(const std::string &name);

    bool BC_ProcessExpression(DATA *value);
    void BC_ProcessExpression_L0(DATA *value);
    void BC_ProcessExpression_L1(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L2(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L3(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L4(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L5(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L6(DATA *value, bool bSkip = false);
    void BC_ProcessExpression_L7(DATA *value, bool bSkip = false);

    /*    void SkipExpression_L1();
        void SkipExpression_L2();
        void SkipExpression_L3();
        void SkipExpression_L4();
        void SkipExpression_L5();
        void SkipExpression_L6();
        void SkipExpression_L7();
    */
    //-----------------------------------------------------
    uint32_t CurrentFuncCode;
    void CompileNumber(SEGMENT_DESC &Segment);
    void CompileFloatNumber(SEGMENT_DESC &Segment);
    void CompileString(SEGMENT_DESC &Segment);
    bool CompileUnknown(SEGMENT_DESC &Segment);
    bool CompileBlock(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE bound_type,
                      uint32_t continue_jump, uint32_t break_offset, STRINGS_LIST &BreakTable);
    S_TOKEN_TYPE CompileAuxiliaryTokens(SEGMENT_DESC &Segment); //, bool & bFunctionBlock, DWORD & inout);
    bool BC_Jump(SEGMENT_DESC &Segment, uint32_t offset);
    void UpdateOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &list, uint32_t offset, const char *sname = nullptr);
    S_TOKEN_TYPE DetectUnknown(uint32_t &code);

    void DumpAttributes(ATTRIBUTES *pA, int32_t level);

    bool IsIntFuncVarArgsNum(uint32_t code);
    S_TOKEN_TYPE GetIntFunctionReturnType(uint32_t code);
    bool IntFuncArgumentsInRange(uint32_t code, uint32_t arguments);
    uint32_t GetInternalFunctionArgumentsNum(uint32_t code);
    bool CreateMessage(MESSAGE *pMs, uint32_t stack_offset, uint32_t vindex, bool s2s = false);
    void ProcessEvent(const char *event_name, MESSAGE *pMs);

    bool SaveState(std::vector<char> &image);
    bool LoadState(const std::vector<char> &raw);
    bool OnLoad();
    void SaveDataDebug(char *data_PTR, ...);
    void SaveData(const void *data_PTR, uint32_t data_size);
    bool ReadData(void *data_PTR, uint32_t data_size);
    void SaveString(const char *pS);
    char *ReadString();

    void SaveVariable(DATA *pV, bool bdim = false);
    bool ReadVariable(char *name, /*DWORD code,*/ bool bdim = false, uint32_t a_index = 0);
    bool FindReferencedVariable(DATA *pRef, uint32_t &var_index, uint32_t &array_index);
    bool FindReferencedVariableByRootA(ATTRIBUTES *pA, uint32_t &var_index, uint32_t &array_index);
    ATTRIBUTES *TraceARoot(ATTRIBUTES *pA, const char *&pAccess);
    void SaveAttributesData(ATTRIBUTES *pRoot);
    void ReadAttributesData(ATTRIBUTES *pRoot, ATTRIBUTES *pParent);
    void WriteVDword(uint32_t v);
    uint32_t ReadVDword();

    void AddPostEvent(S_EVENTMSG *pEM);

    void LoadPreprocess();

    void CopyOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &srclist, STRINGS_LIST &dstlist, const char *sname);

    void FormatDialog(const char *file_name);
    void FormatAllDialog(const char *directory_name);


    bool SetSaveData(const char *file_name, void *save_data, int32_t data_size);
    void *GetSaveData(const char *file_name, int32_t &data_size);

    void AddRuntimeEvent();

    uint32_t SetScriptFunction(IFUNCINFO *pFuncInfo);

    bool CompileExpression(SEGMENT_DESC &Segment);
    bool CompileTernary(SEGMENT_DESC &Segment);
    bool CompileBinaryLevel(SEGMENT_DESC &Segment, bool (COMPILER::*operand)(SEGMENT_DESC &),
                            std::initializer_list<S_TOKEN_TYPE> ops, S_TOKEN_TYPE emitted_for_and);
    bool CompileBitOr(SEGMENT_DESC &Segment);
    bool CompileBitAnd(SEGMENT_DESC &Segment);
    bool CompileShift(SEGMENT_DESC &Segment);
    bool CompileCast(SEGMENT_DESC &Segment);
    bool CompileTypeid(SEGMENT_DESC &Segment);
    bool CompileAttributeDefault(SEGMENT_DESC &Segment);
    bool CompileAttributeRoot(SEGMENT_DESC &Segment);
    S_TOKEN_TYPE DetectCallThrough();
    bool CompileCallThrough(SEGMENT_DESC &Segment, S_TOKEN_TYPE call_type);
    bool CompileObjectLiteral(SEGMENT_DESC &Segment);
    bool IsAccessVariable();
    bool IsPlainAttributePath();
    bool IsTextVariable(S_TOKEN_TYPE var_type, uint32_t var_code);
    bool CompileNewArray(SEGMENT_DESC &Segment);
    std::deque<DATA> newArrays_; // arrays made by "new type[n]" (scripts keep references to them)
    bool CompileChainTail(SEGMENT_DESC &Segment);
    bool IsAttributeCall();
    bool IsArefVariable(S_TOKEN_TYPE var_type, uint32_t var_code);
    bool IsIndexedChain();
    static constexpr uint32_t kAccessRead = 0, kAccessQuiet = 1, kAccessCreate = 2; // ADVANCE_AP_EXPR modes
    bool CompileAccessExpression(SEGMENT_DESC &Segment, uint32_t mode);
    void QuietChainElement(SEGMENT_DESC &Segment, uint32_t offset);
    bool CompileAttributeAddress(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code, uint32_t mode);
    static bool IsAttributeIntrinsic(const char *name);
    bool CompileAttributeIntrinsic(SEGMENT_DESC &Segment);
    bool CompileArrayLiteral(SEGMENT_DESC &Segment);
    bool IsSlice();
    bool CompileSlice(SEGMENT_DESC &Segment, S_TOKEN_TYPE source, uint32_t source_code);
    uint32_t FindMethod(DATA *self, const char *method);
    bool ScanObjectLiteral(ATTRIBUTES *object);
    bool PeekRangeFor(S_TOKEN_TYPE &loop_type);
    bool CompileRangeFor(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE loop_type,
                         STRINGS_LIST &BreakUpdateTable);
    static constexpr const char *kRangeContainer = "#rangecontainer"; // hidden locals of a range-for
    static constexpr const char *kRangePosition = "#rangeposition";
    S_TOKEN_TYPE NextScanToken();
    // typeid values
    static constexpr int32_t kTypeidInt = 1, kTypeidString = 2, kTypeidFloat = 3, kTypeidAref = 4, kTypeidObject = 5,
                             kTypeidRef = 6, kTypeidBool = 7, kTypeidFref = 8;
    bool CompileExpression_L0(SEGMENT_DESC &Segment);
    bool CompileExpression_L1(SEGMENT_DESC &Segment);
    bool CompileExpression_L2(SEGMENT_DESC &Segment);
    bool CompileExpression_L3(SEGMENT_DESC &Segment);
    bool CompileExpression_L4(SEGMENT_DESC &Segment);
    bool CompileExpression_L5(SEGMENT_DESC &Segment);
    bool CompileExpression_L6(SEGMENT_DESC &Segment);
    bool CompileExpression_L7(SEGMENT_DESC &Segment);

    DATA *GetOperand(const char *pCodeBase, uint32_t &ip, S_TOKEN_TYPE *pTokenType = nullptr);

    // writes down current script stack (internal functions+script functions+events) to logfile
    // currently used for collecting additional crash info
    // TODO: use it for internal errors also
    void CollectCallStack() const;

    // printout script functions usage
    void PrintoutUsage();

    // Host compile check (src/apps/script_check): every compile diagnostic is also appended to
    // `sink`; source line tracking is on, the script cache is off and a segment that fails to
    // compile keeps its declarations, so the segments compiled after it are checked against them.
    // missing_natives_are_errors: false for a game check (no engine library is linked), true for
    // script tests (as in the game)
    void ConfigureCompileCheck(std::vector<std::string> *sink, bool missing_natives_are_errors);
    // script tests: store and load the script cache in this folder (also checks its integrity)
    void UseScriptCache(std::filesystem::path folder);
    // whether the last BC_LoadSegment took the segment from the script cache
    bool LastSegmentFromCache() const
    {
        return lastSegmentFromCache_;
    }
    // the game language for %language% in include paths (CL); by default language.ini's default
    // language, the one the string service starts with
    void SetScriptLanguage(std::string language)
    {
        scriptLanguage_ = std::move(language);
    }
    // bytecode of a loaded segment, nullptr if it is not loaded
    const char *GetSegmentCode(const char *segment_name, uint32_t &size) const;
    // names a loaded segment defines (functions, globals, defines): script_check retries a segment
    // that uses them with that segment loaded, as the game loads segments together
    std::vector<std::string> GetSegmentSymbols(const char *segment_name);
    // runs a script function that takes no arguments; `value` receives its result as text
    bool RunScriptFunction(const char *func_name, std::string &value);
    // attribute name of an ACCESS_WORD_CODE (codes depend on the order names were first seen)
    const char *GetAccessWord(uint32_t code)
    {
        return SCodec.Convert(code);
    }

private:
#ifdef __ANDROID__
    bool androidUiAdapterAttempted_ = false;
    void ResetAndroidUiAdapter();
    void LoadAndroidUiAdapter();
#endif
#ifdef STORM_SCENE_SWEEP
    void LoadSceneSweep(); // scene_sweep.cpp
#endif
    [[nodiscard]] std::filesystem::path GetSegmentCachePath(const SEGMENT_DESC &segment) const;

    bool LoadSegmentFromCache(SEGMENT_DESC &segment);
    void LoadDefinesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadVariablesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadFunctionsFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadScriptLibrariesFromCache(storm::script_cache::BufferReader &reader);
    void LoadEventHandlersFromCache(storm::script_cache::BufferReader &reader);
    void LoadByteCodeFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);

    void SaveSegmentToCache(const SEGMENT_DESC &segment);
    void SaveDefinesToCache(storm::script_cache::BufferWriter &writer);
    void SaveVariablesToCache(storm::script_cache::BufferWriter &writer);
    void SaveFunctionsToCache(storm::script_cache::BufferWriter &writer);
    void SaveScriptLibrariesToCache(storm::script_cache::BufferWriter &writer);
    void SaveEventHandlersToCache(storm::script_cache::BufferWriter &writer);
    void SaveByteCodeToCache(storm::script_cache::BufferWriter &writer, const SEGMENT_DESC &segment);

    COMPILER_STAGE CompilerStage;
    STRINGS_LIST LabelTable;
    // STRINGS_LIST EventTable;
    STRINGS_LIST LabelUpdateTable;
    TOKEN Token;
    MESSAGE *pEventMessage;
    std::vector<SEGMENT_DESC> SegmentTable;
    uint32_t SegmentsNum;
    uint32_t RunningSegmentID;
    uint32_t InstructionPointer;

    char *pBuffer;
    uint32_t dwCurPointer, dwMaxSize;

    char *ProgramDirectory;
    bool bCompleted;
    bool bEntityUpdate;
    char *pDebExpBuffer;
    uint32_t nDebExpBufferSize;

    FuncFrame *pRun_fi; // running function frame (scalar snapshot + stable table entry)
    FuncTable FuncTab;
    VarTable VarTab;
    S_DEFTAB DefTab;
    S_STACK SStack;
    S_EVENTTAB EventTab;
    // TCLASS_LIST<S_EVENTMSG> EventMsg;
    POSTEVENTS_LIST EventMsg;
    std::vector<SLIBHOLDER> LibriaryFuncs;

    STRING_CODEC SCodec;

    bool bRuntimeLog;
    uint32_t nRuntimeLogEventsBufferSize;
    uint32_t nRuntimeLogEventsNum;
    std::vector<uint32_t> pRuntimeLogEvent;
    uint32_t nRuntimeTicks;

    bool bFirstRun;
    bool bScriptTrace;
    bool bWriteCodeFile;
    bool bDebugInfo;
    char DebugSourceFileName[MAX_PATH];
    char gs[MAX_PATH];
    uint32_t DebugSourceLine;


    // HANDLE hSaveFileFileHandle;

    bool bDebugExpressionRun;
    bool bTraceMode;

    bool bEventsBreak;

    char DebugTraceFileName[MAX_PATH];
    uint32_t nDebugTraceLineCode;

    uint32_t nIOBufferSize;
    uint32_t nIOFullSize;
    char *pIOBuffer;

    // script registers
    DATA rAX;
    DATA rBX;
    DATA ExpressionResult;
    ATTRIBUTES *rAP;

    // loggers
    storm::logging::logger_ptr logTrace_;
    storm::logging::logger_ptr logError_;
    storm::logging::logger_ptr logStack_;

    // backtrace stack
    // NB: pointers are safe as long as we pop elements before they expire
    static constexpr size_t CALLSTACK_SIZE = 64U;
    storm::ringbuffer_stack<std::tuple<const char *, size_t, const char *>, CALLSTACK_SIZE> callStack_;

    // attempt to read/write script cache?
    int script_cache_mode_;

    // see ConfigureCompileCheck
    std::vector<std::string> *diagnosticsSink_ = nullptr;
    bool compileCheck_ = false;
    std::filesystem::path cacheFolder_; // UseScriptCache; empty: the stash folder
    bool lastSegmentFromCache_ = false;
    [[nodiscard]] std::filesystem::path CacheFolder() const;
    std::string ScriptLanguageDirectory();
    char *appendBuffer_ = nullptr; // AppendProgram's buffer and its capacity
    uint32_t appendCapacity_ = 0;
    std::string scriptLanguage_; // see SetScriptLanguage
    uint32_t compileErrors_ = 0; // errors reported while compiling the current segment
    bool missingNativesAreErrors_ = true;
    std::set<std::string> missingNatives_; // reported once per segment
    // pass 2: locals visible at the current point, innermost last
    struct LocalBinding
    {
        std::string name;
        uint32_t slot;
        uint32_t depth;
    };
    std::vector<LocalBinding> localScopes_;
    std::unordered_map<std::string, uint32_t> localDeclarations_; // declarations so far per name
    bool declListActive_ = false; // inside "int a, b;"
    bool declExpectName_ = false; // a ',' of that list was just compiled
    std::vector<uint32_t> *switchFallthrough_ = nullptr; // innermost switch: jumps into the next case
    std::vector<std::string> *undeclaredNatives_ = nullptr; // SetScriptFunction during a library's Init
    std::unordered_map<std::string, IFUNCINFO> pendingNatives_; // registered, not declared yet (lowercase)
    storm::ScriptCache script_cache_;
};
