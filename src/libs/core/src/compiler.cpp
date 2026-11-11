#include "compiler.h"
#ifdef __ANDROID__
#include "platform/android_land_commands.hpp"
#include "platform/android_land_diagnostics.hpp"
#include "platform/android_script_errors.hpp"
#endif

#include <chrono>
#include <cstdio>

#include <zlib.h>

#ifdef _WIN32 // S_DEBUG
#include "s_debug.h"
#else
#include "core_impl.h"
#endif
#include "debug-trap.h"
#include "fs.h"
#include "logging.hpp"
#include "platform/path_fingerprint.hpp"
#include "save_file.hpp"
#include "script_cache.h"
#include "script_cache_files.hpp"
#include "storm_assert.h"
#include "watermark.hpp"

#include <SDL_timer.h>
#include <unordered_map>
#include <fmt/ranges.h>

#define SKIP_COMMENT_TRACING
#define TRACE_OFF
#define INVALID_SEGMENT_INDEX 0xffffffff
#ifndef INVALID_ARRAY_INDEX
#define INVALID_ARRAY_INDEX 0xffffffff
#endif
#define INVALID_OFFSET 0xffffffff
#define DSL_INI_VALUE 0
#define SBUPDATE 4
#define DEF_COMPILE_EXPRESSIONS

#ifdef _WIN32 // S_DEBUG
namespace
{
S_DEBUG s_debug;
}
#endif

namespace
{
enum CacheMode : int
{
    kCacheDisabled = 0,             // Disable script caching
    kCacheEnabled = 1,              // Enable script caching and integrity checks
    kCacheEnabledNoRuntimeCheck = 2 // Enable script caching; only check integrity once - should be used if no
                                    // changes(e.g. in the interface scripts) are expected during the session
};

auto GetCacheFolder()
{
    static std::filesystem::path cache_folder = fs::GetStashPath() / "Cache";
    return cache_folder;
}

// Everything that can make cached bytecode stale besides the scripts
// themselves: the engine build (token numbering, internal function table and
// bytecode layout are not stable across builds), the cache layout, and which
// game directory the scripts came from (the cache folder is shared).
constexpr uint64_t kCacheFormatVersion = 5; // 3: CL script extensions (defaults, "...", native); 4: array flag;
                                             // 5: the key has the script language

uint64_t CalculateCacheKey(const char *program_directory, const std::string &language)
{
    const std::string resolved = fio->ConvertPathResource(program_directory ? program_directory : "");
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(resolved), ec);
    uint64_t key = storm::fingerprint::fnv1a(STORM_BUILD_WATERMARK_STRING " " __DATE__ " " __TIME__);
    key = storm::fingerprint::fnv1a(kCacheFormatVersion, key);
    key = storm::fingerprint::fnv1a(language, key); // %language% includes
    key = storm::fingerprint::fnv1a(static_cast<uint64_t>(sizeof(size_t)), key);
    key = storm::fingerprint::fnv1a(ec ? resolved : absolute.lexically_normal().generic_string(), key);
    return storm::fingerprint::mix(key) + fio->GetPathFingerprint(std::filesystem::u8path(resolved));
}

} // namespace

// extern char * FuncNameTable[];
extern INTFUNCDESC IntFuncTable[];
#ifdef _WIN32 // S_DEBUG
extern S_DEBUG *CDebug = &s_debug;
#endif
extern uint32_t dwNumberScriptCommandsExecuted;

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;

COMPILER::COMPILER()
    : bBreakOnError(false), pRunCodeBase(nullptr), CompilerStage(CS_SYSTEM), pEventMessage(nullptr), SegmentsNum(0),
      InstructionPointer(0), pBuffer(nullptr), ProgramDirectory(nullptr), bCompleted(false), bEntityUpdate(true),
      pDebExpBuffer(nullptr), nDebExpBufferSize(0), pRun_fi(nullptr), bRuntimeLog(false),
      nRuntimeLogEventsBufferSize(0), nRuntimeLogEventsNum(0), nRuntimeTicks(0), bFirstRun(true), bWriteCodeFile(false),
      bDebugInfo(false), DebugSourceLine(0), bDebugExpressionRun(false),
      bTraceMode(true), nDebugTraceLineCode(0), nIOBufferSize(0), pIOBuffer(nullptr), rAP(nullptr),
      script_cache_mode_(kCacheDisabled)

{
    LabelTable.SetStringDataSize(sizeof(uint32_t));
    LabelUpdateTable.SetStringDataSize(sizeof(DOUBLE_DWORD));

    //    FuncTab.KeepNameMode(true);    // true for displaying internal functions names
    //    VarTab.KeepNameMode(false);
    //    EventTab.KeepNameMode(false);
    //    DefTab.KeepNameMode(false);
    InitInternalFunctions();

    strcpy_s(DebugSourceFileName, "<no debug information>");

    SStack.SetVCompiler(this);
    VarTab.SetVCompiler(this);
    srand(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());

    DebugTraceFileName[0] = 0;

    rAX.SetVCompiler(this);
    rBX.SetVCompiler(this);
    ExpressionResult.SetVCompiler(this);

    // bScriptTrace = false;

    using storm::logging::getOrCreateLogger;
    logTrace_ = getOrCreateLogger("compile");
    logError_ = getOrCreateLogger("error");
    logStack_ = getOrCreateLogger("script_stack");
    logStack_->set_pattern("%v");
}

COMPILER::~COMPILER()
{
    Release();
}

void COMPILER::Release()
{
#ifdef __ANDROID__
    ResetAndroidUiAdapter();
#endif
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        delete[] SegmentTable[n].pData;
        delete[] SegmentTable[n].pCode;
        if (SegmentTable[n].Files_list)
        {
            SegmentTable[n].Files_list->Release();
            delete SegmentTable[n].Files_list;
        }
        SegmentTable[n].Files_list = nullptr;
    }
    SegmentTable.clear();
    SegmentsNum = 0;
    LabelTable.Release();
    LabelUpdateTable.Release();
    Token.Reset();
    delete[] ProgramDirectory;
    ProgramDirectory = nullptr;
    RunningSegmentID = INVALID_SEGMENT_INDEX;


    FuncTab.Release();
    VarTab.Release();
    DefTab.Release();
    SStack.Release();
    EventTab.Release();
    EventMsg.Release();
    SCodec.Release();
    LibriaryFuncs.clear();
    pendingNatives_.clear();
    newArrays_.clear();

    delete[] pDebExpBuffer;
    pDebExpBuffer = nullptr;
    nDebExpBufferSize = 0;

    nRuntimeLogEventsNum = 0;
    nRuntimeLogEventsBufferSize = 0;

    rAX.Release();
    rBX.Release();
    ExpressionResult.Release();
    // Token.Release();
}

void COMPILER::SetProgramDirectory(const char *dir_name)
{
    delete[] ProgramDirectory;
    ProgramDirectory = nullptr;
    if (dir_name)
    {
        const auto len = strlen(dir_name) + strlen("\\") + 1;
        ProgramDirectory = new char[len];
        strcpy_s(ProgramDirectory, len, dir_name);
        strcat_s(ProgramDirectory, len, "\\");
    }
#ifdef _WIN32 // S_DEBUG
    CDebug->SetProgramDirectory(dir_name);
#endif
}

// load file into memory
char *COMPILER::LoadFile(const char *file_name, uint32_t &file_size, bool bFullPath)
{
    const char *fName;
    char buffer[MAX_PATH];

    if (!bFullPath)
    {
        std::string EngineDir = "storm-engine\\";
        if (strncmp(file_name, EngineDir.c_str(), EngineDir.length()) == 0)
        {
            std::string ExePath = fio->_GetExecutableDirectory() + "resource\\shared\\";
            strcpy_s(buffer, ExePath.c_str());
            strcat_s(buffer, file_name + EngineDir.length());
        }
        else if (ProgramDirectory)
        {
            strcpy_s(buffer, ProgramDirectory);
            strcat_s(buffer, file_name);
        }
        else
        {
            strcpy_s(buffer, file_name);
        }
    }

    file_size = 0;
    if (bFullPath)
    {
        // f expand
        /*DWORD n,m;
        m = 0;
        for(n=0;file_name[n];n++)
        {
          buffer[m] = file_name[n];
          if(buffer[m] == '\\')
          {
            m++;
            buffer[m] = '\\';
          }
          m++;
        }
        buffer[m] = 0;*/

        fName = file_name;
    }
    else
    {
        fName = &buffer[0];
    }

    auto fileS = fio->_CreateFile(fName, std::ios::binary | std::ios::in);
    if (!fileS.is_open())
    {
        return nullptr;
    }
    const auto fsize = fio->_GetFileSize(fName);

    auto *const pData = static_cast<char *>(new char[fsize + 1]);
    if (!fio->_ReadFile(fileS, pData, fsize))
    {
        delete[] pData;
        fio->_CloseFile(fileS);
        return nullptr;
    }
    fio->_CloseFile(fileS);
    file_size = fsize;
    pData[fsize] = 0;
    if (fsize >= 3 && static_cast<uint8_t>(pData[0]) == 0xEF && static_cast<uint8_t>(pData[1]) == 0xBB &&
        static_cast<uint8_t>(pData[2]) == 0xBF)
        pData[0] = pData[1] = pData[2] = ' '; // UTF-8 byte order mark
    return pData;
}

// write to compilation log file
void COMPILER::Trace(const char *data_PTR, ...)
{
#ifdef TRACE_OFF
    return;
#endif
    if (data_PTR == nullptr)
        return;
    char LogBuffer[4096];
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    va_end(args);
    logTrace_->trace(LogBuffer);
}

// write to compilation log file
void COMPILER::DTrace(const char *data_PTR, ...)
{
#ifdef DTRACEOFF
    return;
#endif
    if (data_PTR == nullptr)
        return;
    char LogBuffer[4096];
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    va_end(args);
    logTrace_->trace(LogBuffer);
}

// append one block of code to another
// return new pointer
/*char * COMPILER::AppendProgram(char * pBase_program, int32_t Base_program_size, char * pAppend_program, int32_t
Append_program_size, int32_t& new_program_size)
{
    char * pTemp;
    char * pBase;
    new_program_size = Base_program_size + Append_program_size + 2;

    pBase = new char[new_program_size];
    pTemp = pBase;
    memcpy(pTemp,pBase_program,Base_program_size);
    pTemp += Base_program_size;
    pTemp[0] = ' ';    pTemp++;    // code blocks separator ' ' to prevent data merging
    memcpy(pTemp,pAppend_program,Append_program_size);
    delete pBase_program;
    delete pAppend_program;
    return pBase;
}*/

bool COMPILER::AppendProgram(char *&pBase_program, uint32_t &Base_program_size, const char *pAppend_program,
                             uint32_t &Append_program_size, bool bAddLinefeed)
{
    // appends a file and a separator ("\r\n" between files, ';' after a #file marker) so that
    // their tokens cannot merge; the buffer grows geometrically (a segment appends hundreds of files)
    if (pAppend_program == nullptr)
        return false;
    const uint32_t offset = Base_program_size;
    const uint32_t separator = bAddLinefeed ? 2 : 1;
    const uint32_t needed = Base_program_size + Append_program_size + separator + 1; // + terminating zero
    if (pBase_program == nullptr || pBase_program != appendBuffer_ || needed > appendCapacity_)
    {
        const uint32_t capacity = std::max(needed, pBase_program == appendBuffer_ ? appendCapacity_ * 2 : needed);
        auto *const newPtr = new char[capacity];
        if (pBase_program)
            memcpy(newPtr, pBase_program, Base_program_size);
        delete[] pBase_program;
        pBase_program = newPtr;
        appendBuffer_ = newPtr;
        appendCapacity_ = capacity;
    }
    memcpy(&pBase_program[offset], pAppend_program, Append_program_size);
    Base_program_size += Append_program_size + separator;
    if (bAddLinefeed)
    {
        pBase_program[Base_program_size - 2] = 0xd;
        pBase_program[Base_program_size - 1] = 0xa;
    }
    else
        pBase_program[Base_program_size - 1] = ';';
    pBase_program[Base_program_size] = 0;
    delete[] pAppend_program;
    Append_program_size += separator;
    return true;
}

void COMPILER::SetError(const char *data_PTR, ...)
{
    if (bDebugExpressionRun)
        return;
    if (data_PTR == nullptr)
        return;
#ifdef __ANDROID__
    ++storm::android::scriptErrorEpoch;
#endif
    char LogBuffer[BUFSIZ];
    char ErrorBuffer[MAX_PATH + BUFSIZ];
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    uint32_t bytes;
    FindErrorSource();

    switch (CompilerStage)
    {
    case CS_SYSTEM:
        sprintf_s(ErrorBuffer, "ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    case CS_COMPILATION:
        compileErrors_++;
        sprintf_s(ErrorBuffer, "COMPILE ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    case CS_RUNTIME:
        sprintf_s(ErrorBuffer, "RUNTIME ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    }
    va_end(args);

    logError_->error(ErrorBuffer);
    if (diagnosticsSink_)
        diagnosticsSink_->emplace_back(ErrorBuffer);

#ifdef _WIN32 // S_DEBUG
    if (bBreakOnError)
        CDebug->SetTraceMode(TMODE_MAKESTEP);
#endif
}

void COMPILER::SetWarning(const char *data_PTR, ...)
{
#ifdef WARNINGS_OFF
    return;
#endif
    if (bDebugExpressionRun)
        return;
    char LogBuffer[BUFSIZ];
    char ErrorBuffer[MAX_PATH + BUFSIZ];
    if (data_PTR == nullptr)
        return;
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    uint32_t bytes;
    FindErrorSource();

    sprintf_s(ErrorBuffer, "WARNING in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
    va_end(args);

    logTrace_->warn(ErrorBuffer);
    if (diagnosticsSink_)
        diagnosticsSink_->emplace_back(ErrorBuffer);
}

std::string COMPILER::ScriptLanguageDirectory(bool report)
{
    // %language% in include paths: the directory of the game language, as the string service
    // takes it from RESOURCE\INI\TEXTS\language.ini ([COMMON] defaultLanguage, [DIRECTORY])
    // unless SetScriptLanguage chose another language
    constexpr const char *language_ini = "resource\\ini\\texts\\language.ini";
    auto ini = fio->OpenIniFile(language_ini);
    if (!ini)
    {
        if (report)
            SetError("%%language%% in an include: %s not found", language_ini);
        return {};
    }
    char language[256]{};
    if (!scriptLanguage_.empty())
        strcpy_s(language, scriptLanguage_.c_str());
    else if (!ini->ReadString("COMMON", "defaultLanguage", language, sizeof(language) - 1, ""))
    {
        if (report)
            SetError("%%language%% in an include: %s has no [COMMON] defaultLanguage", language_ini);
        return {};
    }
    char directory[256]{};
    if (!ini->ReadString("DIRECTORY", language, directory, sizeof(directory) - 1, "") || directory[0] == 0)
    {
        if (report)
            SetError("%%language%% in an include: %s has no [DIRECTORY] entry for '%s'", language_ini, language);
        return {};
    }
    return directory;
}

std::filesystem::path COMPILER::CacheFolder() const
{
    return cacheFolder_.empty() ? GetCacheFolder() : cacheFolder_;
}

void COMPILER::UseScriptCache(std::filesystem::path folder)
{
    cacheFolder_ = std::move(folder);
    script_cache_mode_ = kCacheEnabled;
}

void COMPILER::ConfigureCompileCheck(std::vector<std::string> *sink, bool missing_natives_are_errors)
{
    diagnosticsSink_ = sink;
    missingNativesAreErrors_ = missing_natives_are_errors;
    compileCheck_ = true;
    bDebugInfo = true;
    script_cache_mode_ = kCacheDisabled;
}

const char *COMPILER::GetSegmentCode(const char *segment_name, uint32_t &size) const
{
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (SegmentTable[n].name == segment_name)
        {
            size = SegmentTable[n].BCode_Program_size;
            return SegmentTable[n].pCode;
        }
    }
    size = 0;
    return nullptr;
}

std::vector<std::string> COMPILER::GetSegmentSymbols(const char *segment_name)
{
    std::vector<std::string> names;
    uint32_t id = INVALID_SEGMENT_ID;
    for (uint32_t n = 0; n < SegmentsNum; n++)
        if (SegmentTable[n].name == segment_name)
            id = SegmentTable[n].id;
    if (id == INVALID_SEGMENT_ID)
        return names;
    for (size_t n = 0; n < FuncTab.GetFuncNum(); n++)
        if (const auto *fi = FuncTab.GetFuncInfo(n); fi && fi->segment_id == id && fi->offset != INVALID_FUNC_OFFSET)
            names.push_back(fi->name);
    for (size_t n = 0; n < VarTab.GetVarNum(); n++)
        if (const auto *vi = VarTab.GetVarX(n); vi && vi->segment_id == id)
            names.push_back(vi->name);
    for (uint32_t n = 0; n < DefTab.GetDefNum(); n++)
    {
        DEFINFO di;
        if (DefTab.GetDefX(di, n) && di.segment_id == id && di.name)
            names.emplace_back(di.name);
    }
    return names;
}

bool COMPILER::RunScriptFunction(const char *func_name, std::string &value)
{
    value.clear();
    const auto func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("function '%s' not found", func_name);
        return false;
    }
    DATA *result = nullptr;
    if (!BC_Execute(func_code, result))
        return false;
    if (result)
    {
        DATA copy;
        copy.SetVCompiler(this);
        copy.Copy(result);
        copy.Convert(VAR_STRING);
        const char *text = nullptr;
        copy.Get(text);
        value = text ? text : "";
        SStack.Pop();
    }
    return true;
}

void COMPILER::LoadPreprocess()
{
    auto engine_ini = fio->OpenIniFile(core_internal.EngineIniFileName());
    if (engine_ini)
    {
        if (engine_ini->GetInt("script", "debuginfo", 0) == 0)
        {
            bDebugInfo = false;
            // FuncTab.KeepNameMode(false);
            // VarTab.KeepNameMode(false);
            // DefTab.KeepNameMode(false);
            // EventTab.KeepNameMode(false);
        }
        else
        {
            bDebugInfo = true;
            // FuncTab.KeepNameMode(true);
            // VarTab.KeepNameMode(true);
            // DefTab.KeepNameMode(true);
            // EventTab.KeepNameMode(true);
        }
        if (engine_ini->GetInt("script", "codefiles", 0) == 0)
            bWriteCodeFile = false;
        else
            bWriteCodeFile = true;

        // seconds one call from the engine into the scripts may run (0: no limit)
        scriptCallTimeLimitMs_ = static_cast<uint32_t>(engine_ini->GetInt("script", "call_time_limit", 60)) * 1000;

        if (engine_ini->GetInt("script", "runtimelog", 0) == 0)
            bRuntimeLog = false;
        else
            bRuntimeLog = true;

        // On by default: game scripts do not change while the game runs (see
        // tools/android/README.md, "Compiled script cache").
        script_cache_mode_ = engine_ini->GetInt("script", "cache_mode", kCacheEnabledNoRuntimeCheck);
        if (script_cache_mode_ < kCacheDisabled || script_cache_mode_ > kCacheEnabledNoRuntimeCheck)
        {
            spdlog::warn("engine.ini [script] cache_mode = {} is not 0, 1 or 2; the script cache is off",
                         script_cache_mode_);
            script_cache_mode_ = kCacheDisabled;
        }

        // if(engine_ini->GetInt("script","tracefiles",0) == 0) bScriptTrace = false;
        // else bScriptTrace = true;
    }

#ifdef _WIN32 // S_DEBUG
    auto ini = fio->OpenIniFile(PROJECT_NAME);
    if (ini)
    {
        bBreakOnError = (ini->GetInt("options", "break_on_error", 0) == 1);
    }
#endif
}

bool COMPILER::CreateProgram(const char *file_name)
{
    /*    INIFILE * engine_ini;

    engine_ini = fio->OpenIniFile(core_internal.EngineIniFileName());
    if(engine_ini != null)
    {
      if(engine_ini->GetInt("script","debuginfo",0) == 0)
      {
        bDebugInfo = false;
        FuncTab.KeepNameMode(false);
        VarTab.KeepNameMode(false);
        DefTab.KeepNameMode(false);
        EventTab.KeepNameMode(false);
      }
      else
      {
        bDebugInfo = true;
        FuncTab.KeepNameMode(true);
        VarTab.KeepNameMode(true);
        DefTab.KeepNameMode(true);
        EventTab.KeepNameMode(true);
      }
      if(engine_ini->GetInt("script","codefiles",0) == 0) bWriteCodeFile = false;
      else bWriteCodeFile = true;


      delete engine_ini;
    }
  //*/
    LoadPreprocess();
    const bool bRes = BC_LoadSegment(file_name);

    /*    for(DWORD m=0;m<HASH_TABLE_SIZE;m++)
      {
        DTrace("HashIndex[%d]",m);
        for(DWORD n=0;n<SCodec.HTable[m].nStringsNum;n++)
        {
          DTrace(SCodec.HTable[m].ppDat[n]);
        }
      }*/
    return bRes;
}

#ifdef __ANDROID__
void COMPILER::ResetAndroidUiAdapter()
{
    androidUiAdapterAttempted_ = false;
    // Script tables and static events are about to disappear. Revoke only our
    // Android leases/tokens; missing presentation support must not close stock.
    storm::android::landCommands.releaseUnavailableSource(storm::android::LandCommandSource::Missing);
    storm::android::traceLandAdapter(0, "script-reset");
}
void COMPILER::LoadAndroidUiAdapter()
{
    // Called after Main/OnLoad return, never inside a frame or an event handler.
    // Optional app-owned entry point: no shipped game script is copied here.
    if (androidUiAdapterAttempted_) return;
    androidUiAdapterAttempted_ = true;
    constexpr const char *segment = "interface/android_ui_bootstrap.c";
    if (BC_SegmentIsLoaded(segment)) {
        storm::android::traceLandAdapter(0, "already-loaded"); return;
    }
    const std::string path = std::string(ProgramDirectory ? ProgramDirectory : "") + segment;
    if (!fio->_FileOrDirectoryExists(path.c_str())) {
        storm::android::traceLandAdapter(0, "missing-entrypoint"); return;
    }
    const bool loaded = BC_LoadSegment(segment);
    storm::android::traceLandAdapter(0, loaded ? "loaded" : "compile-failed");
}
#endif

bool COMPILER::Run()
{
    pRun_fi = nullptr;
    const uint32_t function_code = FuncTab.FindFunc("Main");
    DATA *pResult;
    bFirstRun = false;
    const bool executed = BC_Execute(function_code, pResult);
    pRun_fi = nullptr;
#ifdef __ANDROID__
    if (executed) LoadAndroidUiAdapter();
    else storm::android::traceLandAdapter(0, "main-execution-failed");
#else
    (void)executed;
#endif
#ifdef STORM_SCENE_SWEEP
    LoadSceneSweep();
#endif

    // DATA Result;
    // Result.SetVCompiler(this);
    // ProcessDebugExpression("Characters[45].id",Result);

    /*for(DWORD m=0;m<HASH_TABLE_SIZE;m++)
    {
      DTrace("HashIndex[%d]",m);
      for(DWORD n=0;n<SCodec.HTable[m].nStringsNum;n++)
      {
        DTrace(SCodec.HTable[m].ppDat[n]);
      }
    }*/

    return true;
}

void COMPILER::FindErrorSource()
{
    const uint32_t segment_index = GetSegmentIndex(RunningSegmentID);
    if (segment_index == INVALID_SEGMENT_INDEX)
        return;

    uint32_t index;
    uint32_t token_data_size;
    uint32_t file_line_offset;
    uint32_t file_name_size;
    S_TOKEN_TYPE Token_type;

    uint32_t ip = 0;
    DebugSourceLine = 0;
    file_line_offset = 0;
    char *pCodeBase = SegmentTable[segment_index].pCode;
    do
    {
        Token_type = static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pCodeBase[ip]));
        ip++;
        if (static_cast<uint8_t>(pCodeBase[ip]) < 0xff)
        {
            token_data_size = static_cast<uint8_t>(pCodeBase[ip]);
            ip++;
        }
        else
        {
            ip++;
            memcpy(&token_data_size, &pCodeBase[ip], sizeof(uint32_t));
            ip += sizeof(uint32_t);
        }

        if (ip >= InstructionPointer)
        {
            // DebugSourceLine -= file_line_offset;
            return;
        }
        switch (Token_type)
        {
        case DEBUG_LINE_CODE:
            memcpy(&DebugSourceLine, &pCodeBase[ip], sizeof(uint32_t));
            break;
        case DEBUG_FILE_NAME:
            index = ip;
            memcpy(&file_line_offset, &pCodeBase[index], sizeof(uint32_t));
            index += sizeof(uint32_t);
            memcpy(&file_name_size, &pCodeBase[index], sizeof(uint32_t));
            index += sizeof(uint32_t);
            memcpy(DebugSourceFileName, &pCodeBase[index], file_name_size);
            index += file_name_size;
            DebugSourceFileName[file_name_size] = 0;
            DebugSourceLine = DSL_INI_VALUE;
            break;
        }
        ip += token_data_size; // step to next instruction
    } while (Token_type != END_OF_PROGRAMM);
}

void COMPILER::SetEventHandler(const char *event_name, const char *func_name, int32_t flag, bool bStatic)
{
    FuncInfo fi;

    if (event_name == nullptr)
    {
        SetError("Invalid event name for SetEventHandler");
        return;
    }
    if (func_name == nullptr)
    {
        SetError("Invalid function name for SetEventHandler");
        return;
    }

    const uint32_t func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("Invalid function code in SetEventHandler");
        return;
    }

    if (!FuncTab.GetFunc(fi, func_code))
    {
        SetError("func not found eror");
        return;
    }

    EventTab.AddEventHandler(event_name, func_code, fi.segment_id, flag, bStatic);
}

void COMPILER::DelEventHandler(const char *event_name, const char *func_name)
{
    if (event_name == nullptr)
    {
        SetError("Bad event name");
        return;
    }

    if (func_name == nullptr)
    {
        SetError("Bad func name");
        return;
    }

    const uint32_t func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("Invalid function code in DelEventHandler");
        return;
    }

    EventTab.SetStatus(event_name, func_code, FSTATUS_DELETED);

    for (int32_t n = 0; n < static_cast<int32_t>(EventMsg.GetClassesNum()); n++)
    {
        S_EVENTMSG *pM = EventMsg.Read(n);
        if (!pM->pEventName)
            continue;
        if (pM->ProcessTime(0))
            continue; // skip events, possible executed on this frame
        if (storm::iEquals(pM->pEventName, event_name))
        {
            EventMsg.Del(n);
            n--;
        }
    }
}

VDATA *COMPILER::ProcessEvent(const char *event_name)
{
    // TODO: only do if stack debug if enabled (should be runtime configurable)
    // push event name to call stack
    storm::ringbuffer_stack_push_guard push_guard(callStack_);
    push_guard.push(std::make_tuple("", 0U, event_name));

    uint32_t event_code;
    uint32_t func_code;
    VDATA *pVD;
    DATA *pResult;
    MESSAGE *pMem;
    EVENTINFO ei;
#ifdef _WIN32 // S_DEBUG
    uint32_t current_debug_mode;
#endif

    uint64_t dwRDTSC, nTicks;

    RDTSC_B(dwRDTSC);

    bEventsBreak = false;

    uint32_t nTimeOnEvent = SDL_GetTicks();
#ifdef _WIN32 // S_DEBUG
    current_debug_mode = CDebug->GetTraceMode();
#endif

    pVD = nullptr;
    if (event_name == nullptr)
    {
        SetError("Invalid event name in ProcessEvent");
        return nullptr;
    }

    event_code = EventTab.FindEvent(event_name);
    if (event_code == INVALID_EVENT_CODE)
        return nullptr; // no handlers
    EventTab.GetEvent(ei, event_code);
    for (uint32_t n = 0; n < ei.elements; n++)
    {
        func_code = ei.pFuncInfo[n].func_code;
        if (ei.pFuncInfo[n].status != FSTATUS_NORMAL)
            continue;
        pMem = pEventMessage;
        if (pMem)
        {
            pMem->Move2Start();
        }

        const uint32_t nStackVars = SStack.GetDataNum(); // remember stack elements num
        RDTSC_B(nTicks);
        BC_Execute(ei.pFuncInfo[n].func_code, pResult);
        RDTSC_E(nTicks);

        FuncInfo fi;
        // if(FuncTab.GetFuncX(fi,ei.pFuncInfo[n].func_code))
        if (FuncTab.GetFuncX(fi, func_code))
        {
            if (fi.return_type != TVOID)
            {
                fi.return_type = TVOID;
            }
        } //*/

        if (EventTab.GetEvent(ei, event_code)) // to be sure event still exist
        {
            if (n < ei.elements)
            {
                if (!FuncTab.AddTime(ei.pFuncInfo[n].func_code, nTicks))
                {
                    core_internal.Trace("Invalid func_code = %u for AddTime", ei.pFuncInfo[n].func_code);
                }
            }
        }

        pEventMessage = pMem;
        if (pResult)
            pVD = pResult->GetVarPointer();
        if (pResult)
            SStack.Pop();

        if (nStackVars != SStack.GetDataNum())
        {
            SStack.InvalidateFrom(nStackVars); // restore stack state - crash situation
            pVD = nullptr;
            SetError("process event stack error");
        }

        if (bEventsBreak)
            break;
    }

    nTimeOnEvent = SDL_GetTicks() - nTimeOnEvent;

    nRuntimeTicks += nTimeOnEvent;

    pRun_fi = nullptr;

#ifdef _WIN32 // S_DEBUG
    if (current_debug_mode == TMODE_CONTINUE)
        CDebug->SetTraceMode(TMODE_CONTINUE);
#endif
    // SetFocus(core_internal.App_Hwnd);        // VANO CHANGES

    RDTSC_E(dwRDTSC);

    // VANO CHANGES - remove in release
    if (core_internal.Controls->GetDebugAsyncKeyState('5') < 0 &&
        core_internal.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0)
    {
        core_internal.Trace("evnt: %d, %s", dwRDTSC, event_name);
    }

    return pVD;
}

void COMPILER::ProcessEvent(const char *event_name, MESSAGE *pMs)
{
    pEventMessage = pMs;
    ProcessEvent(event_name);
    pEventMessage = nullptr;
}

VDATA *COMPILER::ProcessEvent(const char *event_name, MESSAGE message)
{
    pEventMessage = &message;
    VDATA *pVD = ProcessEvent(event_name);
    pEventMessage = nullptr;
    return pVD;
}

uint32_t COMPILER::GetSegmentIndex(uint32_t segment_id)
{
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (SegmentTable[n].id == segment_id)
            return n;
    }
    return INVALID_SEGMENT_INDEX;
}

// function release global variables, functions and labels reference for segment
// mark segment for subsequent unload (on ProcessFrame)
void COMPILER::UnloadSegment(const char *segment_name)
{
    //    OFFSET_INFO offset_info;

    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (strcmp(SegmentTable[n].name.c_str(), segment_name) == 0 && !SegmentTable[n].bUnload)
        {
            const uint32_t segment_id = SegmentTable[n].id;
            SegmentTable[n].bUnload = true;
            FuncTab.InvalidateBySegmentID(segment_id);
            VarTab.InvalidateBySegmentID(segment_id);
            EventTab.InvalidateBySegmentID(segment_id);
            DefTab.InvalidateBySegmentID(segment_id);
            if (SegmentTable[n].Files_list)
            {
                SegmentTable[n].Files_list->Release();
            }
            break;
        }
    }
}

bool COMPILER::BC_SegmentIsLoaded(const char *file_name)
{
    if (file_name == nullptr)
    {
        SetError("Invalid segment name");
        return false;
    }
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (strcmp(SegmentTable[n].name.c_str(), file_name) == 0 && !SegmentTable[n].bUnload)
            return true;
    }
    return false;
}

bool COMPILER::BC_LoadSegment(const char *file_name)
{
    uint32_t n;

    LabelUpdateTable.Release();

    if (file_name == nullptr)
    {
        SetError("Invalid segment name");
        return false;
    }

    // Trace("Loading segment: %s", file_name);

    // check for already loaded (a segment unloaded in this frame waits for ProcessFrame to free its
    // code, but its functions are gone: loading it again compiles a new copy)
    for (n = 0; n < SegmentsNum; n++)
    {
        if (strcmp(SegmentTable[n].name.c_str(), file_name) == 0 && !SegmentTable[n].bUnload)
        {
            SetWarning("Segment already loaded: %s", file_name);
            return true;
        }
    }

    // compute new segment id --------------------------
    uint32_t id = 0;
    bool found = false;
    while (!found)
    {
        found = true;
        for (n = 0; n < SegmentsNum; n++)
        {
            if (SegmentTable[n].id == id)
                found = false;
        }
        if (!found)
            id++;
    }

    const uint32_t index = SegmentsNum;
    SegmentsNum++;
    // SegmentTable = (SEGMENT_DESC *)RESIZE(SegmentTable,SegmentsNum*sizeof(SEGMENT_DESC));
    SegmentTable.resize(SegmentsNum);

    // const auto len = strlen(file_name) + 1;
    // SegmentTable[index].name = new char[len];
    // memcpy(SegmentTable[index].name, file_name, len);
    SegmentTable[index].name = strdup(file_name);
    SegmentTable[index].id = id;
    SegmentTable[index].bUnload = false;
    SegmentTable[index].pData = nullptr;
    SegmentTable[index].pCode = nullptr;
    SegmentTable[index].BCode_Program_size = 0;
    SegmentTable[index].BCode_Buffer_size = 0;

    SegmentTable[index].Files_list = new STRINGS_LIST;
    SegmentTable[index].Files_list->SetStringDataSize(sizeof(OFFSET_INFO));
    const auto load_started = std::chrono::steady_clock::now();
    bool result = false;
    bool from_cache = false;
    if (script_cache_mode_ != kCacheDisabled)
    {
        if (!cacheKeyCalculated_ || script_cache_mode_ == kCacheEnabled)
        {
            cacheKeyCalculated_ = true;
            cacheKey_ = CalculateCacheKey(ProgramDirectory, ScriptLanguageDirectory(false));
        }
        const uint64_t calculated_fingerprint = cacheKey_;

        using storm::script_cache::StateFile;
        uint64_t stored_fingerprint = 0;
        const auto state = storm::script_cache::readState(CacheFolder(), stored_fingerprint);
        if (state == StateFile::Read && stored_fingerprint == calculated_fingerprint)
        {
            // attempt to load from cache first
            result = from_cache = LoadSegmentFromCache(SegmentTable[index]);
        }
        else
        {
            // Every outcome is logged: a rebuild costs a full compile at startup.
            if (state == StateFile::Missing)
                spdlog::info("Script cache: none in '{}'; compiling and storing scripts", CacheFolder().string());
            else if (state == StateFile::Damaged)
                spdlog::warn("Script cache: state file in '{}' is damaged; rebuilding", CacheFolder().string());
            else
                spdlog::info("Script cache: scripts or engine build changed (key {:016x} -> {:016x}); rebuilding",
                             stored_fingerprint, calculated_fingerprint);
            if (const auto error = storm::script_cache::resetFolder(CacheFolder(), calculated_fingerprint);
                !error.empty())
            {
                spdlog::error("Script cache: {}; disabled for this session, scripts are compiled from source", error);
                script_cache_mode_ = kCacheDisabled;
            }
        }
    }

    if (!result)
    {
        result = Compile(SegmentTable[index]);
    }
    lastSegmentFromCache_ = result && from_cache;
    if (result)
    {
        spdlog::info("Script segment '{}' {} in {} ms", file_name, from_cache ? "loaded from cache" : "compiled",
                     duration_cast<milliseconds>(std::chrono::steady_clock::now() - load_started).count());
    }

    if (!result && !compileCheck_)
    {
        delete SegmentTable[index].Files_list;
        SegmentsNum--;
        // SegmentTable = (SEGMENT_DESC *)RESIZE(SegmentTable,SegmentsNum*sizeof(SEGMENT_DESC));
        SegmentTable.resize(SegmentsNum);
        FuncTab.InvalidateBySegmentID(id);
        VarTab.InvalidateBySegmentID(id);
        EventTab.InvalidateBySegmentID(id);
        DefTab.InvalidateBySegmentID(id);
    }
    return result;
}

bool COMPILER::ProcessDebugExpression(const char *pExpression, DATA &Result)
{
    if (pExpression == nullptr)
        return false;

    const uint32_t nDataSize = strlen(pExpression) + strlen("return ") + 2;
    if (nDataSize > nDebExpBufferSize)
    {
        //    pDebExpBuffer = (char *)RESIZE(pDebExpBuffer,nDataSize);
        auto *const newPtr = new char[nDataSize];
        memcpy(newPtr, pDebExpBuffer, nDebExpBufferSize);
        delete[] pDebExpBuffer;
        pDebExpBuffer = newPtr;
        nDebExpBufferSize = nDataSize;
    }
    sprintf_s(pDebExpBuffer, nDebExpBufferSize, "return %s;", pExpression);
    return ProcessDebugExpression0(pDebExpBuffer, Result);
}

bool COMPILER::SetOnDebugExpression(const char *pLValue, const char *pRValue, DATA &Result)
{
    if (pLValue == nullptr || pRValue == nullptr)
        return false;

    const uint32_t nDataSize = strlen(pLValue) + strlen(pRValue) + 5;
    if (nDataSize > nDebExpBufferSize)
    {
        // pDebExpBuffer = (char *)RESIZE(pDebExpBuffer,nDataSize);
        auto *const newPtr = new char[nDataSize];
        memcpy(newPtr, pDebExpBuffer, nDebExpBufferSize);
        delete[] pDebExpBuffer;
        pDebExpBuffer = newPtr;
        nDebExpBufferSize = nDataSize;
    }
    sprintf_s(pDebExpBuffer, nDataSize, "%s = %s;", pLValue, pRValue);
    return ProcessDebugExpression0(pDebExpBuffer, Result);
}

bool COMPILER::ProcessDebugExpression0(const char *pExpression, DATA &Result)
{
    SEGMENT_DESC Segment;
    STRINGS_LIST DbgLocalSL;
    bool bRes;

    DATA *pResult = nullptr;

    if (pExpression == nullptr)
        return false;

    bDebugExpressionRun = true;

    Segment = {};
    Segment.name = "Debug Expression";
    Segment.id = RunningSegmentID;
    Segment.bUnload = false;
    Segment.pData = nullptr;
    Segment.pCode = nullptr;
    Segment.BCode_Program_size = 0;
    Segment.BCode_Buffer_size = 0;
    Segment.Files_list = &DbgLocalSL;
    Segment.Files_list->SetStringDataSize(sizeof(OFFSET_INFO));

    if (pRun_fi)
    {
        CurrentFuncCode = FuncTab.FindFunc(pRun_fi->info->name);
    }

    try
    {
        bRes = Compile(Segment, pDebExpBuffer, strlen(pDebExpBuffer) + 1);
    }
    catch (...)
    {
        return false;
    }

    if (!bRes)
    {
        delete[] Segment.pCode;
        bDebugExpressionRun = false;
        return false;
    }

    // DWORD mem_CurrentFuncCode;

    // save current pointers values
    const uint32_t mem_InstructionPointer = InstructionPointer;
    // mem_ip = ip;
    FuncFrame *mem_pfi = pRun_fi;
    const char *mem_codebase = pRunCodeBase;
    // mem_CurrentFuncCode = CurrentFuncCode;

    // trace("Segment.pCode : %s",pDebExpBuffer);

    // DWORD old_data_num;
    // old_data_num = SStack.GetDataNum(); trace("old_data_num: %d",old_data_num);

    const uint32_t stackN = SStack.GetDataNum();

    try
    {
        bRes = BC_Execute(INVALID_FUNC_CODE, pResult, Segment.pCode);
    }
    // old_data_num = SStack.GetDataNum(); trace("new_data_num: %d",old_data_num);

    catch (...)
    {
        bRes = false;
    }

    // restore
    pRun_fi = mem_pfi;
    InstructionPointer = mem_InstructionPointer;
    // ip = mem_ip;
    // RunningSegmentID = pRun_fi->segment_id;
    pRunCodeBase = mem_codebase;

    delete[] Segment.pCode;

    if (pResult)
    {
        Result.Copy(pResult);
        // SStack.Pop();
    }
    SStack.InvalidateFrom(stackN);

    bDebugExpressionRun = false;
    return bRes;
}

void COMPILER::ProcessFrame(uint32_t DeltaTime)
{
    if (core_internal.Timer.Ring)
        AddRuntimeEvent();

    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (!SegmentTable[n].bUnload)
            continue;
        // unload segment of program
        delete[] SegmentTable[n].pData;
        delete[] SegmentTable[n].pCode;
        delete SegmentTable[n].Files_list;
        SegmentTable[n].Files_list = nullptr;

        for (uint32_t i = n; i < (SegmentsNum - 1); i++)
        {
            SegmentTable[i] = SegmentTable[i + 1];
        }
        SegmentsNum--;
        // SegmentTable = (SEGMENT_DESC *)RESIZE(SegmentTable,SegmentsNum*sizeof(SEGMENT_DESC));
        SegmentTable.resize(SegmentsNum);
        // reset search
        n = 0;
    }

    EventTab.ProcessFrame();

    for (int32_t ln = 0; ln < static_cast<int32_t>(EventMsg.GetClassesNum()); ln++)
    {
        S_EVENTMSG *pMsg = EventMsg.Read(ln);
        if (pMsg->bInvalide)
            continue;
        if (pMsg->bProcess == false)
        {
            pMsg->ProcessTime(DeltaTime);
            continue;
        }
        if (pMsg->ProcessTime(DeltaTime))
        {
            pMsg->Invalidate();
            ProcessEvent(pMsg->pEventName, pMsg->pMessageClass);
            // EventMsg.Del(ln);
            // ln--;
            /*ProcessEvent(pMsg->pEventName,pMsg->pMessageClass);
            EventMsg.Del(ln);
            ln--;*/
        }
    }
    EventMsg.RemoveInvalidated();

    PrintoutUsage();
}

void COMPILER::ResizeBCodeBuffer(SEGMENT_DESC &Segment, uint32_t add_size)
{
    // geometric growth: segments compile to megabytes of bytecode
    const uint64_t needed = static_cast<uint64_t>(Segment.BCode_Program_size) + add_size;
    if (needed <= Segment.BCode_Buffer_size)
        return;
    uint64_t capacity = std::max<uint64_t>(Segment.BCode_Buffer_size * 2ull, BCODE_BUFFER_BLOCKSIZE);
    while (capacity < needed)
        capacity *= 2;
    auto *const newPtr = new char[capacity];
    if (Segment.pCode)
        memcpy(newPtr, Segment.pCode, Segment.BCode_Program_size);
    delete[] Segment.pCode;
    Segment.pCode = newPtr;
    Segment.BCode_Buffer_size = static_cast<uint32_t>(capacity);
}

void COMPILER::CompileToken(SEGMENT_DESC &Segment, S_TOKEN_TYPE Token_type, uint32_t data_blocks_num, ...)
{
    uint32_t n;
    va_list args;

    // DTrace("token: %s : %s",Token.GetTypeName(Token_type),Token.GetData());

    // write instruction (1 byte) ----------------------------------------------
    uint32_t write_size = 1;
    ResizeBCodeBuffer(Segment, write_size);
    Segment.pCode[Segment.BCode_Program_size] = Token_type;
    Segment.BCode_Program_size += write_size;
    //--------------------------------------------------------------------------

    if (data_blocks_num == 0)
    {
        // write byte length of token data -------------------------------------
        write_size = 1;
        ResizeBCodeBuffer(Segment, write_size);
        Segment.pCode[Segment.BCode_Program_size] = 0;
        Segment.BCode_Program_size += write_size;
        return;
    }

    // gather data blocks information and count total data size ----------------
    constexpr uint32_t kMaxBlocks = 8;
    const char *block_data[kMaxBlocks];
    uint32_t block_size[kMaxBlocks];
    Assert(data_blocks_num <= kMaxBlocks);
    va_start(args, data_blocks_num);
    write_size = 0;
    for (n = 0; n < data_blocks_num; n++)
    {
        block_data[n] = va_arg(args, char *);
        block_size[n] = va_arg(args, uint32_t);
        write_size += block_size[n];
    }
    va_end(args);
    //--------------------------------------------------------------------------

    // write total length of token
    if (write_size < 0xff)
    {
        // write byte length of token data
        ResizeBCodeBuffer(Segment, 1);
        Segment.pCode[Segment.BCode_Program_size] = static_cast<uint8_t>(write_size);
        Segment.BCode_Program_size += 1;
    }
    else
    {
        // write uint32_t length of token data
        ResizeBCodeBuffer(Segment, sizeof(uint32_t) + 1);
        Segment.pCode[Segment.BCode_Program_size] = static_cast<uint8_t>(0xff);
        Segment.BCode_Program_size += 1;
        memcpy(&Segment.pCode[Segment.BCode_Program_size], &write_size, sizeof(uint32_t));
        Segment.BCode_Program_size += sizeof(uint32_t);
    }
    //--------------------------------------------------------------------------

    // write token data blocks
    ResizeBCodeBuffer(Segment, write_size);
    for (n = 0; n < data_blocks_num; n++)
    {
        memcpy(&Segment.pCode[Segment.BCode_Program_size], block_data[n], block_size[n]);
        Segment.BCode_Program_size += block_size[n];
    }
}

bool COMPILER::InitInternalFunctions()
{
    FuncInfo fi;

    // constants the engine defines for scripts (CL uses them)
    for (const auto &[name, value] : {std::pair<const char *, int32_t>{"INT_MAX", INT32_MAX}, {"INT_MIN", INT32_MIN}})
    {
        DEFINFO di;
        di.name = const_cast<char *>(name);
        di.segment_id = IMPORTED_SEGMENT_ID; // INTERNAL_SEGMENT_ID equals INVALID_SEGMENT_ID (unused entry)
        di.deftype = NUMBER;
        di.data4b = static_cast<uint32_t>(value);
        DefTab.AddDef(di);
    }

    // register internal functions ------------

    const uint32_t internal_functions_num = GetIntFunctionsNum();
    for (uint32_t n = 0; n < internal_functions_num; n++)
    {
        fi.segment_id = INTERNAL_SEGMENT_ID;
        // fi.name = FuncNameTable[n];
        fi.name = IntFuncTable[n].pName;
        fi.offset = n;
        fi.arguments = GetInternalFunctionArgumentsNum(n);
        fi.decl_file_name = "engine";
        fi.decl_line = 0;
        fi.return_type = GetIntFunctionReturnType(n);
        uint32_t func_code = FuncTab.AddFunc(fi);

        if (func_code == INVALID_FUNC_CODE)
        {
            func_code = FuncTab.FindFunc(fi.name);
            FuncTab.GetFunc(fi, func_code);
            // SetError("Duplicate function name: %s",fi.name);
            SetError("Function [%s] already declared in: %s line %d", fi.name.c_str(), fi.decl_file_name.c_str(),
                     fi.decl_line);
            return false;
        }
    }
    return true;
}

bool COMPILER::Compile(SEGMENT_DESC &Segment, char *pInternalCode, uint32_t pInternalCodeSize)
{
    char dbg_file_name[MAX_PATH];
    char file_name[MAX_PATH];
    char func_name[MAX_PATH];
    char var_name[MAX_PATH];
    char *pProgram;
    char *pApend_file;
    char *pSegmentSource;
    FuncInfo fi;
    VarInfo vi = {};
    const VarInfo *real_var = nullptr;
    LocalVarInfo lvi;
    uint32_t SegmentSize;
    uint32_t Program_size;
    uint32_t func_code;
    uint32_t var_code;
    uint32_t def_code;
    uint32_t inout;
    uint32_t bracket_inout;
    uint32_t n;
    uint32_t file_code;
    ptrdiff_t Control_offset;
    uint32_t Current_line;
    uint32_t Append_file_size;
    STRINGS_LIST BlockTable;
    S_TOKEN_TYPE Token_type;
    DEFINFO di;

    CompilerStage = CS_COMPILATION;

    RunningSegmentID = INVALID_SEGMENT_INDEX;

    script_cache_ = storm::ScriptCache();

    //    bool bCDStop;
    bool bFunctionBlock;
    bool bPass1Ok = true; // false after a recoverable declaration error: keep scanning, then fail
    int32_t lvalue;
    float fvalue;

    Control_offset = 0;
    strcpy_s(file_name, Segment.name.c_str());

    if (pInternalCode == nullptr)
    {
        auto is_new = Segment.Files_list->AddUnicalString(file_name);
        file_code = Segment.Files_list->GetStringCode(file_name);
        pProgram = nullptr;
        Program_size = 0;
        pSegmentSource = LoadFile(file_name, SegmentSize);
        if (is_new && script_cache_mode_ != kCacheDisabled)
        {
            script_cache_.files.emplace_back(file_name);
        }
        AppendProgram(pProgram, Program_size, pSegmentSource, SegmentSize, true);
        Segment.pData = pProgram;
        if (pProgram == nullptr)
        {
            SetError("file not found: %s", file_name);
            return false;
        }
        CurrentFuncCode = INVALID_FUNC_CODE;
    }
    else
    {
        pProgram = pInternalCode;
        Program_size = pInternalCodeSize;
        Segment.pData = pProgram;
    }

    // CurrentFuncCode = INVALID_FUNC_CODE;

    // register functions and variables ---------
    strcpy_s(func_name, "null");
    inout = 0;
    bracket_inout = 0;
    bFunctionBlock = false;
    // pProgram = Segment.pData;
    Token.SetProgram(pProgram, pProgram);
    if (bDebugInfo)
    {
        strcpy_s(DebugSourceFileName, Segment.name.c_str());
    }
    DebugSourceLine = DSL_INI_VALUE;
    bool bExtern;
    bool bImport;
    bExtern = false;
    bImport = false;
    bool bDotFlag;
    bDotFlag = false;
    // label detection: the two significant tokens before ':' and '?' still waiting for their ':'
    S_TOKEN_TYPE prevToken = SEPARATOR, prevToken2 = SEPARATOR;
    std::string prevText;
    uint32_t pendingQuestions = 0;
    std::unordered_map<std::string, uint32_t> declaredLocals; // pass 1: declarations per name
    bool resumed = false;                                    // see SkipInitializer
    LabelTable.Release(); // labels belong to a function of this segment
    appendBuffer_ = nullptr; // AppendProgram starts a new buffer
    appendCapacity_ = 0;
    compileErrors_ = 0;   // some compile paths report an error and carry on
    missingNatives_.clear();
    do
    {
        Token_type = Token.Get();
        bDotFlag = false;
        if (Token_type == DOT)
        {
            Token_type = Token.Get();
            bDotFlag = true;
        }
        switch (Token_type)
        {
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case FOR_BLOCK:
            if (inout > 0)
            {
                // for (type x : container): two hidden locals, the container and the position
                char *const at = Token.GetProgramControl();
                const uint32_t line = DebugSourceLine;
                S_TOKEN_TYPE loop_type = UNKNOWN;
                const bool range = Token.Get() == OPEN_BRACKET && PeekRangeFor(loop_type);
                Token.SetProgramControl(at);
                DebugSourceLine = line;
                if (range && func_code != INVALID_FUNC_CODE)
                {
                    LocalVarInfo hidden;
                    hidden.elements = 1;
                    hidden.type = loop_type == VAR_AREFERENCE ? VAR_AREFERENCE : VAR_REFERENCE;
                    hidden.name = LocalSlotName(kRangeContainer, declaredLocals[kRangeContainer]++);
                    FuncTab.AddFuncVar(func_code, hidden);
                    if (script_cache_mode_ != kCacheDisabled)
                        script_cache_.functions.back().local_variables.emplace_back(hidden);
                    hidden.type = VAR_INTEGER;
                    hidden.name = LocalSlotName(kRangePosition, declaredLocals[kRangePosition]++);
                    FuncTab.AddFuncVar(func_code, hidden);
                    if (script_cache_mode_ != kCacheDisabled)
                        script_cache_.functions.back().local_variables.emplace_back(hidden);
                }
            }
            break;
        case OP_QUESTION:
            pendingQuestions++;
            break;
        case LABEL:
            if (pendingQuestions > 0)
            {
                pendingQuestions--; // ':' of a conditional expression
                break;
            }
            // "name:" starting a statement; "default:" belongs to a switch
            if (inout > 0 && prevToken == UNKNOWN && !storm::iEquals(prevText, "default") &&
                (prevToken2 == SEPARATOR || prevToken2 == BLOCK_IN || prevToken2 == BLOCK_OUT ||
                 prevToken2 == LABEL))
            {
                if (!LabelTable.AddUnicalString(LabelKey(func_name, prevText.c_str()).c_str()))
                {
                    SetError("duplicate label: %s", prevText.c_str());
                    bPass1Ok = false;
                }
            }
            break;
        case CASE_COMMAND:
            // the value is an expression up to its ':'
            for (uint32_t depth = 0;;)
            {
                const S_TOKEN_TYPE t = Token.Get();
                if (t == END_OF_PROGRAMM)
                    break;
                if (t == DEBUG_LINEFEED)
                    DebugSourceLine++;
                else if (t == OPEN_BRACKET)
                    depth++;
                else if (t == CLOSE_BRACKET && depth > 0)
                    depth--;
                else if (t == OP_QUESTION)
                    pendingQuestions++;
                else if (t == LABEL && depth == 0)
                {
                    if (pendingQuestions == 0)
                        break;
                    pendingQuestions--;
                }
            }
            Token_type = LABEL;
            break;
        case SEPARATOR:
            if (bFunctionBlock && bExtern)
            {
                bFunctionBlock = false;
                bExtern = false;
            }
            if (bFunctionBlock && bImport)
            {
                bFunctionBlock = false;
                bImport = false;
            }
            break;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            break;
        case DEBUG_FILE_NAME:
            DebugSourceLine = DSL_INI_VALUE;
            strcpy_s(DebugSourceFileName, Token.GetData());
            break;
        case INCLUDE_LIBRIARY: {
            SCRIPT_LIBRIARY *pLib;
            VMA *pClass;

            //-----------------------------------------------------
            // check if already loaded
            auto name = std::string_view(Token.GetData());
            auto comparator = [&name](const auto &library) { return storm::iEquals(library.name, name); };

            if (std::ranges::find_if(LibriaryFuncs, comparator) != LibriaryFuncs.end())
            {
                break;
            }
            //-----------------------------------------------------

            pClass = core_internal.FindVMA(Token.GetData());
            if (!pClass)
            {
                SetWarning("cant load libriary '%s'", Token.GetData());
                break;
            }

            pLib = static_cast<SCRIPT_LIBRIARY *>(pClass->CreateClass());
            if (pLib)
            {
                std::vector<std::string> undeclared;
                undeclaredNatives_ = &undeclared;
                pLib->Init();
                undeclaredNatives_ = nullptr;
                if (!undeclared.empty())
                    spdlog::info("Script library '{}': {} natives not declared before it is loaded (bound if "
                                 "declared later): {}",
                                 Token.GetData(), undeclared.size(), fmt::join(undeclared, ", "));
            }

            LibriaryFuncs.emplace_back(pLib, Token.GetData());
            if (script_cache_mode_ != kCacheDisabled)
            {
                script_cache_.script_libs.emplace_back(Token.GetData());
            }

            break;
        }
        case INCLIDE_FILE: {
            // CL: "scripts\%language%\file.h" is the file of the current game language
            std::string include_name = Token.GetData();
            if (const auto at = include_name.find("%language%"); at != std::string::npos)
            {
                const std::string language = ScriptLanguageDirectory(true);
                if (language.empty())
                {
                    bPass1Ok = false;
                    break;
                }
                include_name.replace(at, strlen("%language%"), language);
            }
            if (Segment.Files_list->AddUnicalString(include_name.c_str()))
            {
                file_code = Segment.Files_list->GetStringCode(include_name.c_str());
                Control_offset = Token.GetProgramControl() - Token.GetProgramBase(); // store program scan point
                pApend_file = LoadFile(include_name.c_str(), Append_file_size);
                if (pApend_file == nullptr)
                {
                    SetError("can't load file: %s", include_name.c_str());
                    return false;
                }
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.files.emplace_back(include_name.c_str());
                }
                if (bDebugInfo)
                {
                    uint32_t aps;
                    strcpy_s(dbg_file_name, "#file \"");
                    strcat_s(dbg_file_name, include_name.c_str());
                    strcat_s(dbg_file_name, "\";");
                    aps = strlen(dbg_file_name);
                    char *pTempS;
                    pTempS = new char[aps + 1];
                    strcpy_s(pTempS, aps + 1, dbg_file_name);
                    AppendProgram(pProgram, Program_size, pTempS, aps, false);
                }
                AppendProgram(pProgram, Program_size, pApend_file, Append_file_size, true);
                Segment.pData = pProgram;
                Token.SetProgram(pProgram, pProgram + Control_offset);
            }
            break;
        }
        case DEFINE_COMMAND: // create define
            // DefineTable
            if (inout == 0)
            {
                strcpy_s(var_name, Token.GetData());
                di.name = var_name;
                di.segment_id = Segment.id;
                Token.Get();
                di.deftype = Token.GetType();
                bool bMinus = false;
                if (Token.GetType() == OP_MINUS)
                {
                    bMinus = true;
                    Token.Get();
                    di.deftype = Token.GetType();
                }
                di.data4b = {};
                if (Token.GetType() == TRUE_CONST || Token.GetType() == FALSE_CONST)
                {
                    // CL: #define X true
                    lvalue = Token.GetType() == TRUE_CONST;
                    di.deftype = NUMBER;
                    memcpy(&di.data4b, &lvalue, sizeof(int32_t));
                }
                switch (Token.GetType())
                {
                case NUMBER:
                    lvalue = ParseIntLiteral(Token.GetData());
                    if (bMinus)
                        lvalue = -lvalue;
                    memcpy(&di.data4b, &lvalue, sizeof(int32_t));
                    break;
                case FLOAT_NUMBER:
                    fvalue = static_cast<float>(atof(Token.GetData()));
                    if (bMinus)
                        fvalue = -fvalue;
                    memcpy(&di.data4b, &fvalue, sizeof(float));
                    break;
                case STRING: {
                    const auto len = strlen(Token.GetData()) + 1;
                    di.data4b = (uintptr_t) new char[len];
                    memcpy((void *)di.data4b, Token.GetData(), len);
                    break;
                }
                case OP_MINUS:

                    break;
                }
                def_code = DefTab.AddDef(di);
                if (def_code == INVALID_DEF_CODE)
                {
                    SetError("define redefinition: %s", di.name);
                    bPass1Ok = false;
                    break;
                }
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.defines.emplace_back(storm::script_cache::Define{di.name, di.deftype, di.data4b});
                }
            }
            break;

            /*case DEFINE_COMMAND:    // create define
              if(inout == 0)
              {
                strcpy_s(var_name,Token.GetData());
                Token.Get();
                if(Token.GetType() == NUMBER || Token.GetType() == FLOAT_NUMBER || Token.GetType() == STRING)
                {
                  vi.type = Token.GetType();
                  vi.elements = 1;
                  vi.name = var_name;
                  vi.segment_id = Segment.id;
                  var_code = VarTab.AddVar(vi);
                  if(var_code == INVALID_VAR_CODE)
                  {
                    SetError("Duplicate define name: %s",vi.name);
                    return false;
                  }
                  VarTab.GetVarX(vi,var_code);
                  vi.pDClass->Set(Token.GetData());
                  vi.pDClass->Convert(Token.GetType());
                }
              }
            break;*/
        case EXTERN:
            Token_type = Token.Get();
            bExtern = true;
        case IMPORT:
            if (!bExtern)
            {
                Token_type = Token.Get();
                bImport = true;
            }
        case VAR_AREFERENCE:
        case VAR_REFERENCE:
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_FREF:
        case TVOID:
            if (inout == 0)
            {
                if (!bFunctionBlock)
                {
                    if (Token.GetData() && strcmp(Token.GetData(), "[") == 0)
                    {
                        // "type []Name(": a function returning an array
                        if (Token.Get() != SQUARE_CLOSE_BRACKET || Token.Get() != UNKNOWN)
                        {
                            SetError("function name expected after 'type []'");
                            bPass1Ok = false;
                            break;
                        }
                    }
                    Token.Get();
                    if (Token.GetType() == OPEN_BRACKET)
                    {
                        // this is function header
                        Token.StepBack();
                        Token.StepBack();
                        Token.Get();           // function name
                        bFunctionBlock = true; // we are stepped into func area

                        strcpy_s(func_name, Token.GetData());

                        fi.segment_id = Segment.id;
                        if (bExtern)
                            fi.offset = INVALID_FUNC_OFFSET;
                        else if (bImport)
                            fi.offset = INVALID_FUNC_OFFSET;
                        else
                            fi.offset = Segment.BCode_Program_size;
                        fi.name = func_name;
                        fi.return_type = Token_type;
                        fi.arguments = 0;
                        fi.decl_file_name = DebugSourceFileName;
                        fi.decl_line = DebugSourceLine;
                        fi.imported_func = nullptr;
                        declaredLocals.clear();
                        fi.native = bImport;
                        fi.varargs = false;
                        fi.default_args.clear();
                        func_code = FuncTab.AddFunc(fi);

                        if (func_code == INVALID_FUNC_CODE)
                        {
                            const auto existing = FuncTab.FindFunc(fi.name);
                            const S_TOKEN_TYPE declared_return = fi.return_type;
                            const std::string declared_name = fi.name;
                            FuncTab.GetFuncX(fi, existing);
                            if (bImport && fi.segment_id == INTERNAL_SEGMENT_ID)
                            {
                                if (!ReturnTypesMatch(declared_return, fi.return_type))
                                {
                                    SetError("native declaration of built-in function '%s' returns %s, the engine's "
                                             "returns %s",
                                             declared_name.c_str(), Token.GetTypeName(declared_return),
                                             Token.GetTypeName(fi.return_type));
                                    bPass1Ok = false;
                                }
                                // a script may declare a built-in function as native (CL: Program\internal.c)
                                FuncInfo declared;
                                if (!ScanParameterList(INVALID_FUNC_CODE, false, declared))
                                    bPass1Ok = false;
                                else if (!IsIntFuncVarArgsNum(existing) &&
                                         (declared.arguments != fi.arguments || declared.varargs))
                                {
                                    SetError("native declaration of built-in function '%s' takes %u arguments, the "
                                             "engine's takes %u",
                                             declared_name.c_str(), declared.arguments, fi.arguments);
                                    bPass1Ok = false;
                                }
                                break;
                            }
                            SetError("Function [%s] already declared in: %s line %d", fi.name.c_str(),
                                     fi.decl_file_name.c_str(), fi.decl_line);
                            bPass1Ok = false; // its arguments and locals go nowhere (invalid func_code)
                            FuncInfo ignored;
                            ScanParameterList(INVALID_FUNC_CODE, false, ignored);
                            break;
                        }
                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            script_cache_.functions.emplace_back(storm::script_cache::Function{
                                fi, std::vector<storm::script_cache::FunctionLocalVariable>{}});
                        }
                        FuncInfo declared;
                        if (!ScanParameterList(func_code, bExtern, declared))
                            bPass1Ok = false;
                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            script_cache_.functions.back().info.default_args = declared.default_args;
                            script_cache_.functions.back().info.varargs = declared.varargs;
                        }
                        FuncTab.SetFuncSignature(func_code, std::move(declared.default_args), declared.varargs);
                        if (bImport)
                        {
                            // a library loaded earlier registered it before this declaration
                            if (auto pending = pendingNatives_.find(LowerName(fi.name)); pending != pendingNatives_.end())
                            {
                                IFUNCINFO info = pending->second;
                                pendingNatives_.erase(pending);
                                if (SetScriptFunction(&info) == INVALID_FUNCHANDLE)
                                    bPass1Ok = false;
                            }
                        }
                        break;
                    }
                    Token.StepBack();

                    // global variable
                    Token.StepBack();
                    do
                    {
                        Token.Get();
                        if (Token.GetData() == nullptr)
                        {
                            SetError("Invalid variable name");
                            return false;
                        }
                        vi.name = Token.GetData();
                        vi.segment_id = Segment.id;
                        vi.elements = 1;
                        vi.array = false;
                        vi.type = Token_type;
                        Token.Get();
                        const bool sized = Token.GetType() == SQUARE_OPEN_BRACKET && Token.Get() != SQUARE_CLOSE_BRACKET;
                        if (Token.GetType() == SQUARE_CLOSE_BRACKET)
                        {
                            vi.elements = kRuntimeArraySize; // "string a[];": sized by SetArraySize or @a = n
                            vi.array = true;
                            Token.Get();
                        }
                        else if (sized)
                        {
                            if (Token.GetType() != NUMBER)
                            {
                                def_code = DefTab.FindDef(Token.GetData());
                                if (def_code != INVALID_DEF_CODE)
                                {
                                    DefTab.GetDef(di, def_code);
                                    if (di.deftype == NUMBER)
                                    {
                                        lvalue = di.data4b;
                                    }
                                    else
                                    {
                                        SetError("Invalid array (%s) size", vi.name.c_str());
                                        return false;
                                    }
                                }
                                else
                                {
                                    SetError("Invalid array (%s) size", vi.name.c_str());
                                    return false;
                                }
                            }
                            else
                                lvalue = ParseIntLiteral(Token.GetData());
                            vi.elements = lvalue;
                            vi.array = true;
                            Token.Get(); // SQUARE_CLOSE_BRACKET
                            Token.Get();
                        }
                        var_code = VarTab.AddVar(vi);
                        if (var_code == INVALID_VAR_CODE)
                        {
                            SetError("Duplicate variable name: %s", vi.name.c_str());
                            bPass1Ok = false;
                            continue;
                        }

                        real_var = VarTab.GetVarX(var_code);
                        if (real_var == nullptr)
                        {
                            SetError("Registered variable %s has invalid code", vi.name.c_str());
                            return false;
                        }

                        bool bNeg;
                        if (Token.GetType() == OP_EQUAL)
                        {
                            // global var initialization
                            if (vi.elements > 1)
                            {
                                // array
                                if (SkipAuxiliaryTokens() != BLOCK_IN)
                                {
                                    SetError("Invalid array '%s' initialization", vi.name.c_str());
                                    return false;
                                }
                                uint32_t aindex;
                                // bool bNeg;
                                aindex = 0;

                                SkipAuxiliaryTokens();
                                while (Token.GetType() != END_OF_PROGRAMM)
                                {
                                    if (Token.GetType() == OP_MINUS)
                                    {
                                        bNeg = true;
                                        Token.Get();
                                    }
                                    else
                                        bNeg = false;
                                    if (Token.GetType() == BLOCK_OUT)
                                        break;
                                    if (Token.GetType() == END_OF_PROGRAMM)
                                        break;

                                    switch (Token.GetType())
                                    {
                                    case TRUE_CONST:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(1);
                                        break;
                                    case FALSE_CONST:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(0);
                                        break;

                                    case NUMBER:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        if (bNeg)
                                            real_var->value->Set(-ParseIntLiteral(Token.GetData()), aindex);
                                        else
                                            real_var->value->Set(ParseIntLiteral(Token.GetData()), aindex);
                                        aindex++;
                                        break;
                                    case FLOAT_NUMBER:
                                        if (vi.type != VAR_FLOAT)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        if (bNeg)
                                            real_var->value->Set(-static_cast<float>(atof(Token.GetData())), aindex);
                                        else
                                            real_var->value->Set(static_cast<float>(atof(Token.GetData())), aindex);
                                        aindex++;
                                        break;
                                    case STRING:
                                        if (vi.type != VAR_STRING)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(Token.GetData(), aindex);
                                        aindex++;
                                        break;
                                    case UNKNOWN:

                                        break;
                                    default:
                                        break;
                                    }
                                    SkipAuxiliaryTokens();
                                    if (Token.GetType() == BLOCK_OUT)
                                        break;
                                    if (Token.GetType() != COMMA)
                                    {
                                        SetError("Invalid array '%s' initialization parameters list", vi.name.c_str());
                                        return false;
                                    }
                                    SkipAuxiliaryTokens();
                                }
                                SkipAuxiliaryTokens();
                            }
                            else if (vi.type == VAR_OBJECT)
                            {
                                if (NextScanToken() != BLOCK_IN ||
                                    !ScanObjectLiteral(real_var->value->GetAClass()))
                                {
                                    SetError("Invalid object '%s' initialization", vi.name.c_str());
                                    bPass1Ok = false;
                                }
                                Token.Get();
                            }
                            else
                            {
                                // single variable
                                if (Token.Get() == OP_MINUS)
                                {
                                    bNeg = true;
                                    Token.Get();
                                }
                                else
                                    bNeg = false;

                                switch (Token.GetType())
                                {
                                case TRUE_CONST:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    real_var->value->Set(1);
                                    break;
                                case FALSE_CONST:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    real_var->value->Set(0);
                                    break;
                                case NUMBER:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    if (bNeg)
                                        real_var->value->Set(-ParseIntLiteral(Token.GetData()));
                                    else
                                        real_var->value->Set(ParseIntLiteral(Token.GetData()));
                                    break;
                                case FLOAT_NUMBER:
                                    if (vi.type != VAR_FLOAT)
                                        break;
                                    if (bNeg)
                                        real_var->value->Set(-static_cast<float>(atof(Token.GetData())));
                                    else
                                        real_var->value->Set(static_cast<float>(atof(Token.GetData())));
                                    break;
                                case STRING:
                                    if (vi.type != VAR_STRING)
                                        break;
                                    real_var->value->Set(Token.GetData());
                                    break;
                                case UNKNOWN:

                                    break;
                                default:
                                    break;
                                }
                                Token.Get();
                            }
                        }

                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            auto &cached_var = script_cache_.variables.emplace_back(vi);
                            cached_var.value = std::make_unique<DATA>();
                            *cached_var.value = *real_var->value;
                        }
                    } while (Token.GetType() == COMMA);
                    Token.StepBack();
                    break;
                }
                // ScanParameterList consumed the parameter list after the function name
                SetError("Invalid function declaration");
                bPass1Ok = false;
            }
            else
            {
                // local variables: a name declared again (sibling blocks, two for loops) gets its own
                // slot, pass 2 binds each declaration to it in the same order
                if (Token.TypeCast() || !IsIdentifier(Token.GetData()))
                    break; // a type used as a function-style cast, e.g. int(x)
                lvi.type = Token_type;
                Token.StepBack();
                resumed = false;
                bool first_declarator = true;
                do
                {
                    Token.Get();
                    if (!first_declarator && Token.GetType() != UNKNOWN)
                    {
                        resumed = true; // "int i, ref x": the main loop reads the next declaration
                        Token.StepBack();
                        break;
                    }
                    first_declarator = false;
                    if (!IsIdentifier(Token.GetData()))
                    {
                        SetError("Invalid variable name");
                        bPass1Ok = false;
                        break;
                    }
                    lvi.name = Token.GetData();
                    lvi.elements = 1;
                    lvi.array = false;
                    Token.Get();
                    if (Token.GetType() == SQUARE_OPEN_BRACKET)
                    {
                        lvi.elements = ScanLocalArraySize();
                        lvi.array = true;
                    }
                    if (Token.GetType() == OP_EQUAL)
                    {
                        const uint32_t items = SkipInitializer(resumed);
                        if (lvi.elements == 0)
                            lvi.elements = items;
                    }
                    lvi.name = LocalSlotName(lvi.name, declaredLocals[LowerName(lvi.name)]++);
                    if (func_code != INVALID_FUNC_CODE && !FuncTab.AddFuncVar(func_code, lvi))
                    {
                        SetError("Duplicate variable name: %s", lvi.name.c_str());
                        bPass1Ok = false;
                    }

                    if (script_cache_mode_ != kCacheDisabled)
                    {
                        script_cache_.functions.back().local_variables.emplace_back(lvi);
                    }
                } while (!resumed && Token.GetType() == COMMA);
                if (!resumed)
                    Token.StepBack();
            }
            break;

        case BLOCK_IN:
            inout++;
            break;
        case BLOCK_OUT:
            inout--;
            if (inout == 0)
                bFunctionBlock = false; // sign if we are out of function
            break;
        default:
            break;
        }
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
        case COMMENT:
        case DEBUG_FILE_NAME:
        case INCLIDE_FILE:
            break;
        default:
            prevToken2 = prevToken;
            prevToken = Token_type;
            if (Token_type == UNKNOWN)
                prevText = Token.GetData() ? Token.GetData() : "";
            break;
        }
    } while (Token_type != END_OF_PROGRAMM);

    if (inout != 0)
    {
        SetError("Missing '}' in segment");
        return false;
    }

    //-----------------------------------------

    Current_line = 1;
    // Current_line = DSL_INI_VALUE;
    DebugSourceLine = DSL_INI_VALUE;
    uint32_t fnsize;

    if (bDebugInfo)
    {
        strcpy_s(DebugSourceFileName, Segment.name.c_str());
        fnsize = Segment.name.length();
        CompileToken(Segment, DEBUG_FILE_NAME, 3, (char *)&DebugSourceLine, sizeof(uint32_t), (char *)&fnsize,
                     sizeof(uint32_t), Segment.name.c_str(), fnsize);
        CompileToken(Segment, DEBUG_LINE_CODE, 1, (char *)&DebugSourceLine, sizeof(uint32_t));
        // DebugSourceLine++;
    }

    uint32_t block_io_code;
    block_io_code = 0;
    inout = 0;
    bracket_inout = 0;
    bFunctionBlock = false;
    pProgram = Segment.pData;
    Token.SetProgram(pProgram, pProgram);

    STRINGS_LIST BreakUpdateTable;
    // After an error, resume at the next top-level declaration so that one run reports every error.
    bool compiled = bPass1Ok;
    while (!CompileBlock(Segment, bFunctionBlock, inout, TVOID, INVALID_OFFSET, INVALID_OFFSET, BreakUpdateTable))
    {
        compiled = false;
        if (!SkipToTopLevel(inout))
            break;
        bFunctionBlock = false;
        inout = 0;
        BreakUpdateTable.Release();
    }
    if (!compiled || compileErrors_ != 0)
        return false;
    DOUBLE_DWORD ddw;
    uint32_t label_offset;
    for (n = 0; n < LabelUpdateTable.GetStringsCount(); n++)
    {
        LabelUpdateTable.GetStringData(n, &ddw);
        LabelTable.GetStringData(ddw.dw1, &label_offset);
        memcpy(&Segment.pCode[ddw.dw2], &label_offset, sizeof(uint32_t));
    }

    if (pInternalCode == nullptr)
    {
        delete[] Segment.pData;
        Segment.pData = nullptr;
    }
    HANDLE fh;
    uint32_t dwR;
    if (bWriteCodeFile)
    {
        auto fName = std::filesystem::path(Segment.name.c_str()).filename().string();
        strcpy_s(file_name, fName.c_str());
        strcat_s(file_name, ".b");
        auto fileS = fio->_CreateFile(file_name, std::ios::binary | std::ios::out);
        if (fileS.is_open())
        {
            fio->_WriteFile(fileS, Segment.pCode, Segment.BCode_Program_size);
            fio->_CloseFile(fileS);
        }
    }
    if (script_cache_mode_ != kCacheDisabled)
    {
        SaveSegmentToCache(Segment);
    }
    return true;
}

bool COMPILER::IsIdentifier(const char *text)
{
    // ASCII letters, '_' and UTF-8 letters (CL has a Cyrillic parameter name)
    return text != nullptr && (std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_' ||
                               static_cast<unsigned char>(text[0]) >= 0x80);
}

std::string COMPILER::LowerName(const std::string &name)
{
    std::string lower(name);
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

std::string COMPILER::LocalSlotName(const std::string &name, uint32_t occurrence)
{
    // the first declaration keeps the plain name (arguments, legacy function-wide lookups)
    return occurrence == 0 ? name : name + "#" + std::to_string(occurrence);
}

std::string COMPILER::LabelKey(const std::string &func_name, const char *label)
{
    return func_name + ":" + label;
}

uint32_t COMPILER::ScanLocalArraySize()
{
    // pass 1, on '[': a constant size, 0 for "[]" (sized by its initializer) or
    // kRuntimeArraySize for a size computed at run time; leaves Token after ']'
    uint32_t size = kRuntimeArraySize;
    S_TOKEN_TYPE t = Token.Get();
    if (t == SQUARE_CLOSE_BRACKET)
    {
        Token.Get();
        return 0;
    }
    if (const auto constant = ConstantArraySize(); constant != 0)
    {
        if (Token.Get() == SQUARE_CLOSE_BRACKET)
        {
            Token.Get();
            return constant;
        }
    }
    for (uint32_t depth = 0; t != END_OF_PROGRAMM; t = Token.Get())
    {
        if (t == DEBUG_LINEFEED)
            DebugSourceLine++;
        else if (t == SQUARE_OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET && depth-- == 0)
            break;
    }
    Token.Get();
    return size;
}

uint32_t COMPILER::ConstantArraySize()
{
    // the current token as an array size: a number or a numeric #define; 0 otherwise
    if (Token.GetType() == NUMBER)
        return static_cast<uint32_t>(ParseIntLiteral(Token.GetData()));
    if (Token.GetType() == UNKNOWN && Token.GetData())
    {
        if (const auto def_code = DefTab.FindDef(Token.GetData()); def_code != INVALID_DEF_CODE)
        {
            DEFINFO di;
            DefTab.GetDef(di, def_code);
            if (di.deftype == NUMBER)
                return static_cast<uint32_t>(di.data4b);
        }
    }
    return 0;
}

uint32_t COMPILER::SkipInitializer(bool &resumed)
{
    resumed = false;
    // pass 1, on '=' of a declaration. A {...} list is skipped and its items counted; a plain
    // expression is skipped only up to the ',' of a following declarator. Otherwise (';', or a
    // missing ';' before the next statement, which old scripts have) the scan resumes right
    // after '=' as it always did.
    char *const start = Token.GetProgramControl();
    const uint32_t start_line = DebugSourceLine;
    uint32_t depth = 0, items = 0;
    bool empty = true;
    S_TOKEN_TYPE t = Token.Get();
    while (t == DEBUG_LINEFEED || t == COMMENT)
    {
        DebugSourceLine += t == DEBUG_LINEFEED ? 1 : Token.TokenLines();
        t = Token.Get();
    }
    const bool list = t == BLOCK_IN;
    if (!list)
        depth = 0;
    for (;; t = Token.Get())
    {
        switch (t)
        {
        case END_OF_PROGRAMM:
            return items;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            continue;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            continue;
        case BLOCK_IN:
        case OPEN_BRACKET:
        case SQUARE_OPEN_BRACKET:
            if (list && depth == 1 && empty)
            {
                items++;
                empty = false;
            }
            if (t == BLOCK_IN && !list)
                break; // a block: the ';' is missing
            depth++;
            continue;
        case BLOCK_OUT:
        case CLOSE_BRACKET:
        case SQUARE_CLOSE_BRACKET:
            if (depth == 0)
                break;
            depth--;
            if (list && depth == 0)
            {
                Token.Get(); // the ',' or ';' after the list
                return items;
            }
            continue;
        case COMMA:
            if (depth == 0)
                return items; // next declarator
            if (depth == 1 && list)
                empty = true;
            continue;
        case IF_BLOCK:
        case ELSE_BLOCK:
        case FOR_BLOCK:
        case WHILE_BLOCK:
        case SWITCH_COMMAND:
        case FUNCTION_RETURN:
        case SEPARATOR:
            if (depth == 0 && !list)
                break;
            continue;
        default:
            if (list && depth == 1 && empty)
            {
                items++;
                empty = false;
            }
            continue;
        }
        // not a list and no following declarator: resume after '='
        Token.SetProgramControl(start);
        DebugSourceLine = start_line;
        resumed = true;
        return 0;
    }
}

bool COMPILER::BindPassedArguments(const FuncInfo &fi, FuncFrame &frame, uint32_t passed_args)
{
    // fewer arguments: the declared defaults fill the rest; more: the extra ones of a "..." function
    // move off the stack into the frame, where GetArg finds them
    if (passed_args < frame.arguments)
    {
        const auto defaults = static_cast<uint32_t>(fi.default_args.size());
        if (frame.arguments - passed_args > defaults)
        {
            SetError("function '%s' called with %u arguments, needs at least %u", fi.name.c_str(), passed_args,
                     frame.arguments - defaults);
            return false;
        }
        for (uint32_t n = passed_args; n < frame.arguments; n++)
        {
            const auto &value = fi.default_args[n - (frame.arguments - defaults)];
            DATA *pV = SStack.Push();
            switch (value.type)
            {
            case NUMBER:
                pV->Set(value.int_value);
                break;
            case FLOAT_NUMBER:
                pV->Set(value.float_value);
                break;
            case STRING:
                pV->Set(value.string_value);
                break;
            case VAR_FREF:
                pV->ClearType();
                pV->SetFref(value.string_value);
                break;
            default: // nullptr for a ref or aref parameter
                pV->SetType(fi.local_vars[n].type);
                break;
            }
        }
        return true;
    }
    if (!fi.varargs)
    {
        SetError("function '%s' called with %u arguments, takes %u", fi.name.c_str(), passed_args, frame.arguments);
        return false;
    }
    const uint32_t extra = passed_args - frame.arguments;
    const uint32_t first = SStack.GetDataNum() - extra;
    frame.varargs.resize(extra);
    for (uint32_t n = 0; n < extra; n++)
    {
        frame.varargs[n].SetVCompiler(this);
        frame.varargs[n].Copy(SStack.Read(first, n));
    }
    for (uint32_t n = 0; n < extra; n++)
        SStack.Pop();
    return true;
}

const std::string &COMPILER::CurrentFunctionName() const
{
    static const std::string none;
    const auto *fi = FuncTab.GetFuncInfo(CurrentFuncCode);
    return fi ? fi->name : none;
}

bool COMPILER::ReturnTypesMatch(S_TOKEN_TYPE declared, S_TOKEN_TYPE engine)
{
    // engine UNKNOWN: returns values of several types (GetEventData); ptr and int are one value
    auto same = [](S_TOKEN_TYPE t) { return t == VAR_PTR ? VAR_INTEGER : t; };
    return engine == UNKNOWN || same(declared) == same(engine);
}

DATA *COMPILER::Deref(DATA *pV)
{
    // the value a stack entry stands for; nullptr for a null reference (without an error)
    if (pV->Data_type != VAR_REFERENCE)
        return pV;
    return pV->pReference ? pV->pReference->GetVarPointer() : nullptr;
}

int32_t COMPILER::IntValue(DATA *pV)
{
    DATA *value = Deref(pV);
    if (value == nullptr)
    {
        SetError("integer operation on a null reference");
        return 0;
    }
    switch (value->GetType())
    {
    case VAR_INTEGER:
        return value->GetInt();
    case VAR_FLOAT:
        return static_cast<int32_t>(value->GetFloat());
    case VAR_STRING:
        return static_cast<int32_t>(strtoll(value->GetString(), nullptr, 10));
    default:
        SetError("integer operation on a %s value", Token.GetTypeName(value->GetType()));
        return 0;
    }
}

ATTRIBUTES *COMPILER::AttributesOf(DATA *pV)
{
    if (pV->GetType() == VAR_AREFERENCE)
        return pV->AttributesClass; // null for a missing attribute
    DATA *value = Deref(pV);
    if (value == nullptr || value->IsArray())
        return nullptr;
    if (value->GetType() == VAR_AREFERENCE)
        return value->AttributesClass;
    if (value->GetType() == VAR_OBJECT)
        return value->GetAClass();
    return nullptr;
}

int32_t COMPILER::TypeidOf(DATA *pV)
{
    DATA *value = Deref(pV);
    if (value == nullptr)
        return kTypeidRef;
    switch (value->GetType())
    {
    case VAR_INTEGER:
    case VAR_PTR:
        return kTypeidInt;
    case VAR_FLOAT:
        return kTypeidFloat;
    case VAR_STRING:
        return kTypeidString;
    case VAR_OBJECT:
        return kTypeidObject;
    case VAR_AREFERENCE:
        return kTypeidAref;
    case VAR_FREF:
        return kTypeidFref;
    default:
        return kTypeidRef;
    }
}

bool COMPILER::CastValue(DATA *pV, S_TOKEN_TYPE target)
{
    // replaces *pV (a stack value, possibly a reference) with its value converted to target;
    // TRUE_CONST stands for bool
    DATA *value = Deref(pV);
    if (target == TRUE_CONST)
    {
        const bool truth = value != nullptr && value->Truth();
        pV->ClearType();
        pV->Set(static_cast<int32_t>(truth));
        return true;
    }
    if (target == VAR_AREFERENCE)
    {
        // aref(object or reference): the attribute reference of its root
        ATTRIBUTES *attributes = value ? AttributesOf(value) : nullptr;
        pV->ClearType();
        pV->SetType(VAR_AREFERENCE);
        pV->SetAReference(attributes);
        return true;
    }
    if (target == VAR_FREF && (value == nullptr || value->IsNull()))
    {
        pV->ClearType();
        pV->SetFref({});
        return true;
    }
    if (value == nullptr)
    {
        SetError("%s(nullptr)", Token.GetTypeName(target));
        return false;
    }
    DATA result;
    result.SetVCompiler(this);
    if (value->GetType() == VAR_AREFERENCE)
        result.Set(value->GetAClass() ? to_string(value->GetAClass()->GetThisAttr()) : std::string());
    else
        result.Copy(value);
    if (!result.Convert(target))
        return false;
    pV->ClearType();
    pV->Copy(&result);
    return true;
}

uint32_t COMPILER::FindMethod(DATA *self, const char *method)
{
    // object->Method: the object's "classtable" attribute names a global object whose attribute
    // Method names the function
    ATTRIBUTES *object = self ? AttributesOf(self) : nullptr;
    ATTRIBUTES *table_name = object ? object->GetAttributeClass("classtable") : nullptr;
    if (!table_name)
    {
        SetError("->%s: the object has no class table", method);
        return INVALID_FUNC_CODE;
    }
    const std::string table = to_string(table_name->GetThisAttr());
    const auto *var = VarTab.GetVar(VarTab.FindVar(table));
    ATTRIBUTES *entry = var && var->value->GetAClass() ? var->value->GetAClass()->GetAttributeClass(method) : nullptr;
    if (!entry)
    {
        SetError("->%s: class table '%s' has no such method", method, table.c_str());
        return INVALID_FUNC_CODE;
    }
    const std::string function = to_string(entry->GetThisAttr());
    const auto func_code = FuncTab.FindFunc(function);
    if (func_code == INVALID_FUNC_CODE)
        SetError("->%s: function '%s' not found", method, function.c_str());
    return func_code;
}

S_TOKEN_TYPE COMPILER::NextScanToken()
{
    // pass 1: the next significant token, counting source lines
    for (;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == DEBUG_LINEFEED)
            DebugSourceLine++;
        else if (t == COMMENT)
            DebugSourceLine += Token.TokenLines();
        else
            return t;
    }
}

bool COMPILER::ScanObjectLiteral(ATTRIBUTES *object)
{
    // pass 1, after '{' of a global object's initializer: constant values only
    if (object == nullptr)
        return false;
    for (;;)
    {
        S_TOKEN_TYPE t = NextScanToken();
        if (t == BLOCK_OUT)
            return true;
        if (!((t == UNKNOWN || t == STRING) && Token.GetData()))
            return false;
        std::string key = Token.GetData();
        if (NextScanToken() != OP_EQUAL)
            return false;
        bool negative = false;
        t = NextScanToken();
        if (t == OP_MINUS)
        {
            negative = true;
            t = Token.Get();
        }
        std::string value = Token.GetData() ? Token.GetData() : "";
        switch (t)
        {
        case BLOCK_IN:
            if (!ScanObjectLiteral(object->CreateSubAClass(object, key.c_str())))
                return false;
            break;
        case STRING:
            object->SetAttribute(key, value.c_str());
            break;
        case NUMBER:
            value = std::to_string(negative ? -ParseIntLiteral(value.c_str()) : ParseIntLiteral(value.c_str()));
            object->SetAttribute(key, value.c_str());
            break;
        case FLOAT_NUMBER:
            sprintf_s(gs, "%.7f", (negative ? -1.0f : 1.0f) * static_cast<float>(atof(value.c_str())));
            object->SetAttribute(key, gs);
            break;
        case TRUE_CONST:
        case FALSE_CONST:
            object->SetAttribute(key, t == TRUE_CONST ? "1" : "0");
            break;
        case AND: // &Function: a function reference is the function's name
            if (Token.Get() != UNKNOWN || !Token.GetData())
                return false;
            object->SetAttribute(key, Token.GetData());
            break;
        case UNKNOWN: {
            // a define's value, or a global object by name (classtable)
            const auto def_code = DefTab.FindDef(value.c_str());
            if (def_code != INVALID_DEF_CODE)
            {
                DEFINFO di;
                DefTab.GetDef(di, def_code);
                if (di.deftype == STRING)
                    value = reinterpret_cast<const char *>(di.data4b);
                else if (di.deftype == NUMBER)
                    value = std::to_string(static_cast<int32_t>(di.data4b));
                else
                    return false;
            }
            else if (VarTab.FindVar(value) == INVALID_VAR_CODE)
                return false;
            object->SetAttribute(key, value.c_str());
            break;
        }
        default:
            return false;
        }
        t = NextScanToken();
        if (t == BLOCK_OUT)
            return true;
        if (t != COMMA)
            return false;
    }
}

bool COMPILER::PeekRangeFor(S_TOKEN_TYPE &loop_type)
{
    // after "for (": a ':' before the closing ')' makes it a range-for; loop_type is the type of
    // the variable declared right before ':' (the caller restores the position)
    loop_type = UNKNOWN;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        switch (t)
        {
        case END_OF_PROGRAMM:
        case SEPARATOR:
        case BLOCK_IN:
            return false;
        case OPEN_BRACKET:
            depth++;
            break;
        case CLOSE_BRACKET:
            if (depth-- == 0)
                return false;
            break;
        case LABEL:
            return depth == 0 && loop_type != UNKNOWN;
        case VAR_INTEGER:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_FREF:
            if (depth == 0)
                loop_type = t;
            break;
        default:
            break;
        }
    }
}

bool COMPILER::CompileRangeFor(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE loop_type,
                               STRINGS_LIST &BreakUpdateTable)
{
    // for ([type index,] type item : container) body
    //   aref item: the container's attributes; otherwise the elements of the array it refers to
    const uint32_t depth = inout + 1;
    uint32_t index_slot = INVALID_VAR_CODE;
    uint32_t item_slot = INVALID_VAR_CODE;
    for (;;)
    {
        const S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
        if (t == LABEL)
            break;
        if (t == COMMA)
            continue;
        if (Token.TypeCast() || !IsIdentifier(Token.GetData()))
        {
            SetError("invalid range-for variable");
            return false;
        }
        if (item_slot != INVALID_VAR_CODE)
        {
            if (index_slot != INVALID_VAR_CODE)
            {
                SetError("range-for takes an index and an item variable");
                return false;
            }
            index_slot = item_slot;
        }
        item_slot = BindLocal(Token.GetData(), depth);
        if (item_slot == INVALID_VAR_CODE)
        {
            SetError("Undeclared identifier: %s", Token.GetData());
            return false;
        }
    }
    const uint32_t container = BindLocal(kRangeContainer, depth);
    const uint32_t position = BindLocal(kRangePosition, depth);
    if (container == INVALID_VAR_CODE || position == INVALID_VAR_CODE)
    {
        SetError("range-for: hidden locals missing");
        return false;
    }

    // container
    uint32_t var_code;
    S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
    const bool address = t == AND;
    if (address)
        t = Token.Get();
    const S_TOKEN_TYPE var_type = t == UNKNOWN ? DetectUnknown(var_code) : UNKNOWN;
    if (loop_type != VAR_AREFERENCE)
    {
        // an array variable, by reference
        if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        {
            SetError("range-for over elements needs an array variable");
            return false;
        }
        CompileToken(Segment, SETREF);
        CompileToken(Segment, LOCAL_VARIABLE, 1, &container, sizeof(uint32_t));
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
    }
    else
    {
        if ((var_type == VARIABLE || var_type == LOCAL_VARIABLE) && IsPlainAttributePath())
        {
            if (!CompileAttributeRoot(Segment))
                return false;
        }
        else
        {
            Token.StepBack();
            if (address)
                Token.StepBack();
            if (!CompileExpression(Segment))
                return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, LOCAL_VARIABLE, 1, &container, sizeof(uint32_t));
    }
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after the range-for container");
        return false;
    }
    // position = 0
    const int32_t zero = 0;
    CompileToken(Segment, STACK_PUSH);
    CompileToken(Segment, NUMBER, 1, &zero, sizeof(int32_t));
    CompileToken(Segment, STACK_POP);
    CompileToken(Segment, LOCAL_VARIABLE, 1, &position, sizeof(uint32_t));

    const uint32_t loop = Segment.BCode_Program_size;
    CompileToken(Segment, RANGE_NEXT, 4, &container, sizeof(uint32_t), &position, sizeof(uint32_t), &item_slot,
                 sizeof(uint32_t), &index_slot, sizeof(uint32_t));
    uint32_t jump = 0;
    const uint32_t to_end = Segment.BCode_Program_size + 2;
    CompileToken(Segment, JUMP_Z, 1, &jump, sizeof(uint32_t));

    if (CompileAuxiliaryTokens(Segment) == BLOCK_IN)
    {
        inout++;
        if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, loop, 6, BreakUpdateTable))
            return false;
    }
    else
    {
        Token.StepBack();
        if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, loop, 6, BreakUpdateTable))
            return false;
        CloseLocalScopes(inout);
    }
    CompileToken(Segment, JUMP, 1, &loop, sizeof(uint32_t));
    memcpy(&Segment.pCode[to_end], &Segment.BCode_Program_size, sizeof(uint32_t));
    UpdateOffsets(Segment, BreakUpdateTable, Segment.BCode_Program_size, "b");
    UpdateOffsets(Segment, BreakUpdateTable, loop, "c");
    return true;
}

bool COMPILER::AssignValue(DATA *destination, DATA *value)
{
    // destination = value, converting the value to the destination's type as MOVE does
    if (!destination)
    {
        SetError("assignment through a null reference");
        return false;
    }
    DATA *source = value->GetVarPointer();
    if (source && source->GetType() != destination->GetType())
    {
        ExpressionResult.ClearType();
        ExpressionResult.Copy(value);
        if (ExpressionResult.IsReference())
            ExpressionResult.RefConvert();
        if (!ExpressionResult.Convert(destination->GetType()))
            return false;
        source = ExpressionResult.GetVarPointer();
    }
    return destination->Copy(source);
}

uint32_t COMPILER::FindLocal(const char *name) const
{
    for (auto it = localScopes_.rbegin(); it != localScopes_.rend(); ++it)
        if (storm::iEquals(it->name, name))
            return it->slot;
    // arguments, and a local used after the block that declared it (function-wide, as before)
    return FuncTab.FindVar(CurrentFuncCode, name);
}

uint32_t COMPILER::BindLocal(const char *name, uint32_t depth)
{
    const auto slot = FuncTab.FindVar(CurrentFuncCode, LocalSlotName(name, localDeclarations_[LowerName(name)]++));
    if (slot != INVALID_VAR_CODE)
        localScopes_.push_back({name, slot, depth});
    return slot;
}

void COMPILER::CloseLocalScopes(uint32_t depth)
{
    while (!localScopes_.empty() && localScopes_.back().depth > depth)
        localScopes_.pop_back();
}

bool COMPILER::CompileLocalDeclarator(SEGMENT_DESC &Segment, uint32_t depth, bool first)
{
    // pass 2, on the name of a local declaration ("int a" or the "b" of "int a, b"). Plain
    // declarations compile exactly as before; arrays sized at run time and array initializers
    // compile here.
    const char *name = Token.GetData();
    if (Token.TypeCast() || !IsIdentifier(name))
    {
        SetError("variable name expected after type");
        return false;
    }
    const std::string var_name = name;
    const uint32_t slot = BindLocal(name, depth);
    if (slot == INVALID_VAR_CODE)
    {
        SetError("Undeclared identifier: %s", var_name.c_str());
        return false;
    }
    declListActive_ = true;
    char *const after_name = Token.GetProgramControl();
    S_TOKEN_TYPE next = Token.Get();
    bool array = false;
    if (next == SQUARE_OPEN_BRACKET)
    {
        char *const after_open = Token.GetProgramControl();
        next = Token.Get();
        if (next == SQUARE_CLOSE_BRACKET)
            array = true; // sized by its initializer (pass 1)
        else if (ConstantArraySize() != 0 && Token.Get() == SQUARE_CLOSE_BRACKET)
        {
            array = true;
            char *const after_close = Token.GetProgramControl();
            const bool initializer = Token.Get() == OP_EQUAL && Token.Get() == BLOCK_IN;
            Token.SetProgramControl(initializer ? after_close : after_name);
            if (!initializer)
            {
                // as before: the size compiles as a meaningless index expression
                if (!first)
                    CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
                return true;
            }
        }
        else
        {
            // sized at run time
            Token.SetProgramControl(after_open);
            if (!CompileExpression(Segment))
                return false;
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, SET_ARRAY_SIZE);
            CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
            array = true;
        }
        char *const after_array = Token.GetProgramControl();
        next = Token.Get();
        if (next == OP_EQUAL && Token.Get() == BLOCK_IN)
            return CompileArrayInitializer(Segment, LOCAL_VARIABLE, slot);
        Token.SetProgramControl(after_array);
        return true;
    }
    if (next == OP_EQUAL || !first)
        CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
    Token.StepBack();
    (void)array;
    return true;
}

bool COMPILER::CompileArrayInitializer(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code)
{
    // after "= {": element i = i-th expression
    for (uint32_t index = 0;; index++)
    {
        if (Token.Get() == BLOCK_OUT)
            return true; // empty list or trailing comma
        Token.StepBack();
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileToken(Segment, STACK_PUSH);
        CompileToken(Segment, NUMBER, 1, &index, sizeof(uint32_t));
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, LEFTOP_INDEX);
        if (!CompileExpression(Segment))
            return false;
        CompileToken(Segment, MOVE);
        CompileToken(Segment, LEFT_OPERAND);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, STACK_POP_VOID);
        if (Token.GetType() == BLOCK_OUT)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in array initializer");
            return false;
        }
    }
}

bool COMPILER::ScanParameterList(uint32_t func_code, bool bExtern, FuncInfo &declared)
{
    // pass 1, after the name in a function header: registers the arguments up to the matching ')'.
    // declared receives the count, the values of defaulted trailing arguments and "...".
    declared.arguments = 0;
    declared.varargs = false;
    declared.default_args.clear();
    LocalVarInfo lvi;
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("Invalid function declaration: '(' expected");
        return false;
    }
    for (;;)
    {
        S_TOKEN_TYPE type = Token.Get();
        switch (type)
        {
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            continue;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            continue;
        case COMMA:
            continue;
        case CLOSE_BRACKET:
            return true;
        case DOT:
            if (Token.Get() != DOT || Token.Get() != DOT || declared.varargs)
            {
                SetError("Invalid function declaration: '...' expected");
                return false;
            }
            declared.varargs = true;
            continue;
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_FREF:
            break;
        default:
            SetError("Invalid function declaration: '%s'", Token.GetData() ? Token.GetData() : "");
            return false;
        }
        if (declared.varargs)
        {
            SetError("Invalid function declaration: '...' must be the last parameter");
            return false;
        }
        const char *name = Token.GetData();
        if (Token.TypeCast() || !IsIdentifier(name))
        {
            SetError("Invalid function declaration: parameter name expected");
            return false;
        }
        lvi.type = type;
        lvi.name = name;
        lvi.elements = 1;
        declared.arguments++;
        if (func_code != INVALID_FUNC_CODE)
        {
            FuncTab.AddFuncArg(func_code, lvi, bExtern);
            if (script_cache_mode_ != kCacheDisabled)
                script_cache_.functions.back().arguments.emplace_back(
                    storm::script_cache::FunctionLocalVariable{lvi, bExtern});
        }
        Token.Get();
        if (Token.GetType() == OP_EQUAL)
        {
            DefaultArgument value;
            if (!ScanDefaultArgument(type, value))
                return false;
            declared.default_args.push_back(std::move(value));
            Token.Get();
        }
        else if (!declared.default_args.empty())
        {
            SetError("Invalid function declaration: '%s' follows a parameter with a default value", lvi.name.c_str());
            return false;
        }
        if (Token.GetType() == CLOSE_BRACKET)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("Invalid function declaration: ',' or ')' expected after '%s'", lvi.name.c_str());
            return false;
        }
    }
}

bool COMPILER::ScanDefaultArgument(S_TOKEN_TYPE param_type, DefaultArgument &value)
{
    // a constant: [-]number, string, true/false, a #define, or nullptr for ref/aref parameters
    bool negative = false;
    S_TOKEN_TYPE type = Token.Get();
    if (type == OP_MINUS)
    {
        negative = true;
        type = Token.Get();
    }
    const char *text = Token.GetData() ? Token.GetData() : "";
    DEFINFO di;
    switch (type)
    {
    case NUMBER:
        value.type = NUMBER;
        value.int_value = ParseIntLiteral(text);
        break;
    case FLOAT_NUMBER:
        value.type = FLOAT_NUMBER;
        value.float_value = static_cast<float>(atof(text));
        break;
    case STRING:
        value.type = STRING;
        value.string_value = text;
        break;
    case TRUE_CONST:
    case FALSE_CONST:
        value.type = NUMBER;
        value.int_value = type == TRUE_CONST;
        break;
    case VAR_FREF: // fref(nullptr)
        if (Token.TypeCast() && Token.Get() == OPEN_BRACKET && Token.Get() == UNKNOWN &&
            storm::iEquals(Token.GetData(), "nullptr") && Token.Get() == CLOSE_BRACKET && param_type == VAR_FREF)
        {
            value.type = VAR_FREF;
            return true;
        }
        break;
    case AND: // &Function
        if (Token.Get() == UNKNOWN && Token.GetData() && param_type == VAR_FREF)
        {
            value.type = VAR_FREF;
            value.string_value = Token.GetData();
            return true;
        }
        break;
    case UNKNOWN:
        if (storm::iEquals(text, "nullptr") && !negative &&
            (param_type == VAR_REFERENCE || param_type == VAR_AREFERENCE || param_type == VAR_FREF))
        {
            value.type = param_type == VAR_FREF ? VAR_FREF : VAR_REFERENCE;
            return true;
        }
        if (const auto def_code = DefTab.FindDef(text); def_code != INVALID_DEF_CODE)
        {
            DefTab.GetDef(di, def_code);
            value.type = static_cast<S_TOKEN_TYPE>(di.deftype);
            if (di.deftype == NUMBER)
                std::memcpy(&value.int_value, &di.data4b, sizeof(int32_t));
            else if (di.deftype == FLOAT_NUMBER)
                std::memcpy(&value.float_value, &di.data4b, sizeof(float));
            else if (di.deftype == STRING && !negative)
                value.string_value = reinterpret_cast<const char *>(di.data4b);
            else
                break;
            break;
        }
        [[fallthrough]];
    default:
        SetError("unsupported default value '%s%s'", negative ? "-" : "", text);
        return false;
    }
    if (negative)
    {
        if (value.type == NUMBER)
            value.int_value = -value.int_value;
        else if (value.type == FLOAT_NUMBER)
            value.float_value = -value.float_value;
        else
        {
            SetError("unsupported default value '-%s'", text);
            return false;
        }
    }
    return true;
}

int32_t COMPILER::ParseIntLiteral(const char *text)
{
    // decimal (a leading 0 is not octal) or 0x hexadecimal; an optional u suffix; wraps to 32 bits
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        return static_cast<int32_t>(static_cast<uint32_t>(strtoull(text + 2, nullptr, 16)));
    return static_cast<int32_t>(atoll(text));
}

bool COMPILER::CompileCallArguments(SEGMENT_DESC &Segment, uint32_t &func_args)
{
    // after the '(' of a call: each argument leaves its value on the stack; stops on ')'
    func_args = 0;
    if (Token.Get() == CLOSE_BRACKET)
        return true;
    Token.StepBack();
    for (;;)
    {
        if (!CompileExpression(Segment))
        {
            SetError("invalid function argument");
            return false;
        }
        func_args++;
        if (Token.GetType() == CLOSE_BRACKET)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("invalid function argument");
            return false;
        }
    }
}

bool COMPILER::CheckCallArguments(uint32_t func_code, uint32_t func_args, bool in_expression)
{
    const FuncInfo *fi = FuncTab.GetFuncInfo(func_code);
    if (fi == nullptr)
    {
        SetError("function not found");
        return false;
    }
    if (fi->segment_id == IMPORTED_SEGMENT_ID)
        return true; // provided by an engine library
    if (fi->segment_id == INTERNAL_SEGMENT_ID)
    {
        if (fi->arguments == func_args || IsIntFuncVarArgsNum(func_code))
            return true;
        if (in_expression)
        {
            SetWarning("function '%s(args:%d)' called with %d arguments", fi->name.c_str(), fi->arguments, func_args);
            return true;
        }
        SetError("function '%s(args:%d)' doesnt accept %d arguments", fi->name.c_str(), fi->arguments, func_args);
        return false;
    }
    uint32_t expected = fi->arguments;
    if (fi->native)
    {
        if (!ReportMissingNative(fi->name))
            return false;
    }
    else if (fi->offset == INVALID_FUNC_OFFSET)
        expected = fi->extern_arguments; // external function declared but not compiled yet
    const auto defaults = static_cast<uint32_t>(fi->default_args.size());
    const uint32_t required = expected > defaults ? expected - defaults : 0;
    if (func_args >= required && (fi->varargs || func_args <= expected))
        return true;
    if (in_expression)
    {
        SetWarning("function %s(args:%d) called with %d arguments", fi->name.c_str(), expected, func_args);
        return true;
    }
    SetError("function %s(args:%d) doesnt accept %d arguments", fi->name.c_str(), expected, func_args);
    return false;
}

bool COMPILER::ReportMissingNative(const std::string &name)
{
    if (!missingNativesAreErrors_ && !missingNatives_.insert(name).second)
        return true;
    if (!missingNativesAreErrors_)
    {
        // the host check links no engine library: listed separately, not a script error
        if (diagnosticsSink_)
            diagnosticsSink_->emplace_back("MISSING NATIVE in " + std::string(DebugSourceFileName) + "(" +
                                           std::to_string(DebugSourceLine + 1) + "): " + name);
        return true;
    }
    SetError("native function '%s' is not implemented by the engine", name.c_str());
    return false;
}

bool COMPILER::SkipToTopLevel(uint32_t depth)
{
    // skip the rest of the statement (at top level) or of the function body the error occurred in
    for (;;)
    {
        switch (Token.Get())
        {
        case END_OF_PROGRAMM:
            return false;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            break;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case DEBUG_FILE_NAME:
            DebugSourceLine = DSL_INI_VALUE;
            if (Token.GetData())
                strcpy_s(DebugSourceFileName, Token.GetData());
            break;
        case BLOCK_IN:
            depth++;
            break;
        case BLOCK_OUT:
            if (depth > 0)
                depth--;
            if (depth == 0)
                return true;
            break;
        case SEPARATOR:
            if (depth == 0)
                return true;
            break;
        default:
            break;
        }
    }
}

S_TOKEN_TYPE COMPILER::SkipAuxiliaryTokens()
{
    S_TOKEN_TYPE Token_type;
    do
    {
        Token_type = Token.Get();
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
        case DEBUG_FILE_NAME:
        case COMMENT:
        case INCLIDE_FILE:
        case WHITESPACE:
            break;
        default:
            return Token_type;
        }
    } while (Token_type != END_OF_PROGRAMM);
    return Token_type;
}

S_TOKEN_TYPE COMPILER::CompileAuxiliaryTokens(SEGMENT_DESC &Segment) //, bool & bFunctionBlock, DWORD & inout)
{
    uint32_t fnsize;
    S_TOKEN_TYPE Token_type;

    do
    {
        Token_type = Token.Get();
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
            if (!bDebugInfo)
                break;
            DebugSourceLine++;
            CompileToken(Segment, DEBUG_LINE_CODE, 1, (char *)&DebugSourceLine, sizeof(uint32_t));
            break;
        case DEBUG_FILE_NAME:
            if (!bDebugInfo)
                break;
            DebugSourceLine = DSL_INI_VALUE;
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid DBG_FILE_NAME");
                return DEBUG_FILE_NAME;
            }
            strcpy_s(DebugSourceFileName, Token.GetData());
            fnsize = strlen(Token.GetData());

            CompileToken(Segment, DEBUG_FILE_NAME, 3, (char *)&DebugSourceLine, sizeof(uint32_t), (char *)&fnsize,
                         sizeof(uint32_t), Token.GetData(), strlen(Token.GetData()));
            break;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case INCLIDE_FILE:
        case WHITESPACE:
            // skip compiling of auxiliary tokens
            break;
        default:
            return Token_type;
        }
    } while (Token_type != END_OF_PROGRAMM);
    return Token_type;
}

void COMPILER::CopyOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &srclist, STRINGS_LIST &dstlist, const char *sname)
{
    uint32_t n;
    uint32_t i;
    uint32_t jump_voffset;
    if (sname == nullptr)
    {
        for (n = 0; n < srclist.GetStringsCount(); n++)
        {
            srclist.GetStringData(n, (char *)&jump_voffset);

            i = dstlist.GetStringsCount();
            dstlist.AddString("c");
            dstlist.SetStringData(i, (char *)&jump_voffset);
        }
        return;
    }

    for (n = 0; n < srclist.GetStringsCount(); n++)
    {
        if (strcmp(sname, srclist.GetString(n)) != 0)
            continue;

        srclist.GetStringData(n, (char *)&jump_voffset);

        i = dstlist.GetStringsCount();
        dstlist.AddString("c");
        dstlist.SetStringData(i, (char *)&jump_voffset);
    }
}

void COMPILER::UpdateOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &list, uint32_t offset, const char *sname)
{
    uint32_t n;
    uint32_t jump_voffset;
    if (sname == nullptr)
    {
        for (n = 0; n < list.GetStringsCount(); n++)
        {
            list.GetStringData(n, (char *)&jump_voffset);
            memcpy(&Segment.pCode[jump_voffset], &offset, sizeof(uint32_t));
        }
        return;
    }
    for (n = 0; n < list.GetStringsCount(); n++)
    {
        if (strcmp(sname, list.GetString(n)) != 0)
            continue;
        list.GetStringData(n, (char *)&jump_voffset);
        memcpy(&Segment.pCode[jump_voffset], &offset, sizeof(uint32_t));
    }
}

bool COMPILER::CompileBlock(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE bound_type,
                            uint32_t continue_jump, uint32_t break_offset, STRINGS_LIST &BreakTable)
{
    S_TOKEN_TYPE Token_type;
    //    S_TOKEN_TYPE Block_bound;
    //    DWORD fnsize;
    uint32_t awcode;
    uint32_t var_code;
    uint32_t balance_bracket;
    uint32_t balance_block;
    uint32_t jump_offset;
    uint32_t update_offset;
    uint32_t vuint32_t;
    uint32_t n;
    uint32_t unk_code;
    uint32_t func_args;
    STRINGS_LIST BreakUpdateTable;
    BreakUpdateTable.SetStringDataSize(sizeof(uint32_t));
    bool bExtern;
    bool bImport;
    int32_t lvalue;
    uint32_t def_code;
    DEFINFO di;
    uint32_t dwRCode;

    const VarInfo *real_var;
    LocalVarInfo lvi;

    jump_offset = 0xffbadbad;

    balance_block = 0;
    balance_bracket = 0;

    switch (bound_type)
    {
    case CLOSE_BRACKET:
        balance_bracket = 1;
        break;
    case BLOCK_OUT:
        balance_block = 1;
        break;
    }

    bExtern = false;
    bImport = false;
    do
    {
        // Token_type = Token.Get();
        Token_type = CompileAuxiliaryTokens(Segment /*,bFunctionBlock,inout*/);

        switch (Token_type)
        {
        case EVENT_HANDLER:
            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("Invalid event handler");
                return false;
            }

            if (Token.Get() == STRING)
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                strcpy_s(gs, Token.GetData());
            }
            else
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                def_code = DefTab.FindDef(Token.GetData());
                if (def_code != INVALID_DEF_CODE)
                {
                    DefTab.GetDef(di, def_code);
                    if (di.deftype != STRING)
                    {
                        SetError("Invalid event handler");
                        return false;
                    }
                    if (di.data4b == 0)
                    {
                        SetError("Invalid define");
                        return false;
                    }
                    strcpy_s(gs, (char *)di.data4b);
                }
                else
                {
                    SetError("Invalid event handler");
                    return false;
                }
            }
            if (Token.Get() != COMMA)
            {
                SetError("Invalid event handler");
                return false;
            }
            if (Token.Get() == STRING)
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                // SetEventHandler(gs,Token.GetData(),1,true);
                SetEventHandler(gs, Token.GetData(), 0, true);
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.event_handlers.emplace_back(storm::script_cache::EventHandler{gs, Token.GetData()});
                }
            }
            else
            {
                SetError("Invalid event handler");
                return false;
            }

            if (Token.Get() != CLOSE_BRACKET)
            {
                SetError("Invalid event handler");
                return false;
            }
            break;

        case GOTO_COMMAND:
            // CompileToken(Segment,Token_type,1,Token.GetData(),strlen(Token.GetData())+1);
            // LabelUpdateTable
            Token.Get(); // label name
            DOUBLE_DWORD ddw;
            ddw.dw1 = LabelTable.GetStringCode(LabelKey(CurrentFunctionName(), Token.GetData()).c_str());
            if (ddw.dw1 == INVALID_ORDINAL_NUMBER)
            {
                SetError("goto: no label '%s' in this function", Token.GetData());
                return false;
            }
            n = LabelUpdateTable.GetStringsCount();
            LabelUpdateTable.AddString("j");
            ddw.dw2 = Segment.BCode_Program_size + 2;
            LabelUpdateTable.SetStringData(n, (char *)&ddw);
            vuint32_t = 0xffffffff;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            break;
        case BREAK_COMMAND:
            if (break_offset == INVALID_OFFSET)
            {
                SetError("invalid break");
                return false;
            }
            vuint32_t = Segment.BCode_Program_size + 2;
            n = BreakTable.GetStringsCount();
            BreakTable.AddString("b");
            BreakTable.SetStringData(n, (char *)&vuint32_t);
            vuint32_t = INVALID_OFFSET;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            if (bound_type == BREAK_COMMAND)
            {
                return true;
            }
            break;
        case CONTINUE_COMMAND:
            if (continue_jump == INVALID_OFFSET)
            {
                SetError("invalid continue");
                return false;
            }
            vuint32_t = Segment.BCode_Program_size + 2;
            n = BreakTable.GetStringsCount();
            BreakTable.AddString("c");
            BreakTable.SetStringData(n, (char *)&vuint32_t);
            vuint32_t = INVALID_OFFSET;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            // CompileToken(Segment,JUMP,1,(char *)&continue_jump,sizeof(uint32_t));
            break;
        case SEPARATOR:
            CompileToken(Segment, Token_type);
            declListActive_ = declExpectName_ = false;
            if (bound_type == SEPARATOR)
            {
                return true;
            }
            break;
        case OPEN_BRACKET:
            balance_bracket++;
            CompileToken(Segment, Token_type);
            break;
        case CLOSE_BRACKET:
            balance_bracket--;
            CompileToken(Segment, Token_type);
            if (bound_type == CLOSE_BRACKET)
            {
                if (balance_bracket == 0)
                {
                    return true;
                }
            }
            break;
        case BLOCK_IN:
            inout++;
            balance_block++;
            break;
        case BLOCK_OUT:
            if (bound_type == BREAK_COMMAND && balance_block == 0)
            {
                Token.StepBack(); // the switch's '}' ends the last case (no break): the switch closes it
                return true;
            }

            if (inout == 0)
            {
                SetError("syntax error");
                return false;
            }
            inout--;
            CloseLocalScopes(inout);
            declListActive_ = declExpectName_ = false;

            if (balance_block == 0)
            {
                switch (bound_type)
                {
                case CLOSE_BRACKET:
                    SetError("missed ')'");
                    break;
                case BLOCK_OUT:
                    SetError("missed '}'");
                    break;
                default:
                    SetError("syntax error");
                    break;
                }
                return false;
            }
            balance_block--;
            if (inout == 0)
            {
                CompileToken(Segment, FUNCTION_RETURN_VOID);
                bFunctionBlock = false; // sign if we are out of function
                break;
            }
            if (bound_type == BLOCK_OUT)
            {
                if (balance_block == 0)
                {
                    return true;
                }
            }
            break;
            /*
                case IF_BLOCK:
                  CompileToken(Segment,Token_type);
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type != OPEN_BRACKET){SetError("missed '('"); return false;}
                  CompileToken(Segment,OPEN_BRACKET);
                  if(!CompileBlock(Segment,bFunctionBlock,inout,CLOSE_BRACKET,INVALID_OFFSET,INVALID_OFFSET,BreakTable))
               return false; CompileToken(Segment,PUSH_EXPRESULT);    // store for possible else

                  update_offset = Segment.BCode_Program_size + 2;
                  CompileToken(Segment,JUMP_Z,1,(char *)&jump_offset,sizeof(uint32_t));
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type == BLOCK_IN)
                  {
                    inout++;
                    if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,continue_jump,break_offset,BreakTable))
               return false;
                  }
                  else
                  {
                    Token.StepBack();
                    if(!CompileBlock(Segment,bFunctionBlock,inout,SEPARATOR,continue_jump,break_offset,BreakTable))
               return false;
                  }
                  memcpy(&Segment.pCode[update_offset],&Segment.BCode_Program_size,sizeof(uint32_t));
                  CompileToken(Segment,POP_EXPRESULT);    // pop
                break;
                case ELSE_BLOCK:
                  update_offset = Segment.BCode_Program_size + 2;
                  CompileToken(Segment,JUMP_NZ,1,(char *)&jump_offset,sizeof(uint32_t));
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type == BLOCK_IN)
                  {
                    inout++;
                    if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,continue_jump,break_offset,BreakTable))
               return false;
                  }
                  else
                  {
                    Token.StepBack();
                    if(!CompileBlock(Segment,bFunctionBlock,inout,SEPARATOR,continue_jump,break_offset,BreakTable))
               return false;
                  }
                  memcpy(&Segment.pCode[update_offset],&Segment.BCode_Program_size,sizeof(uint32_t));
                break;
                case WHILE_BLOCK:
                  BreakUpdateTable.Release();
                  jump_offset = Segment.BCode_Program_size;
                  CompileToken(Segment,Token_type);
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type != OPEN_BRACKET){SetError("missed '('"); return false;}
                  CompileToken(Segment,OPEN_BRACKET);
                  if(!CompileBlock(Segment,bFunctionBlock,inout,CLOSE_BRACKET,INVALID_OFFSET,INVALID_OFFSET,BreakUpdateTable))
               return false;

                  update_offset = Segment.BCode_Program_size + 2;
                  CompileToken(Segment,JUMP_Z,1,(char *)&jump_offset,sizeof(uint32_t));
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type == BLOCK_IN)
                  {
                    inout++;
                    if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,jump_offset,6,BreakUpdateTable)) return
               false;
                  }
                  else { SetError("missed '{'");    return false; }
                  CompileToken(Segment,JUMP,1,(char *)&jump_offset,sizeof(uint32_t));
                  memcpy(&Segment.pCode[update_offset],&Segment.BCode_Program_size,sizeof(uint32_t));
                  UpdateOffsets(Segment,BreakUpdateTable,Segment.BCode_Program_size,"b");
                  UpdateOffsets(Segment,BreakUpdateTable,jump_offset,"c");
                  BreakUpdateTable.Release();
                break;
                case FOR_BLOCK:
                  BreakUpdateTable.Release();

                  // '('
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type != OPEN_BRACKET){SetError("missed '('"); return false;}

                  // var = initial_value
                  if(!CompileBlock(Segment,bFunctionBlock,inout,SEPARATOR,INVALID_OFFSET,INVALID_OFFSET,BreakTable))
               return false;

                  jump_offset = Segment.BCode_Program_size;
                  // condition
                  CompileToken(Segment,PROCESS_EXPRESSION);
                  if(!CompileBlock(Segment,bFunctionBlock,inout,SEPARATOR,INVALID_OFFSET,INVALID_OFFSET,BreakTable))
               return false;
                  // jump if zero
                  update_offset = Segment.BCode_Program_size + 2;
                  CompileToken(Segment,JUMP_Z,1,(char *)&jump_offset,sizeof(uint32_t));


                  S_TOKEN_TYPE sttResult;    // var for inc/dec op
                  S_TOKEN_TYPE forOp;
                  if(Token.Get()!=UNKNOWN) {SetError("invalid 'for' syntax");return false;};

                  sttResult = DetectUnknown(var_code);
                  if(!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE)) {SetError("invalid 'for' syntax");return
               false;};

                  forOp = Token.Get();
                  if(!(forOp == OP_INC || forOp == OP_DEC)) {SetError("invalid 'for' syntax");return false;};
                  if(CompileAuxiliaryTokens(Segment) != CLOSE_BRACKET) {SetError("invalid 'for' syntax");return false;};

                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type == BLOCK_IN)
                  {
                    inout++;
                    if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,jump_offset,6,BreakUpdateTable)) return
               false;
                  }
                  else { SetError("missed '{'");    return false; }

                  DWORD forcont_offset;
                  forcont_offset = Segment.BCode_Program_size;
                  CompileToken(Segment,sttResult,1,(char *)&var_code,sizeof(uint32_t));
                  CompileToken(Segment,forOp);
                  CompileToken(Segment,JUMP,1,(char *)&jump_offset,sizeof(uint32_t));
                  memcpy(&Segment.pCode[update_offset],&Segment.BCode_Program_size,sizeof(uint32_t));
                  UpdateOffsets(Segment,BreakUpdateTable,Segment.BCode_Program_size,"b");
                  UpdateOffsets(Segment,BreakUpdateTable,forcont_offset,"c");
                  BreakUpdateTable.Release();
                break;
                case SWITCH_COMMAND:
                  BreakUpdateTable.Release();
                  CompileToken(Segment,Token_type);
                  Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type != OPEN_BRACKET){SetError("missed '('"); return false;}
                  CompileToken(Segment,OPEN_BRACKET);
                  if(!CompileBlock(Segment,bFunctionBlock,inout,CLOSE_BRACKET,INVALID_OFFSET,INVALID_OFFSET,BreakUpdateTable))
               return false; CompileToken(Segment,PUSH_EXPRESULT); Token_type = CompileAuxiliaryTokens(Segment);
                  if(Token_type == BLOCK_IN)
                  {
                    inout++;
                    //if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,jump_offset,6,BreakUpdateTable)) return
               false; if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,continue_jump,6,BreakUpdateTable)) return
               false;
                  }
                  else { SetError("missed '{'");    return false; }
                  UpdateOffsets(Segment,BreakUpdateTable,Segment.BCode_Program_size,"b");
                  CopyOffsets(Segment,BreakUpdateTable,BreakTable,"c");
                  //UpdateOffsets(Segment,BreakUpdateTable,continue_jump,"c");    // -> ***
                  CompileToken(Segment,POP_VOID);
                  BreakUpdateTable.Release();
                break;
                case CASE_COMMAND:
                  //CompileToken(Segment,Token_type);
                  Token_type = CompileAuxiliaryTokens(Segment);
                  CompileToken(Segment,PROCESS_EXPRESSION);
                  switch(Token_type)
                  {
                    case UNKNOWN:    CompileUnknown(Segment); break;
                    case NUMBER:    CompileNumber(Segment); break;
                    case FLOAT_NUMBER: CompileFloatNumber(Segment); break;
                    case STRING:    CompileString(Segment); break;
                    break;
                    default: SetError("invalid case"); return false;
                  }
                  if(Token.Get() != LABEL){ SetError("missed ':'");    return false; }
                  CompileToken(Segment,SEPARATOR);
                  CompileToken(Segment,STACK_COMPARE);

                  update_offset = Segment.BCode_Program_size + 2;
                  CompileToken(Segment,JUMP_Z,1,(char *)&jump_offset,sizeof(uint32_t));

                  //if(!CompileBlock(Segment,bFunctionBlock,inout,BREAK_COMMAND,INVALID_OFFSET,0,BreakTable)) return
               false;
                  // ??? why INVALID_OFFSET on continue_jump here? dont remember; anyway now change to normal
                  if(!CompileBlock(Segment,bFunctionBlock,inout,BREAK_COMMAND,continue_jump,0,BreakTable)) return false;

                  memcpy(&Segment.pCode[update_offset],&Segment.BCode_Program_size,sizeof(uint32_t));
                break;

            */
        case IF_BLOCK:

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            CompileExpression(Segment);

            if (Token.GetType() != CLOSE_BRACKET)
            {
                SetError("missed ')'");
                return false;
            }

            CompileToken(Segment, STACK_READ);
            CompileToken(Segment, EX);

            update_offset = Segment.BCode_Program_size + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, break_offset, BreakTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, continue_jump, break_offset, BreakTable))
                    return false;
            }
            memcpy(&Segment.pCode[update_offset], &Segment.BCode_Program_size, sizeof(uint32_t));
            // CompileToken(Segment,STACK_POP);
            // CompileToken(Segment,EX);

            CompileToken(Segment, POP_EXPRESULT); // pop

            if (bound_type == IF_BLOCK)
            {
                return true;
            }

            break;
        case ELSE_BLOCK:
            update_offset = Segment.BCode_Program_size + 2;
            CompileToken(Segment, JUMP_NZ, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment /*,bFunctionBlock,inout*/);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, break_offset, BreakTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, Token_type == IF_BLOCK ? IF_BLOCK : SEPARATOR,
                                  continue_jump, break_offset, BreakTable))
                    return false;
            }
            memcpy(&Segment.pCode[update_offset], &Segment.BCode_Program_size, sizeof(uint32_t));
            break;

        case WHILE_BLOCK:
            BreakUpdateTable.Release();
            jump_offset = Segment.BCode_Program_size;

            // CompileToken(Segment,Token_type);

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }
            // CompileToken(Segment,OPEN_BRACKET);
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);

            if (Token.GetType() != CLOSE_BRACKET)
            {
                SetError("missed ')'");
                return false;
            }

            // if(!CompileBlock(Segment,bFunctionBlock,inout,CLOSE_BRACKET,INVALID_OFFSET,INVALID_OFFSET,BreakUpdateTable))
            // return false;

            update_offset = Segment.BCode_Program_size + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, jump_offset, 6, BreakUpdateTable))
                    return false;
            }
            else
            {
                // SetError("missed '{'");    return false;
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, jump_offset, 6, BreakUpdateTable))
                    return false;
                // **** ?
            }
            CompileToken(Segment, JUMP, 1, (char *)&jump_offset, sizeof(uint32_t));
            memcpy(&Segment.pCode[update_offset], &Segment.BCode_Program_size, sizeof(uint32_t));
            UpdateOffsets(Segment, BreakUpdateTable, Segment.BCode_Program_size, "b");
            UpdateOffsets(Segment, BreakUpdateTable, jump_offset, "c");
            BreakUpdateTable.Release();
            break;
        case FOR_BLOCK:
            BreakUpdateTable.Release();

            // '('
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            {
                char *const at = Token.GetProgramControl();
                const uint32_t line = DebugSourceLine;
                S_TOKEN_TYPE loop_type = UNKNOWN;
                const bool range = PeekRangeFor(loop_type);
                Token.SetProgramControl(at);
                DebugSourceLine = line;
                if (range)
                {
                    if (!CompileRangeFor(Segment, bFunctionBlock, inout, loop_type, BreakUpdateTable))
                        return false;
                    BreakUpdateTable.Release();
                    break;
                }
            }

            // var = initial_value
            if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, INVALID_OFFSET, INVALID_OFFSET, BreakTable))
                return false;

            jump_offset = Segment.BCode_Program_size;

            // condition
            // CompileToken(Segment,PROCESS_EXPRESSION);
            // if(!CompileBlock(Segment,bFunctionBlock,inout,SEPARATOR,INVALID_OFFSET,INVALID_OFFSET,BreakTable)) return
            // false;
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);

            // jump if zero
            update_offset = Segment.BCode_Program_size + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));

            S_TOKEN_TYPE sttResult; // var for inc/dec op
            S_TOKEN_TYPE forOp;
            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            sttResult = DetectUnknown(var_code);
            if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            forOp = Token.Get();
            if (!(forOp == OP_INC || forOp == OP_DEC))
            {
                SetError("invalid 'for' syntax");
                return false;
            }
            if (CompileAuxiliaryTokens(Segment) != CLOSE_BRACKET)
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, jump_offset, 6, BreakUpdateTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, jump_offset, 6, BreakUpdateTable))
                    return false;
                // SetError("missed '{'");    return false;
                // **** ?
            }

            uint32_t forcont_offset;
            forcont_offset = Segment.BCode_Program_size;
            CompileToken(Segment, sttResult, 1, (char *)&var_code, sizeof(uint32_t));
            CompileToken(Segment, forOp);
            CompileToken(Segment, JUMP, 1, (char *)&jump_offset, sizeof(uint32_t));
            memcpy(&Segment.pCode[update_offset], &Segment.BCode_Program_size, sizeof(uint32_t));
            UpdateOffsets(Segment, BreakUpdateTable, Segment.BCode_Program_size, "b");
            UpdateOffsets(Segment, BreakUpdateTable, forcont_offset, "c");
            BreakUpdateTable.Release();
            break;
        case SWITCH_COMMAND:
            BreakUpdateTable.Release();
            // CompileToken(Segment,Token_type);
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            // CompileToken(Segment,OPEN_BRACKET);

            // if(!CompileBlock(Segment,bFunctionBlock,inout,CLOSE_BRACKET,INVALID_OFFSET,INVALID_OFFSET,BreakUpdateTable))
            // return false; CompileToken(Segment,PUSH_EXPRESULT);
            CompileExpression(Segment);

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                // if(!CompileBlock(Segment,bFunctionBlock,inout,BLOCK_OUT,jump_offset,6,BreakUpdateTable)) return
                // false;
                std::vector<uint32_t> fallthrough; // jumps of cases without break into the next case
                auto *const outer = switchFallthrough_;
                switchFallthrough_ = &fallthrough;
                const bool compiled_switch =
                    CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, 6, BreakUpdateTable);
                switchFallthrough_ = outer;
                if (!compiled_switch)
                    return false;
            }
            else
            {
                SetError("missed '{'");
                return false;
            }
            UpdateOffsets(Segment, BreakUpdateTable, Segment.BCode_Program_size, "b");
            CopyOffsets(Segment, BreakUpdateTable, BreakTable, "c");
            // UpdateOffsets(Segment,BreakUpdateTable,continue_jump,"c");    // -> ***
            CompileToken(Segment, POP_VOID);
            BreakUpdateTable.Release();
            break;
        case CASE_COMMAND:
            if (bound_type == BREAK_COMMAND && balance_block == 0)
            {
                // the previous case has no break: jump over this comparison into this case's code
                if (switchFallthrough_)
                {
                    switchFallthrough_->push_back(Segment.BCode_Program_size + 2);
                    vuint32_t = INVALID_OFFSET;
                    CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
                }
                Token.StepBack();
                return true;
            }
            // CompileToken(Segment,Token_type);
            // Token_type = CompileAuxiliaryTokens(Segment);
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);

            /*CompileToken(Segment,PROCESS_EXPRESSION);
            switch(Token_type)
            {
              case UNKNOWN:    CompileUnknown(Segment); break;
              case NUMBER:    CompileNumber(Segment); break;
              case FLOAT_NUMBER: CompileFloatNumber(Segment); break;
              case STRING:    CompileString(Segment); break;
              break;
              default: SetError("invalid case"); return false;
            }

            if(Token.Get() != LABEL){ SetError("missed ':'");    return false; }*/

            if (Token.GetType() != LABEL)
            {
                SetError("missed ':'");
                return false;
            }

            // CompileToken(Segment,SEPARATOR);
            CompileToken(Segment, STACK_COMPARE);

            update_offset = Segment.BCode_Program_size + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            if (switchFallthrough_)
            {
                for (const auto offset : *switchFallthrough_)
                    memcpy(&Segment.pCode[offset], &Segment.BCode_Program_size, sizeof(uint32_t));
                switchFallthrough_->clear();
            }

            // if(!CompileBlock(Segment,bFunctionBlock,inout,BREAK_COMMAND,INVALID_OFFSET,0,BreakTable)) return false;
            // ??? why INVALID_OFFSET on continue_jump here? dont remember; anyway now change to normal
            if (!CompileBlock(Segment, bFunctionBlock, inout, BREAK_COMMAND, continue_jump, 0, BreakTable))
                return false;

            memcpy(&Segment.pCode[update_offset], &Segment.BCode_Program_size, sizeof(uint32_t));
            break;
        case INVALID_TOKEN:
            SetError("bad token");
            return false;
            break;

            /*case CALL:
              CompileToken(Segment,Token_type);
              Token_type = CompileAuxiliaryTokens(Segment);
              if(Token_type != UNKNOWN)
              {
                SetError("invalid string call syntax");
                return false;
              }
              Token_type = DetectUnknown(unk_code);
              if(!(Token_type == VARIABLE || Token_type == LOCAL_VARIABLE))
              {
                SetError("invalid string call syntax");
                return false;
              }
              CompileToken(Segment,Token_type,1,(char *)&unk_code,sizeof(uint32_t));
            break;*/

        case CALL:

            S_TOKEN_TYPE sttVar;
            uint32_t dwCallVarCode;

            // CompileToken(Segment,Token_type);
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == OPEN_BRACKET)
            {
                // call (expression)(args): the expression's text names the function
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != CLOSE_BRACKET || CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
                {
                    SetError("call (name)(arguments) expected");
                    return false;
                }
                if (!CompileCallArguments(Segment, func_args))
                    return false;
                CompileToken(Segment, CALL_NAMED);
                CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
                CompileToken(Segment, POP_NZ);
                break;
            }
            if (Token_type != UNKNOWN)
            {
                SetError("invalid dynamic call");
                return false;
            }
            sttVar = DetectUnknown(dwCallVarCode);
            if (!(sttVar == VARIABLE || sttVar == LOCAL_VARIABLE))
            {
                SetError("invalid dynamic call");
                return false;
            }
            //======================================================

            if (CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
            {
                SetError("missing '('");
                return false;
            }
            if (!CompileCallArguments(Segment, func_args))
                return false;
            // CompileToken(Segment,CALL_FUNCTION,1,(char *)&func_code,sizeof(uint32_t));
            // CompileToken(Segment,ARGS_NUM,1,(char *)&func_args,sizeof(DWORD));
            // if(fi.return_type!=TVOID) CompileToken(Segment,STACK_POP_VOID);
            //======================================================

            CompileToken(Segment, CALL);
            CompileToken(Segment, Token_type, 1, (char *)&dwCallVarCode, sizeof(uint32_t));
            CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));
            CompileToken(Segment, POP_NZ); // pop return value if function returned value

            break;

        case UNKNOWN: {
            uint32_t func_code;
            uint32_t func_args;
            if (declExpectName_)
            {
                declExpectName_ = false;
                if (!CompileLocalDeclarator(Segment, inout, false))
                    return false;
                break;
            }
            if (inout == 0 && Token.GetData() && storm::iEquals(Token.GetData(), "nullptr"))
                break; // "ref g = nullptr;": global initializers are not code
            if (Token.Get() == LABEL)
            {
                // "name:" starts a statement: a goto label (registered by pass 1) or a switch's "default:"
                // (the cases before it jump past it when one matched)
                Token.StepBack();
                Token.StepBack();
                Token.Get();
                if (!storm::iEquals(Token.GetData(), "default"))
                {
                    const auto lab_code =
                        LabelTable.GetStringCode(LabelKey(CurrentFunctionName(), Token.GetData()).c_str());
                    if (lab_code == INVALID_ORDINAL_NUMBER)
                    {
                        SetError("misplaced label '%s'", Token.GetData());
                        return false;
                    }
                    LabelTable.SetStringData(lab_code, &Segment.BCode_Program_size);
                }
                Token.Get(); // ':'
                break;
            }
            Token.StepBack(); // back to the identifier (restores its data)
            Token.StepBack();
            Token.Get();
            if (IsAttributeCall())
            {
                // object.attr(args); calls the function the attribute names
                Token.StepBack();
                if (!CompileExpression(Segment))
                    return false;
                CompileToken(Segment, POP_NZ); // drop a returned value
                Token.StepBack();
                break;
            }
            if (Token.GetData() && IsAttributeIntrinsic(Token.GetData()))
            {
                // deleteattr(&a.b); touchattr(a.b); as statements
                if (!CompileAttributeIntrinsic(Segment))
                    return false;
                CompileToken(Segment, STACK_POP_VOID);
                break;
            }
            if (S_TOKEN_TYPE call_type = DetectCallThrough(); call_type != UNKNOWN)
            {
                // object->Method(...) or fref_variable(...) as a statement
                if (!CompileCallThrough(Segment, call_type))
                    return false;
                CompileToken(Segment, POP_NZ); // drop a returned value
                break;
            }

            if (DetectUnknown(func_code) == CALL_FUNCTION)
            {
                if (CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
                {
                    SetError("missing '('");
                    return false;
                }
                if (!CompileCallArguments(Segment, func_args) || !CheckCallArguments(func_code, func_args))
                    return false;

                CompileToken(Segment, CALL_FUNCTION, 1, (char *)&func_code, sizeof(uint32_t));
                CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));
                if (FuncTab.GetFuncInfo(func_code)->return_type != TVOID)
                    CompileToken(Segment, STACK_POP_VOID);
            }
            else if (!CompileUnknown(Segment))
                return false;
        }
        break;

        case EXTERN:
            Token_type = Token.Get();
            bExtern = true;
        case IMPORT:
            if (!bExtern)
            {
                Token_type = Token.Get();
                bImport = true;
            }
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_INTEGER: // create global variable
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_FREF:
        case TVOID:
            if (inout > 0)
            {
                // local variable declaration
                if (!CompileLocalDeclarator(Segment, inout, true))
                    return false;
                break;
            }
            if (Token.GetData() && strcmp(Token.GetData(), "[") == 0)
            {
                Token.Get(); // "type []Name(": a function returning an array (checked by pass 1)
                Token.Get();
            }
            Token_type = DetectUnknown(unk_code);
            if (Token_type == CALL_FUNCTION)
            {
                // function declaration

                bFunctionBlock = true;

                CurrentFuncCode = FuncTab.FindFunc(Token.GetData());
                localScopes_.clear();
                localDeclarations_.clear();
                if (!bExtern)
                    if (!bImport)
                    {
                        if (!FuncTab.SetFuncOffset(Token.GetData(), Segment.BCode_Program_size))
                        {
                            SetError("Invalid function name: %s", Token.GetData());
                            return false;
                        }

                        auto func_name = Token.GetData();
                        auto cmp = [&func_name](const auto &func) { return storm::iEquals(func.info.name, func_name); };
                        auto it = std::ranges::find_if(script_cache_.functions, cmp);
                        if (it != script_cache_.functions.end())
                        {
                            it->info.offset = Segment.BCode_Program_size;
                        }
                    }
                bExtern = false;
                bImport = false;
                // skip function arguments list in function declaration
                if (Token.Get() != OPEN_BRACKET)
                {
                    SetError("Invalid syntax");
                    return false;
                }

                // pass 1 checked the parameter list (ScanParameterList)
                for (uint32_t depth = 0;;)
                {
                    Token_type = Token.Get();
                    if (Token_type == END_OF_PROGRAMM)
                        break;
                    if (Token_type == OPEN_BRACKET)
                        depth++;
                    else if (Token_type == CLOSE_BRACKET && depth-- == 0)
                        break;
                }
            }

            // ... skip var or func declaration
            break;

        case FUNCTION_RETURN:
        case FUNCTION_RETURN_VOID:
            Token_type = Token.Get();
            if (Token_type == SEPARATOR)
                CompileToken(Segment, FUNCTION_RETURN_VOID);
            else
            {
                Token.StepBack();
                CompileExpression(Segment); // this will cause function result be pushed into stack
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, EX);
                CompileToken(Segment, FUNCTION_RETURN);
            }
            Token.StepBack(); // ???

            break;

            // case MAKEREF_COMMAND: CompileToken(Segment,Token_type); break;
            // case MAKEAREF_COMMAND: CompileToken(Segment,Token_type); break;

        case OP_SIZEOF: {
            // @array = size
            uint32_t var_code;
            if (Token.Get() != UNKNOWN)
            {
                SetError("array expected after '@'");
                return false;
            }
            const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
            if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE) || Token.Get() != OP_EQUAL)
            {
                SetError("'@array = size' expected");
                return false;
            }
            if (!CompileExpression(Segment))
                return false;
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, RESIZE_ARRAY);
            CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
            Token.StepBack();
            break;
        }
        case OP_BOOL_NEG:
            // case OP_EQUAL:
        case OP_BOOL_EQUAL:
        case OP_GREATER:
        case OP_GREATER_OR_EQUAL:
        case OP_LESSER:
        case OP_LESSER_OR_EQUAL:
        case OP_NOT_EQUAL:
        case OP_MINUS:
        case OP_PLUS:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_POWER:
        case OP_MODUL:
        case OP_INC:
        case OP_DEC:
            /*        case OP_INCADD:
                case OP_DECADD:
                case OP_MULTIPLYEQ:
                case OP_DIVIDEEQ://*/
        case OP_BOOL_AND:
        case OP_BOOL_OR:
            // case SQUARE_OPEN_BRACKET:
            // case SQUARE_CLOSE_BRACKET:
        case COMMA:
        case AND:
        case END_OF_PROGRAMM:
            CompileToken(Segment, Token_type);
            if (Token_type == COMMA && declListActive_)
                declExpectName_ = true; // the next declarator of "int a, b"
            break;

            //=============================================================================

        case OP_INCADD:
        case OP_DECADD:
        case OP_MULTIPLYEQ:
        case OP_DIVIDEEQ:
        case OP_OREQ:
        case OP_ANDEQ:
            // CompileToken(Segment,Token_type);
            CompileExpression(Segment);
            CompileToken(Segment, Token_type);
            // CompileToken(Segment,LEFT_OPERAND);
            // CompileToken(Segment,STACK_TOP);
            CompileToken(Segment, STACK_POP_VOID);
            if (Token.GetType() == bound_type)
            {
                Token.StepBack();
            }

            break;
            //*/

        case OP_COLON_EQUAL: // CL: assignment that keeps the value's type; attributes hold text anyway
        case OP_EQUAL:
            if (inout == 0 && !bDebugExpressionRun)
                break; // skip global vars initialization
            // CompileToken(Segment,Token_type);
            CompileExpression(Segment);

            CompileToken(Segment, MOVE);
            CompileToken(Segment, LEFT_OPERAND);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, STACK_POP_VOID);
            Token.StepBack();

            break;

            //        case OP_EQUAL:
            //            CompileToken(Segment,Token_type);
            //        break;

        case SQUARE_OPEN_BRACKET:
            if (Token.Get() == SQUARE_CLOSE_BRACKET)
                break; // "type name[]" of a global array sized at run time
            Token.StepBack();
            if (!CompileExpression(Segment))
            {
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, LEFTOP_INDEX);
            // a[i][j] = ...: an element of the array a[i] refers to
            while (Token.Get() == SQUARE_OPEN_BRACKET)
            {
                if (!CompileExpression(Segment))
                    return false;
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);
                CompileToken(Segment, LEFTOP_SUBINDEX);
            }
            Token.StepBack();

            break;
        case SQUARE_CLOSE_BRACKET:
            SetError("']' without matching '['");
            break;

            // case MAKEREF_COMMAND: CompileToken(Segment,Token_type); break;
        case MAKEREF_COMMAND:
            uint32_t dwRCode1, dwRCode2;
            S_TOKEN_TYPE sttVarType1, sttVarType2;

            // CompileToken(Segment,Token_type);
            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 1st operand");
                return false;
            }

            sttVarType1 = DetectUnknown(dwRCode1);

            if (!(sttVarType1 == VARIABLE || sttVarType1 == LOCAL_VARIABLE))
            {
                SetError("invalid 1st operand");
                return false;
            }

            /*CompileToken(Segment,STACK_ALLOC);
            CompileToken(Segment,SETREF);
            CompileToken(Segment,STACK_TOP);
            CompileToken(Segment,sttVarType,1,(char *)&dwRCode,sizeof(DWORD));*/
            /*            if(sttVarType == VARIABLE)
                  {
                    if(!VarTab.GetVar(vi,*((uint32_t *)&pCodeBase[ip]))) { SetError("Global variable not found"); break;
               } pRef = vi.pDClass;
                  }
                  else
                  {
                    pRef = SStack.Read(pRun_fi->stack_offset,*((uint32_t *)&pCodeBase[ip]));
                    if(pRef == 0) { SetError("Local variable not found"); return false; }
                  }
            */

            //            if(!pRef->IsReference()) { SetError("makeref to non reference variable"); return false; }
            if (Token.Get() != COMMA)
            {
                SetError("missed ','");
                return false;
            }

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 1st operand");
                return false;
            }

            sttVarType2 = DetectUnknown(dwRCode2);

            if (!(sttVarType2 == VARIABLE || sttVarType2 == LOCAL_VARIABLE))
            {
                SetError("invalid 2st operand");
                return false;
            }

            if (Token.Get() == SQUARE_OPEN_BRACKET)
            {
                CompileExpression(Segment);

                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                CompileToken(Segment, SETREF_BXINDEX);
                CompileToken(Segment, sttVarType1, 1, (char *)&dwRCode1, sizeof(uint32_t));
                CompileToken(Segment, sttVarType2, 1, (char *)&dwRCode2, sizeof(uint32_t));

                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missed ']'");
                    return false;
                }
                if (Token.Get() != CLOSE_BRACKET)
                {
                    SetError("missed ')'");
                    return false;
                }
            }
            else
            {
                if (Token.GetType() != CLOSE_BRACKET)
                {
                    SetError("missed ')'");
                    return false;
                }
                CompileToken(Segment, SETREF);
                CompileToken(Segment, sttVarType1, 1, (char *)&dwRCode1, sizeof(uint32_t));
                CompileToken(Segment, sttVarType2, 1, (char *)&dwRCode2, sizeof(uint32_t));
            }

            /*    if(BC_TokenGet()== SQUARE_OPEN_BRACKET)
              {
                if(!BC_ProcessExpression(&ExpressionResult)) { SetError("Invalid array index"); return false; }
                if(!ExpressionResult.Get(array_index)) { SetError("Invalid array index"); return false; }
                if(TokenType()!= SQUARE_CLOSE_BRACKET){ SetError("missed ']'"); return false; }
                BC_TokenGet();
                pVar = (DATA *)pVar->GetArrayElement(array_index);
              }
              pRef->SetReference(pVar->GetVarPointer());
              if(TokenType() != CLOSE_BRACKET) {SetError("missed ')'"); return false;};
      */

            break;

            // case MAKEAREF_COMMAND: CompileToken(Segment,Token_type); break;

        case MAKEAREF_COMMAND:

            // CompileToken(Segment,Token_type);    // debug
            S_TOKEN_TYPE sttVarType;

            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }
            Token_type = Token.Get();
            if (Token_type != UNKNOWN)
            {
                SetError("invalid 1st argument");
                return false;
            }
            Token_type = DetectUnknown(dwRCode);
            if (!(Token_type == VARIABLE || Token_type == LOCAL_VARIABLE))
            {
                SetError("invalid 1st argument");
                return false;
            }

            if (Token_type == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwRCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                if (!(real_var->type == VAR_AREFERENCE || real_var->type == VAR_REFERENCE))
                {
                    SetError("not aref or ref");
                    return false;
                }
            }
            else
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwRCode);
                if (!(lvi.type == VAR_AREFERENCE || lvi.type == VAR_REFERENCE))
                {
                    SetError("not aref or ref");
                    return false;
                }
            }

            // set ref to stack top
            CompileToken(Segment, STACK_ALLOC);
            CompileToken(Segment, SETREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, Token_type, 1, (char *)&dwRCode, sizeof(uint32_t));

            if (Token.Get() != COMMA)
            {
                SetError("missed ','");
                return false;
            }

            sttVarType = Token.Get();
            if (sttVarType != UNKNOWN)
            {
                SetError("invalid 2d argument");
                return false;
            }
            sttVarType = DetectUnknown(dwRCode);
            if (!(sttVarType == VARIABLE || sttVarType == LOCAL_VARIABLE))
            {
                SetError("invalid 2d argument");
                return false;
            }

            Token_type = Token.Get();
            if (Token_type == SQUARE_OPEN_BRACKET)
            {
                CompileExpression(Segment);
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                //....
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                // move ap to AP register
                CompileToken(Segment, MOVEAP_BXINDEX);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
                Token_type = Token.Get();

                CompileToken(Segment, PUSH_OBJID_BXINDEX);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
            }
            else
            {
                // bove atributes root to AP
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));

                CompileToken(Segment, PUSH_OBJID);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
            }

            if (Token_type != DOT)
            {
                // set attribute pointer to referenced var
                CompileToken(Segment, SETAREF);
                CompileToken(Segment, STACK_TOP);
                CompileToken(Segment, AP);
                CompileToken(Segment, STACK_POP_VOID);
                break;
            }

            // advance ap

            sttResult = Token.Get();
            uint32_t dwAWCode;
            uint32_t dwVarCode;
            do
            {
                if (Token.GetType() == OPEN_BRACKET)
                {
                    // access var
                    sttResult = Token.Get();
                    if (sttResult != UNKNOWN)
                    {
                        SetError("Invalid access var syntax");
                        return false;
                    }
                    sttResult = DetectUnknown(dwVarCode);
                    if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
                    {
                        SetError("not variable: %s", Token.GetData());
                        return false;
                    }
                    // CompileToken(Segment,ADVANCE_AP);
                    CompileToken(Segment, VERIFY_AP);
                    CompileToken(Segment, sttResult, 1, &dwVarCode, sizeof(uint32_t));
                    if (Token.Get() != CLOSE_BRACKET)
                    {
                        SetError("missing ')'");
                        return false;
                    }
                }
                else
                {
                    // access word
                    if (Token.GetData() == nullptr)
                    {
                        SetError("Invalid access string");
                        return false;
                    }
                    Token.LowCase();
                    dwAWCode = SCodec.Convert(Token.GetData());
                    // CompileToken(Segment,ADVANCE_AP);
                    CompileToken(Segment, VERIFY_AP);
                    CompileToken(Segment, ACCESS_WORD_CODE, 1, &dwAWCode, sizeof(uint32_t));
                }
                sttResult = Token.Get();
                if (sttResult == DOT)
                    Token.Get(); // get next token (after dot)
            } while (sttResult == DOT);

            CompileToken(Segment, SETAREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, AP);
            CompileToken(Segment, STACK_POP_VOID);
            break;
            //=============================================================================

        case DOT:
            Token_type = Token.Get();
            if (Token_type == OPEN_BRACKET && !IsAccessVariable())
            {
                // .(expression) on the left side: the expression's text names the attribute
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != CLOSE_BRACKET)
                {
                    SetError("no mathcing ')'");
                    return false;
                }
                CompileToken(Segment, ACCESS_EXPR);
                break;
            }
            if (Token_type == OPEN_BRACKET)
            {
                // access word inside variable
                Token.Get(); // variable name
                // check for valid local variable
                var_code = FindLocal(Token.GetData());
                if (var_code != INVALID_VAR_CODE)
                {
                    Token.Get();
                    if (Token.GetType() != CLOSE_BRACKET)
                    {
                        SetError("no mathcing ')'");
                        return false;
                    }
                    CompileToken(Segment, ACCESS_VAR);
                    CompileToken(Segment, LOCAL_VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
                    break;
                }

                var_code = VarTab.FindVar(Token.GetData());
                if (var_code != INVALID_VAR_CODE)
                {
                    Token.Get();
                    if (Token.GetType() != CLOSE_BRACKET)
                    {
                        SetError("no mathcing ')'");
                        return false;
                    }
                    CompileToken(Segment, ACCESS_VAR);
                    CompileToken(Segment, VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
                    break;
                }

                SetError("Invalid access string");
                return false;
            }
            // else its access word
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }
            // CompileToken(Segment,ACCESS_WORD,1,Token.GetData(),strlen(Token.GetData())+1);

            Token.LowCase();
            awcode = SCodec.Convert(Token.GetData());
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &awcode, sizeof(uint32_t));
            break;

        case TRUE_CONST:
            lvalue = 1;
            CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
            break;
        case FALSE_CONST:
            lvalue = 0;
            CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
            break;

            // write value
        case NUMBER:
            CompileNumber(Segment);
            break;
        case FLOAT_NUMBER:
            CompileFloatNumber(Segment);
            break;
        case STRING:
            CompileString(Segment);
            break;

        case LABEL: // register label

            break;
        case DEFINE_COMMAND: // create define

            break;

        default:

            break;
        }
    } while (Token_type != END_OF_PROGRAMM);

    return true;
}

bool COMPILER::TokenIs(S_TOKEN_TYPE test)
{
    if (TokenLastReadResult == test)
        return true;
    return false;
}

bool COMPILER::BC_Jump(SEGMENT_DESC &Segment, uint32_t offset)
{
    if (offset >= Segment.BCode_Program_size)
    {
        SetError("invalid jump");
        return false;
    }
    InstructionPointer = offset;
    return true;
}

S_TOKEN_TYPE COMPILER::BC_TokenGet(uint32_t &ip, uint32_t &token_data_size)
{
    // function read token type and data size, advance InstructionPointer to next token
    // set ip to token data
    // set token data size value and return token type
    TokenLastReadResult = static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pRunCodeBase[InstructionPointer]));
    // Trace("Token: %s", Token.GetTypeName(TokenLastReadResult));
    InstructionPointer++;
    if (static_cast<uint8_t>(pRunCodeBase[InstructionPointer]) < 0xff)
    {
        token_data_size = static_cast<uint8_t>(pRunCodeBase[InstructionPointer]);
        InstructionPointer++;
    }
    else
    {
        InstructionPointer++;
        memcpy(&token_data_size, &pRunCodeBase[InstructionPointer], sizeof(uint32_t));
        InstructionPointer += sizeof(uint32_t);
    }
    ip = InstructionPointer;
    TLR_DataOffset = InstructionPointer;
    InstructionPointer += token_data_size;
    return TokenLastReadResult;
}

S_TOKEN_TYPE COMPILER::BC_TokenGet()
{
    // short version of function for calls what doesnt need to work with token data
    uint32_t token_data_size;
    TokenLastReadResult = static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pRunCodeBase[InstructionPointer]));
    // Trace("Token: %s", Token.GetTypeName(TokenLastReadResult));
    InstructionPointer++;
    if (static_cast<uint8_t>(pRunCodeBase[InstructionPointer]) < 0xff)
    {
        token_data_size = static_cast<uint8_t>(pRunCodeBase[InstructionPointer]);
        InstructionPointer++;
    }
    else
    {
        InstructionPointer++;
        memcpy(&token_data_size, &pRunCodeBase[InstructionPointer], sizeof(uint32_t));
        InstructionPointer += sizeof(uint32_t);
    }
    TLR_DataOffset = InstructionPointer;
    InstructionPointer += token_data_size;
    return TokenLastReadResult;
}

S_TOKEN_TYPE COMPILER::NextTokenType()
{
    return static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pRunCodeBase[InstructionPointer]));
}

bool COMPILER::BC_CallFunction(uint32_t func_code, uint32_t &ip, DATA *&pVResult)
{
    // snapshot of the scalar fields the call needs after the callee has run
    uint32_t mem_ip;
    uint32_t mem_InstructionPointer;
    FuncFrame *mem_pfi;
    //    DATA * pV;
    const char *mem_codebase;
    uint32_t arguments;
    uint32_t check_sp;
#ifdef _WIN32 // S_DEBUG
    uint32_t nDebugEnterMode;
#endif

    CompilerStage = CS_RUNTIME;

    // get func info
    const FuncInfo *const call_pfi = FuncTab.GetFuncPtr(func_code);
    if (call_pfi == nullptr)
    {
        SetError("Invalid function call");
        return false;
    }
    // no deep copy: the table entry address is stable (see FuncTable::funcs_)
    const FuncInfo &call_fi = *call_pfi;
    const uint32_t call_segment_id = call_fi.segment_id;
    const S_TOKEN_TYPE call_return_type = call_fi.return_type;
    const SIMPORTFUNC call_imported_func = call_fi.imported_func;

    // TODO: only do if stack debug if enabled (should be runtime configurable)
    // push function details to call stack
    storm::ringbuffer_stack_push_guard push_guard(callStack_);
    push_guard.push(std::make_tuple(call_fi.decl_file_name.c_str(), call_fi.decl_line, call_fi.name.c_str()));

    // number f arguments pushed into stack for this function call
    if (BC_TokenGet() != ARGS_NUM)
    {
        SetError("missing args_num token");
        return false;
    }
    arguments = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);

    check_sp = SStack.GetDataNum() - arguments;
    /*
    // arguments number check done on compilation stage now
    // arguments pushed into stack before this function call

      // push function arguments into stack ----------------------------------------------
      if(BC_TokenGet() != OPEN_BRACKET)  { SetError("missed '('"); return false; }
      arguments = 0;
      check_sp = SStack.GetDataNum();
      call_fi.stack_offset = check_sp;

      if(NextTokenType()!= CLOSE_BRACKET)
      {
        while(TokenType() != CLOSE_BRACKET)
        {
          pV = SStack.Push();
          BC_ProcessExpression(pV);
          arguments++;
        }
      } else
      {
        BC_TokenGet();
      }
      if(arguments != call_fi.arguments)
      {
        if(call_fi.segment_id != INTERNAL_SEGMENT_ID)
        {
          SetError("function '%s(args:%d)' doesnt accept %d arguments",call_fi.name,call_fi.arguments,arguments);
          return false;
        }
        else
        {
          if(!IsIntFuncVarArgsNum(func_code))
          {
            SetError("function '%s(args:%d)' doesnt accept %d arguments",call_fi.name,call_fi.arguments,arguments);
            return false;
          }
        }
      }
      //---------------------------------------------------------------------------------------
    //*/

    // save current pointers values
    mem_InstructionPointer = InstructionPointer;
    mem_ip = ip;
    mem_pfi = pRun_fi;
    mem_codebase = pRunCodeBase;

#ifdef _WIN32 // S_DEBUG
    nDebugEnterMode = CDebug->GetTraceMode();
#endif
    uint64_t nTicks;
    if (call_segment_id == INTERNAL_SEGMENT_ID)
    {
        if (bRuntimeLog)
        {
            if (!FuncTab.AddCall(func_code))
            {
                core_internal.Trace("Invalid func_code = %u for AddCall", func_code);
            }
        }

        // BC_CallIntFunction(func_code,pVResult,arguments);
        RDTSC_B(nTicks);
        BC_CallIntFunction(func_code, pVResult, arguments);
        RDTSC_E(nTicks);

        if (!FuncTab.AddTime(func_code, nTicks))
        {
            core_internal.Trace("Invalid func_code = %u for AddTime", func_code);
        }
    }
    else if (call_segment_id == IMPORTED_SEGMENT_ID)
    {
        // a native reads its arguments after it pushes its result into the same stack slot (the
        // value's type is reset by then): give numbers and text the declared parameter types now
        // (CL passes ints to float parameters, e.g. Degree2Radian(14))
        const uint32_t first = SStack.GetDataNum() - arguments;
        for (uint32_t n = 0; n < arguments && n < call_fi.local_vars.size(); n++)
        {
            const S_TOKEN_TYPE declared = call_fi.local_vars[n].type;
            DATA *argument = SStack.Read(first, n);
            const S_TOKEN_TYPE type = argument->GetType();
            auto scalar = [](S_TOKEN_TYPE t) { return t == VAR_INTEGER || t == VAR_FLOAT || t == VAR_STRING; };
            if (type != declared && scalar(type) && scalar(declared) && !argument->IsArray())
                argument->Convert(declared);
        }
        pVResult = nullptr;
        RDTSC_B(nTicks);
        const uint32_t nResult = call_imported_func(&SStack);
        if (nResult == IFUNCRESULT_OK)
        {
            if (call_return_type != TVOID)
            {
                pVResult = SStack.Read();
            }
        }
        RDTSC_E(nTicks);
        if (!FuncTab.AddTime(func_code, nTicks))
        {
            core_internal.Trace("Invalid func_code = %u for AddTime", func_code);
        }
    }
    else
    {
        // BC_Execute(func_code,pVResult);
        RDTSC_B(nTicks);
        BC_Execute(func_code, pVResult, nullptr, arguments);
        RDTSC_E(nTicks);
        if (!FuncTab.AddTime(func_code, nTicks))
        {
            core_internal.Trace("Invalid func_code = %u for AddTime", func_code);
        }
    }
#ifdef _WIN32 // S_DEBUG
    if (nDebugEnterMode == TMODE_MAKESTEP)
    {
        CDebug->SetTraceMode(TMODE_MAKESTEP);
    }
#endif

    if (pVResult)
    {
        if (check_sp != (SStack.GetDataNum() - 1))
        {
            SetError("function '%s' stack error", call_fi.name.c_str());

            pRun_fi = mem_pfi;
            InstructionPointer = mem_InstructionPointer;
            ip = mem_ip;

            RunningSegmentID = pRun_fi->segment_id;
            pRunCodeBase = mem_codebase;

            // return false;    // debug!
        }
    }
    else
    {
        if (check_sp != SStack.GetDataNum())
        {
            SetError("function '%s' stack error", call_fi.name.c_str());
            pRun_fi = mem_pfi;
            InstructionPointer = mem_InstructionPointer;
            ip = mem_ip;

            RunningSegmentID = pRun_fi->segment_id;
            pRunCodeBase = mem_codebase;

            // return false;// debug!
        }
    }
    //*/
    // restore
    pRun_fi = mem_pfi;
    InstructionPointer = mem_InstructionPointer;
    ip = mem_ip;
    if (pRun_fi)
        RunningSegmentID = pRun_fi->segment_id;
    pRunCodeBase = mem_codebase;

    return true;
}

// `call name()` leaves 1 (the callee returned a value) or 0 in the expression register. The
// callee may have left a reference there (`ref r = CharacterFromID(...)` does), and DATA::Set
// writes through a reference, which would overwrite the referenced object with the status; the
// register's previous type is therefore dropped first.
void COMPILER::StoreDynamicCallStatus(bool returned_value)
{
    ExpressionResult.ClearType();
    ExpressionResult.Set(static_cast<int32_t>(returned_value ? 1 : 0));
}

bool COMPILER::BC_Execute(uint32_t function_code, DATA *&pVReturnResult, const char *pDbgExpSource,
                          uint32_t passed_args)
{
    // GUARD(BC_Execute)
    uint32_t inout;
    uint32_t segment_index;
    uint32_t token_data_size;
    uint32_t ip;
    uint32_t n;
    uint32_t jump_offset;
    uint32_t func_code;
    uint32_t nLeftOperandCode;
    uint32_t bLeftOperandType;
    int32_t nLeftOperandIndex;
    S_TOKEN_TYPE Token_type;
    // fi: stable table entry (or an empty one for debug expressions), never copied.
    // frame: this invocation's scalar snapshot, including its own stack offset.
    static const FuncInfo empty_fi;
    const FuncInfo *pfi = &empty_fi;
    FuncFrame frame(empty_fi);
    const VarInfo *real_var;
    DATA *pV;
    DATA *pVResult;
    //    DATA   ExpressionResult;    // while compile expression not ready, each function have its own register
    const char *pCodeBase;
    bool bExit;
    int32_t lvalue;
    S_TOKEN_TYPE vtype;
    DATA *pVV;
    uint32_t var_code;
    const char *pAccess_string;
    //    int32_t array_index;
    //    DATA * pRef;
    DATA *pVar;
    entid_t eid;
    //    ATTRIBUTES * pRoot;
    ATTRIBUTES *pLeftOperandAClass;
    DATA *pLeftOperandElement = nullptr; // a[i][j] = ...: the element itself
    bool bDebugWaitForThisFunc;
    DATA *pVDst;
    DATA *pVSrc;
    bool bUseIndex;
    int32_t dwBXIndex;

    CompilerStage = CS_RUNTIME;

    if (bRuntimeLog)
    {
        if (!FuncTab.AddCall(function_code))
        {
            core_internal.Trace("Invalid function_code = %u for AddCall", function_code);
        }
    }

    bDebugWaitForThisFunc = false;

    // ExpressionResult.SetVCompiler(this);
    pVReturnResult = nullptr;
    pVResult = nullptr;

    if (pDbgExpSource == nullptr)
    {
        if (const auto *table_fi = FuncTab.GetFuncPtr(function_code); table_fi != nullptr)
        {
            pfi = table_fi;
            frame = FuncFrame(*table_fi);
        }
        else
        {
            SetError("Invalid function: %s", pfi->name.c_str());
            return false;
        }

        if (frame.offset == INVALID_FUNC_OFFSET)
        {
            SetError("Function (%s) isnt loaded", pfi->name.c_str());
            return false;
        }

        if (frame.segment_id == INTERNAL_SEGMENT_ID)
        {
            SetError("Function (%s) is internal", pfi->name.c_str());
            return false;
        }

        if (frame.segment_id == IMPORTED_SEGMENT_ID)
        {
            SetError("Function (%s) is imported", pfi->name.c_str());
            return false;
        }

        segment_index = GetSegmentIndex(frame.segment_id);
        if (segment_index == INVALID_SEGMENT_INDEX)
        {
            SetError("Function (%s) segment not loaded", pfi->name.c_str());
            return false;
        }
        if (SegmentTable[segment_index].pCode == nullptr)
        {
            SetError("Segment (%s) not loaded", SegmentTable[segment_index].name.c_str());
            return false;
        }

        // Trace("-----------------------------------------------------------------");
        // Trace("Execute function: %s",pfi->name);

        RunningSegmentID = frame.segment_id;
        if (passed_args != kArgsAsDeclared && passed_args != frame.arguments &&
            !BindPassedArguments(*pfi, frame, passed_args))
            return false;
        frame.stack_offset = SStack.GetDataNum() - frame.arguments; // set stack offset

        // check arguments types
        for (n = 0; n < frame.arguments; n++)
        {
            if (pfi->local_vars[n].type == VAR_REFERENCE)
                continue;
            pV = SStack.Read(frame.stack_offset, n);
            if (pV->GetType() != pfi->local_vars[n].type)
            {
                if (pV->GetType() == VAR_REFERENCE && pV->pReference == nullptr &&
                    (pfi->local_vars[n].type == VAR_AREFERENCE || pfi->local_vars[n].type == VAR_FREF))
                    continue; // nullptr for an aref or fref parameter
                pV = pV->GetVarPointer();
                if (!pV)
                {
                    SetError("uninitialized function argument");
                    return false;
                }

                if (pfi->local_vars[n].type == VAR_AREFERENCE && pV->GetType() == VAR_OBJECT)
                    continue;

                // TODO: remove and fix
                if (false && pV->GetType() != pfi->local_vars[n].type)
                {
                    SetWarning("wrong type of argument %d  %s(%s) <-- [%s]", n, pfi->name.c_str(),
                               Token.GetTypeName(pfi->local_vars[n].type), Token.GetTypeName(pV->GetType()));
                }
            }
        }

        for (n = frame.arguments; n < pfi->local_vars.size(); n++)
        {
            pV = SStack.Push();
            if (pfi->local_vars[n].elements == 0 || (pfi->local_vars[n].array && pfi->local_vars[n].elements == 1))
            {
                pV->SetType(pfi->local_vars[n].type); // an array sized at run time, or of one element
                pV->SetArraySize(static_cast<uint32_t>(pfi->local_vars[n].elements));
            }
            else
                pV->SetType(pfi->local_vars[n].type, pfi->local_vars[n].elements);
        }

        pRun_fi = &frame; // set pointer to 'this' function info

        InstructionPointer = frame.offset;

        pCodeBase = SegmentTable[segment_index].pCode;
        pRunCodeBase = pCodeBase;
    }
    else
    {
        InstructionPointer = 0;
        pCodeBase = pDbgExpSource;
        pRunCodeBase = pCodeBase;
    }

    inout = 0;
    bExit = false;

    nLeftOperandIndex = INVALID_ARRAY_INDEX;
    nLeftOperandCode = INVALID_VAR_CODE;
    bLeftOperandType = 0xffffffff;
    pLeftOperandAClass = nullptr;

    // a call from the engine (outermost BC_Execute) gets a time limit: an endless script loop stops
    // with an error and the script stack instead of hanging the engine
    struct CallDepth
    {
        COMPILER &compiler;
        explicit CallDepth(COMPILER &c) : compiler(c)
        {
            if (compiler.scriptCallDepth_++ == 0)
            {
                compiler.scriptCallStart_ = std::chrono::steady_clock::now();
                compiler.scriptCallAborted_ = false;
            }
        }
        ~CallDepth()
        {
            compiler.scriptCallDepth_--;
        }
    } call_depth(*this);

    // S_TOKEN_TYPE oldToken_type;
    do
    {
        // the clock is read every 2^20 instructions; after a stop every frame of the call returns
        if (scriptCallAborted_ || ((++dwNumberScriptCommandsExecuted & 0xFFFFF) == 0 && scriptCallTimeLimitMs_ != 0 &&
                                   !ScriptCallInTime()))
            return false;
        // oldToken_type = Token_type;
        Token_type = BC_TokenGet(ip, token_data_size);

        switch (Token_type)
        {
        case ARGS_NUM:
            psnip_trap();
            break;
        case STACK_COMPARE:
            pV = SStack.Read();
            ExpressionResult.CompareAndSetResult(pV, OP_BOOL_EQUAL);
            break;
        case POP_EXPRESULT:
            pV = SStack.Pop();
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pV);
            break;
        case PUSH_EXPRESULT:
            pV = SStack.Push();
            pV->Copy(&ExpressionResult);
            break;
        case POP_NZ:
            ExpressionResult.Convert(VAR_INTEGER);
            ExpressionResult.Get(lvalue);
            if (lvalue == 0)
                break;
            SStack.Pop();
            break;
        case POP_VOID:
            SStack.Pop();
            break;
        case JUMP:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case JUMP_Z:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            ExpressionResult.Convert(VAR_INTEGER);
            ExpressionResult.Get(lvalue);
            if (lvalue)
                break;
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case JUMP_NZ:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            ExpressionResult.Convert(VAR_INTEGER);
            ExpressionResult.Get(lvalue);
            if (lvalue == 0)
                break;
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case LOCAL_VARIABLE:
            pLeftOperandAClass = nullptr;            // reset attribute
            pLeftOperandElement = nullptr;
            nLeftOperandIndex = INVALID_ARRAY_INDEX; // reset index
            nLeftOperandCode = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
            bLeftOperandType = LOCAL_VARIABLE;
            break;
        case VARIABLE:
            pLeftOperandAClass = nullptr;            // reset attribute
            pLeftOperandElement = nullptr;
            nLeftOperandIndex = INVALID_ARRAY_INDEX; // reset index
            nLeftOperandCode = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
            bLeftOperandType = VARIABLE;
            break;
        case ACCESS_WORD_CODE:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }

            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }
                pLeftOperandAClass =
                    pLeftOperandAClass->VerifyAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass =
                pLeftOperandAClass->VerifyAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
            break;
            break;
        case ACCESS_WORD:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }
                pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
            break;
            break;
        case ACCESS_VAR:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }

                vtype = BC_TokenGet();
                var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
                if (!(vtype == VARIABLE || vtype == LOCAL_VARIABLE))
                {
                    SetError("invalid access var");
                    return false;
                }
                if (vtype == VARIABLE)
                {
                    real_var = VarTab.GetVar(var_code);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pVV = real_var->value.get();
                }
                else
                {
                    pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                    if (pVV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pVV->Get(pAccess_string);

                // pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass(pAccess_string);
                pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, pAccess_string);
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }

            vtype = BC_TokenGet();
            var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
            if (!(vtype == VARIABLE || vtype == LOCAL_VARIABLE))
            {
                SetError("invalid access var");
                return false;
            }
            if (vtype == VARIABLE)
            {
                real_var = VarTab.GetVar(var_code);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVV = real_var->value.get();
            }
            else
            {
                pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pVV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            pVV->Get(pAccess_string);

            // pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass(pAccess_string);
            pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, pAccess_string);

            break;
            break;
        case DEBUG_LINE_CODE:
            if (core_internal.Exit_flag)
                return false;
            if (pDbgExpSource)
                break;
            if (bDebugExpressionRun)
                break;
#ifdef _WIN32 // S_DEBUG
            memcpy(&nDebugTraceLineCode, &pCodeBase[ip], sizeof(uint32_t));
            if (bTraceMode)
            {
                if (CDebug->GetTraceMode() == TMODE_MAKESTEP || CDebug->GetTraceMode() == TMODE_MAKESTEP_OVER)
                {
                    if (CDebug->GetTraceMode() == TMODE_MAKESTEP_OVER && bDebugWaitForThisFunc == false)
                        break;

                    if (!CDebug->IsDebug())
                        CDebug->OpenDebugWindow(core_internal.GetAppInstance());
                    // else
                    ShowWindow(CDebug->GetWindowHandle(), SW_NORMAL);

                    CDebug->SetTraceLine(nDebugTraceLineCode);
                    CDebug->BreakOn(pfi->decl_file_name.c_str(), nDebugTraceLineCode);
                    CDebug->SetTraceMode(TMODE_WAIT);
                    while (CDebug->GetTraceMode() == TMODE_WAIT)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(40));
                    }
                    if (CDebug->GetTraceMode() == TMODE_MAKESTEP_OVER)
                        bDebugWaitForThisFunc = true;
                    else
                        bDebugWaitForThisFunc = false;
                }
                else if (CDebug->Breaks.CanBreak())
                {
                    // check for breakpoint
                    if (CDebug->Breaks.Find(pfi->decl_file_name.c_str(), nDebugTraceLineCode))
                    {
                        if (!CDebug->IsDebug())
                            CDebug->OpenDebugWindow(core_internal.GetAppInstance());

                        ShowWindow(CDebug->GetWindowHandle(), SW_NORMAL);
                        // CDebug->OpenDebugWindow(core_impl.hInstance);
                        CDebug->SetTraceMode(TMODE_WAIT);
                        CDebug->BreakOn(pfi->decl_file_name.c_str(), nDebugTraceLineCode);

                        while (CDebug->GetTraceMode() == TMODE_WAIT)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(40));
                        } // wait for debug thread decision
                        if (CDebug->GetTraceMode() == TMODE_MAKESTEP_OVER)
                            bDebugWaitForThisFunc = true;
                        else
                            bDebugWaitForThisFunc = false;
                    }
                }
            }
#endif
            break;
        case DEBUG_FILE_NAME:

            /*if(!bTraceMode) break;

            int32_t index;
            int32_t file_line_offset;
            int32_t file_name_size;
            index = ip;
            memcpy(&file_line_offset,&pCodeBase[index],sizeof(uint32_t)); index += sizeof(uint32_t);
            memcpy(&file_name_size,&pCodeBase[index],sizeof(uint32_t)); index += sizeof(uint32_t);
            memcpy(DebugTraceFileName,&pCodeBase[index],file_name_size); index += file_name_size;
            DebugTraceFileName[file_name_size] = 0;
            //DebugSourceLine = 1;*/
            break;
        case BLOCK_IN:
        case BLOCK_OUT:
            // case DEBUG_FILE_NAME:
            SetError("Unallowed instraction");
            return false;
            break;
        default:
            SetError("Incorrect instruction code");
            return false;

        case CALL:                                                  // undetermined function call
            vtype = BC_TokenGet();                                  // read variable
            var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]); // var code
            if (vtype == VARIABLE)
            {
                real_var = VarTab.GetVar(var_code);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVV = real_var->value.get();
            }
            else
            {
                pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pVV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            pVV = pVV->GetVarPointer();
            if (pVV->GetType() != VAR_STRING && pVV->GetType() != VAR_FREF)
            {
                SetError("string call argument isnt string var");
                return false;
            }
            if (pVV->GetType() == VAR_FREF)
            {
                if (pVV->sValue.empty())
                {
                    SetError("call through a null function reference");
                    return false;
                }
                pAccess_string = pVV->sValue.c_str(); // a function reference holds the name
            }
            else
                pVV->Get(pAccess_string); // func name
            func_code = FuncTab.FindFunc(pAccess_string);
            if (func_code == INVALID_FUNC_CODE)
            {
                SetError("function '%s' not found", pAccess_string);
                return false;
            }
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            StoreDynamicCallStatus(pVResult != nullptr);
            break;
        case CALL_FUNCTION:
            memcpy(&func_code, &pCodeBase[ip], sizeof(uint32_t));
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            // if(pVResult) SStack.Pop();
            break;
        case FUNCTION_RETURN_VOID:
            if (frame.return_type != TVOID)
            {
                SetError("function must return value");
                return false;
            }
            // for(n=0;n<fi.var_num;n++) SStack.Pop();
            SStack.InvalidateFrom(frame.stack_offset);

            return true;
        case FUNCTION_RETURN:
            if (frame.return_type == TVOID)
            {
                SetError("void function return value");

                return false;
            }
            // at this moment result expression placed in EX register

            if (pDbgExpSource == nullptr) // skip stack unwind for dbg expression process    // ????????
                SStack.InvalidateFrom(frame.stack_offset);

            // copy result into stack
            pV = SStack.Push();
            pV->Copy(&ExpressionResult);
            pVReturnResult = pV;

            // check return type
            if (pDbgExpSource == nullptr) // skip test for dbg expression process
                if (frame.return_type != pV->GetType())
                {
                    if (frame.return_type == VAR_INTEGER && pV->GetType() == VAR_PTR)
                    {
                        pV->Convert(VAR_INTEGER);
                        return true;
                    }

                    SetError("%s function return %s value", Token.GetTypeName(frame.return_type),
                             Token.GetTypeName(pV->GetType()));
                    return false;
                }
            return true;
            /*if(BC_ProcessExpression(&ExpressionResult))
            {
              if(pDbgExpSource == 0)    // skip stack unwind for dbg expression process
                SStack.InvalidateFrom(frame.stack_offset);
              pV = SStack.Push();
              pV->Copy(&ExpressionResult);
              pVReturnResult = pV;

              if(pDbgExpSource == 0)    // skip test for dbg expression process
              if(frame.return_type != pV->GetType())
              {
                SetError("%s function return %s value",
                  Token.GetTypeName(frame.return_type),
                  Token.GetTypeName(pV->GetType()));

                return false;
              }
            }
            else
            {
              SetError("incorrect return expression");

              return false;
            }*/

            // case PROCESS_EXPRESSION:
            // BC_ProcessExpression(&ExpressionResult);
            // break;
            /*case WHILE_BLOCK:
              if(BC_TokenGet(ip,token_data_size) != OPEN_BRACKET){SetError("missing '('"); return false;};
              BC_ProcessExpression(&ExpressionResult);
            break;
            case IF_BLOCK:
              if(BC_TokenGet(ip,token_data_size) != OPEN_BRACKET){SetError("missing '('"); return false;};
              BC_ProcessExpression(&ExpressionResult);
            break;
            case SWITCH_COMMAND:
              if(BC_TokenGet(ip,token_data_size) != OPEN_BRACKET){SetError("missing '('"); return false;};
              BC_ProcessExpression(&ExpressionResult);
            break;*/

        case OP_MULTIPLYEQ:
        case OP_DIVIDEEQ:
        case OP_INCADD:
        case OP_DECADD:
        case OP_OREQ:
        case OP_ANDEQ:
            S_TOKEN_TYPE OpType;
            OpType = Token_type;
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
            }
            // BC_ProcessExpression(&ExpressionResult);
            pVV = SStack.Read();
            switch (OpType)
            {
            case OP_INCADD:
                pV->Plus(pVV);
                break;
            case OP_DECADD:
                pV->Minus(pVV);
                break;
            case OP_MULTIPLYEQ:
                pV->Multiply(pVV);
                break;
            case OP_DIVIDEEQ:
                pV->Divide(pVV);
                break;
            case OP_OREQ:
            case OP_ANDEQ: {
                const int32_t a = IntValue(pV), b = IntValue(pVV);
                pV->Set(OpType == OP_OREQ ? (a | b) : (a & b));
                break;
            }
            }

            break; //*/

            /*case OP_MULTIPLYEQ:
            case OP_DIVIDEEQ:
            case OP_INCADD:
            case OP_DECADD:
              S_TOKEN_TYPE OpType;
              OpType = Token_type;
              if(nLeftOperandCode == INVALID_VAR_CODE) { SetError("no Lvalue"); return false;    }
              if(bLeftOperandType == VARIABLE)
              {
                if(!VarTab.GetVar(vi,nLeftOperandCode))    { SetError("Global variable not found"); return false; }
                pV = vi.pDClass;
              }
              else
              {
                pV = SStack.Read(pRun_fi->stack_offset,nLeftOperandCode);
                if(pV == 0) { SetError("Local variable not found"); return false; }
              }
              if(nLeftOperandIndex != INVALID_ARRAY_INDEX)
              {
                pV = (DATA*)pV->GetArrayElement(nLeftOperandIndex);
                if(pV == 0) { SetError("invalid array index"); return false; }

              }
              BC_ProcessExpression(&ExpressionResult);
              switch(OpType)
              {
                case OP_INCADD:        pV->Plus(&ExpressionResult); break;
                case OP_DECADD:        pV->Minus(&ExpressionResult); break;
                case OP_MULTIPLYEQ: pV->Multiply(&ExpressionResult); break;
                case OP_DIVIDEEQ: pV->Divide(&ExpressionResult); break;
              }

            break;//*/
        case OP_INC:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
                pV->Inc();
            else
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
                pV->Inc();
            }
            break;
        case OP_DEC:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
                pV->Dec();
            else
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
                pV->Dec();
            }
            break;
            /*case OP_EQUAL:
              if(nLeftOperandCode == INVALID_VAR_CODE) { SetError("no Lvalue"); return false;    }
              if(BC_ProcessExpression(&ExpressionResult))
              {
                if(pLeftOperandAClass != 0)
                {
                  // set attribute class value
                  char * pChar;
                  // 2 signs precision --------------------------------
                    if(ExpressionResult.GetType() == VAR_FLOAT)
                    {
                      float fV1;
                      ExpressionResult.Get(fV1);
                      sprintf_s(gs,"%.7f",fV1);
                      ExpressionResult.Set(gs);

                    }
                  // 2 signs precision --------------------------------
                  ExpressionResult.Convert(VAR_STRING);
                  ExpressionResult.Get(pChar);
                  pLeftOperandAClass->SetValue(pChar);


                  if(bLeftOperandType == VARIABLE)
                  {
                    if(!VarTab.GetVar(vi,nLeftOperandCode))    { SetError("Global variable not found"); return false; }
                    pV = vi.pDClass;
                  }
                  else
                  {
                    pV = SStack.Read(pRun_fi->stack_offset,nLeftOperandCode);
                    if(pV == 0) { SetError("Local variable not found"); return false; }
                  }
                  if(nLeftOperandIndex != INVALID_ARRAY_INDEX) pV = (DATA*)pV->GetArrayElement(nLeftOperandIndex);
                  if(pV == 0) { SetError("bad array element"); return false; }
                  pV->Get(eid);
                  //api->Entity_AttributeChanged(&eid,pLeftOperandAClass->GetThisName());
                  if(bEntityUpdate) core_internal.Entity_AttributeChanged(&eid,pLeftOperandAClass);
                  break;
                }
                // copy value to variable
                if(bLeftOperandType == VARIABLE)
                {
                  if(!VarTab.GetVar(vi,nLeftOperandCode))    { SetError("Global variable not found"); return false; }
                  pV = vi.pDClass;
                }
                else
                {
                  pV = SStack.Read(pRun_fi->stack_offset,nLeftOperandCode);
                  if(pV == 0) { SetError("Local variable not found"); return false; }
                }

                if(pV->GetType() == VAR_REFERENCE && ExpressionResult.GetType() == VAR_REFERENCE)
                {
                  pV->SetReference(ExpressionResult.GetReference());
                  break;
                }

                pVV = ExpressionResult.GetVarPointer();


                DATA * pV1;
                pV1 = pV->GetVarPointer();
                if(pV1->GetType() != pVV->GetType())
                {
                  // ??? think about a little later
                  if(ExpressionResult.IsReference()) ExpressionResult.RefConvert();
                  if(!ExpressionResult.Convert(pV1->GetType()))    /// ??????????
                  {
                    break;    // skip unallowed conversion, write to log(in Convert func)
                  }
                  pVV = ExpressionResult.GetVarPointer();    // ??????????? ->
                  //ExpressionResult.RefConvert();
                  //if(!pVV->Convert(ExpressionResult.GetType()))
                  //{
                  //    break;    // skip unallowed conversion, write to log(in Convert func)
                  //}
                }

                if(nLeftOperandIndex == INVALID_ARRAY_INDEX)
                {
                  pV->Copy(pVV);
                }
                else
                {
                  pV->CopyOnElement(pVV,nLeftOperandIndex);
                }
              }
            break;*/
            /*case SQUARE_OPEN_BRACKET:
              if(!BC_ProcessExpression(&ExpressionResult)) { SetError("Invalid array index"); return false; }
              if(!ExpressionResult.Get(nLeftOperandIndex)) { SetError("Invalid array index"); return false; }
            break;*/

            /*case MAKEREF_COMMAND:
              if(BC_TokenGet() != OPEN_BRACKET) {SetError("missed '('"); return false;};
              Token_type = BC_TokenGet(ip,token_data_size);
              if(Token_type == VARIABLE)
              {
                if(!VarTab.GetVar(vi,*((uint32_t *)&pCodeBase[ip]))) { SetError("Global variable not found"); break; }
                pRef = vi.pDClass;
              }
              else
              {
                pRef = SStack.Read(pRun_fi->stack_offset,*((uint32_t *)&pCodeBase[ip]));
                if(pRef == 0) { SetError("Local variable not found"); return false; }
              }
              if(!pRef->IsReference()) { SetError("makeref to non reference variable"); return false; }

              if(BC_TokenGet() != COMMA) {SetError("missed ','"); return false;};

              Token_type = BC_TokenGet(ip,token_data_size);
              if(Token_type == VARIABLE)
              {
                if(!VarTab.GetVar(vi,*((uint32_t *)&pCodeBase[ip])))    { SetError("Global variable not found"); break;
            } pVar = vi.pDClass;
              }
              else
              {
                pVar = SStack.Read(pRun_fi->stack_offset,*((uint32_t *)&pCodeBase[ip]));
                if(pVar == 0) { SetError("Local variable not found"); return false; }
              }

              if(BC_TokenGet()== SQUARE_OPEN_BRACKET)
              {
                if(!BC_ProcessExpression(&ExpressionResult)) { SetError("Invalid array index"); return false; }
                if(!ExpressionResult.Get(array_index)) { SetError("Invalid array index"); return false; }
                if(TokenType()!= SQUARE_CLOSE_BRACKET){ SetError("missed ']'"); return false; }
                BC_TokenGet();
                pVar = (DATA *)pVar->GetArrayElement(array_index);
              }
              pRef->SetReference(pVar->GetVarPointer());
              if(TokenType() != CLOSE_BRACKET) {SetError("missed ')'"); return false;};
            break;

            case MAKEAREF_COMMAND:
              Token_type = Token_type;

            break;

              if(BC_TokenGet() != OPEN_BRACKET) {SetError("missed '('"); return false;};
              Token_type = BC_TokenGet(ip,token_data_size);
              if(Token_type == VARIABLE)
              {
                if(!VarTab.GetVar(vi,*((uint32_t *)&pCodeBase[ip]))) { SetError("Global variable not found"); break; }
                pRef = vi.pDClass;
              }
              else
              {
                pRef = SStack.Read(pRun_fi->stack_offset,*((uint32_t *)&pCodeBase[ip]));
                if(pRef == 0) { SetError("Local variable not found"); return false; }
              }
              if(!pRef->IsAReference()) { SetError("makeref to non A reference variable"); return false; }

              if(BC_TokenGet() != COMMA) {SetError("missed ','"); return false;};

              Token_type = BC_TokenGet(ip,token_data_size);
              if(Token_type == VARIABLE)
              {
                if(!VarTab.GetVar(vi,*((uint32_t *)&pCodeBase[ip])))    { SetError("Global variable not found"); break;
            } pVar = vi.pDClass; if(pVar == 0) { SetError("Global variable not found"); return false; }
              }
              else
              {
                pVar = SStack.Read(pRun_fi->stack_offset,*((uint32_t *)&pCodeBase[ip]));
                if(pVar == 0) { SetError("Local variable not found"); return false; }
              }

              if(BC_TokenGet()== SQUARE_OPEN_BRACKET)
              {
                if(!BC_ProcessExpression(&ExpressionResult)) { SetError("Invalid array index"); return false; }
                if(!ExpressionResult.Get(array_index)) { SetError("Invalid array index"); return false; }
                if(TokenType()!= SQUARE_CLOSE_BRACKET){ SetError("missed ']'"); return false; }
                BC_TokenGet();
                pVar = (DATA*)pVar->GetArrayElement(array_index);
                if(pVar == 0) { SetError("Invalid array index"); return false; }
              }
              pRoot = pVar->GetAClass();
              if(pRoot == 0) {SetError("AClass ERROR n1"); return false; }
              //while(TokenType() == ACCESS_WORD)
              //{
              //    pRoot = pRoot->CreateSubAClass(pRoot,(char *)&pRunCodeBase[TLR_DataOffset]);
              //    BC_TokenGet();
              //}
              while(TokenType() == ACCESS_WORD_CODE || TokenType() == ACCESS_WORD || TokenType() == ACCESS_VAR)
              {
                if(TokenType() == ACCESS_WORD_CODE)
                {
                  pRoot = pRoot->VerifyAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                  // pRoot = pRoot->CreateSubAClass(pRoot,(char *)&pRunCodeBase[TLR_DataOffset]);
                } else
                if(TokenType() == ACCESS_WORD)
                {
                  pRoot = pRoot->CreateSubAClass(pRoot,(char *)&pRunCodeBase[TLR_DataOffset]);
                } else
                if(TokenType() == ACCESS_VAR)
                {
                  vtype = BC_TokenGet();
                  var_code = *((int32_t *)&pRunCodeBase[TLR_DataOffset]);
                  if(!(vtype == VARIABLE || vtype == LOCAL_VARIABLE)){SetError("invalid access var");return false;}
                  if(vtype == VARIABLE)
                  {
                    if(!VarTab.GetVar(vi,var_code))    { SetError("Global variable not found"); return false; }
                    pVV = vi.pDClass;
                  }
                  else
                  {
                    pVV = SStack.Read(pRun_fi->stack_offset,var_code);
                    if(pVV == 0) { SetError("Local variable not found"); return false; }
                  }
                  pVV->Get(pAccess_string);
                  pRoot = pRoot->CreateSubAClass(pRoot,pAccess_string);
                }

                BC_TokenGet();
              }
              pRef->SetAReference(pRoot);

              pVar->Get(eid);
              pRef->Set(eid);
              if(TokenType() != CLOSE_BRACKET) {SetError("missed ')'"); return false;};
            break;*/

            //-----------------------------------------------------------------------

        case GOTO_COMMAND:
            break;
        case CONTINUE_COMMAND:
            break;
        case BREAK_COMMAND:
            break;

        case CASE_COMMAND:
            return false;

        case SEPARATOR:
            break;
        case UNKNOWN:
            break;
        case VAR_OBJECT:
        case VAR_STRING:
        case VAR_FLOAT:
        case VAR_INTEGER:
        case VAR_PTR:
            break;
        case OPEN_BRACKET:
            break;
        case CLOSE_BRACKET:
            break;
        case COMMA:

            break;
        case DOT:

            break;
            /*            // off start whean ready compile expression
                  case OP_BOOL_EQUAL:
                  case OP_GREATER:
                  case OP_GREATER_OR_EQUAL:
                  case OP_LESSER:
                  case OP_LESSER_OR_EQUAL:
                  case OP_NOT_EQUAL:
                  case OP_MINUS:
                  case OP_PLUS:
                  case OP_MULTIPLY:
                  case OP_DIVIDE:
                  case OP_POWER:
                  case OP_MODUL:


                  break;//*/

            //----------------------------------------------
        case NUMBER:

            break;
        case FLOAT_NUMBER:

            break;
        case STRING:

            break;
            //----------------------------------------------

        case PUSH_OBJID_BXINDEX:
        case PUSH_OBJID:

            if (Token_type == PUSH_OBJID_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pV = SStack.Push();
            pV->ClearType();

            pVV = GetOperand(pCodeBase, ip, &Token_type);

            if (!(Token_type == VARIABLE || Token_type == LOCAL_VARIABLE))
            {
                SetError("invalid arg for push_objid");
                return false;
            }
            if (bUseIndex)
            {
                pVV = pVV->GetArrayElement(dwBXIndex);
            }

            if (!(pVV->GetType() == VAR_AREFERENCE || pVV->GetType() == VAR_REFERENCE || pVV->GetType() == VAR_OBJECT))
            {
                SetError("not aref or ref or object");
            }

            pVV->Get(eid);
            pV->Set(eid);
            break;
            // process expression section ------------------

        case SETAREF:

            pV = SStack.Pop();
            pV->Get(eid);

            // destination
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVDst = SStack.Read();
                break;
            default:
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            if (!pVDst)
            {
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            pVDst = pVDst->GetVarPointer();
            if (!pVDst)
            {
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            if (pVDst->GetType() != VAR_AREFERENCE)
            {
                SetError("isnt aref");
                return false;
            }

            // source
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case AP:
                pVDst->Set(eid);
                pVDst->SetAReference(rAP);

                break;
            default:
                SetError("invalid argument for setref (src)");
                return false;
            }

            break;
        case SETREF_BXINDEX:
        case SETREF:

            if (Token_type == SETREF_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pVDst = nullptr;
            pVSrc = nullptr;

            // destination
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVDst = SStack.Read();
                pVDst->ClearType();
                pVDst->SetType(VAR_REFERENCE);
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(*((uint32_t *)&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVDst = real_var->value.get();
                // if(bUseIndex) pVDst = pVDst->GetArrayElement(dwBXIndex);
                if (!pVDst)
                    return false;
                if (pVDst->GetType() != VAR_REFERENCE)
                {
                    SetError("'%s' isnt reference", real_var->name.c_str());
                    return false;
                }
                break;
            case LOCAL_VARIABLE:
                pVDst = SStack.Read(pRun_fi->stack_offset, *((uint32_t *)&pCodeBase[ip]));
                if (pVDst == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                // if(bUseIndex) pVDst = pVDst->GetArrayElement(dwBXIndex);
                if (!pVDst)
                    return false;
                if (pVDst->GetType() != VAR_REFERENCE)
                {
                    SetError("Local variable is not reference");
                    return false;
                }
                break;
            default:
                SetError("invalid argument for setref (dst)");
                return false;
            }
            if (!pVDst)
            {
                SetError("invalid argument for setref (dst)");
                return false;
            }

            // source
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case VARIABLE:
                real_var = VarTab.GetVar(*((uint32_t *)&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVSrc = real_var->value.get();
                if (bUseIndex)
                    pVSrc = pVSrc->GetArrayElement(dwBXIndex);
                if (!pVSrc)
                    return false;
                break;
            case LOCAL_VARIABLE:
                pVSrc = SStack.Read(pRun_fi->stack_offset, *((uint32_t *)&pCodeBase[ip]));
                if (pVSrc == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                if (bUseIndex)
                    pVSrc = pVSrc->GetArrayElement(dwBXIndex);
                if (!pVSrc)
                    return false;
                break;
            default:
                SetError("invalid argument for setref (src)");
                return false;
            }

            pVDst->SetReference(pVSrc->GetVarPointer());

            break;
        case MOVE:

            pVDst = nullptr;
            pVSrc = nullptr;
            if (pLeftOperandElement)
            {
                // a[i][j] = value
                BC_TokenGet(ip, token_data_size); // LEFT_OPERAND
                if (BC_TokenGet(ip, token_data_size) != STACK_TOP)
                {
                    SetError("incorrect SRC");
                    return false;
                }
                pVSrc = SStack.Read();
                if (pVSrc->GetType() == VAR_REFERENCE && pLeftOperandElement->GetType() == VAR_REFERENCE)
                    pLeftOperandElement->SetReference(pVSrc->GetReference());
                else
                    AssignValue(pLeftOperandElement->GetVarPointer(), pVSrc);
                break;
            }
            // find destination
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case LEFT_OPERAND:
                if (nLeftOperandCode == INVALID_VAR_CODE)
                {
                    SetError("no Lvalue");
                    return false;
                }

                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pVDst = real_var->value.get();
                }
                else
                {
                    if (pRun_fi)
                    {
                        pVDst = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pVDst == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    else
                    {
                        pVDst = nullptr;
                        SetError("Internal error, line 4808! Current function not defined...");
                        return false;
                    }
                }
                break;
                /*case EX:
                  ExpressionResult.ClearType();
                  pVDst = &ExpressionResult;

                break;*/
            default:
                SetError("invalid DST");
                return false;
            }

            if (pVDst == nullptr)
            {
                SetError("no LValue");
                return false;
            }

            // find source
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVSrc = SStack.Read();
                if (!pVSrc)
                    return false;
                break;
            default:
                SetError("incorrect SRC");
                return false;
            }

            // check for attribute operations
            if (pLeftOperandAClass != nullptr)
            {
                // set attribute class value
                const char *pChar;

                // use ExpressionResult as temporary register
                ExpressionResult.ClearType();
                ExpressionResult.Copy(pVSrc);

                // 2 signs precision --------------------------------
                if (ExpressionResult.GetType() == VAR_FLOAT)
                {
                    float fV1;
                    ExpressionResult.Get(fV1);
                    sprintf_s(gs, "%.7f", fV1);
                    ExpressionResult.Set(gs);
                }
                // 2 signs precision --------------------------------
                ExpressionResult.Convert(VAR_STRING);
                if (!ExpressionResult.Get(pChar))
                    break;
                pLeftOperandAClass->SetValue(pChar);

                if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
                {
                    // the element of an object array, directly or through a reference to it
                    pVDst = pVDst->GetVarPointer();
                    if (pVDst)
                        pVDst = pVDst->GetArrayElement(nLeftOperandIndex);
                }
                if (!pVDst)
                    return false;

                pVDst->Get(eid);
                if (bEntityUpdate)
                {
                    core_internal.Entity_AttributeChanged(eid, pLeftOperandAClass);
                }
                break;
            }

            if (pVSrc->GetType() == VAR_REFERENCE && nLeftOperandIndex != INVALID_ARRAY_INDEX)
            {
                // refs[i] = reference: an element of an array of references (directly or through
                // a reference to it) refers to the value
                DATA *container = pVDst->IsArray() ? pVDst : pVDst->GetVarPointer();
                DATA *element = container && container->IsArray() ? container->GetArrayElement(nLeftOperandIndex)
                                                                  : nullptr;
                if (element && element->GetType() == VAR_REFERENCE)
                {
                    element->SetReference(pVSrc->GetReference());
                    break;
                }
            }
            // test for references operations
            if (pVSrc->GetType() == VAR_REFERENCE && pVDst->GetType() == VAR_REFERENCE)
            {
                pVDst->SetReference(pVSrc->GetReference());
                break;
            }

            // find real dst
            pVDst = pVDst->GetVarPointer();
            if (!pVDst)
            {
                SetError("no DST");
                return false;
            }

            if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
                pVDst = pVDst->GetArrayElement(nLeftOperandIndex);
            if (!pVDst)
                return false;

            // test for allowed data conversion
            DATA *pVTTSrc;
            DATA *pVTTDst;
            pVTTSrc = pVSrc->GetVarPointer();
            pVTTDst = pVDst->GetVarPointer();

            if (pVTTSrc->GetType() != pVTTDst->GetType())
            {
                // ??? think about a little later
                ExpressionResult.ClearType();
                ExpressionResult.Copy(pVSrc);
                if (ExpressionResult.IsReference())
                    ExpressionResult.RefConvert();
                if (!ExpressionResult.Convert(pVTTDst->GetType())) /// ??????????
                {
                    break; // skip unallowed conversion, write to log(in Convert func)
                }
                pVSrc = ExpressionResult.GetVarPointer(); // converted source
            }

            // if not attribute operation proceed to variables copy

            // if(nLeftOperandIndex != INVALID_ARRAY_INDEX) pVDst = pVDst->GetArrayElement(nLeftOperandIndex);
            // if(!pVDst) return false;
            pVDst->Copy(pVSrc);

            break;

        case STACK_ALLOC:
            SStack.Push();
            break;

        case STACK_WRITE_BXINDEX:
        case STACK_PUSH:
        case STACK_WRITE:

            if (Token_type == STACK_WRITE_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            if (Token_type == STACK_PUSH)
                pV = SStack.Push();
            else
                pV = SStack.Read();

            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case NUMBER:
                pV->Set(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                break;
            case FLOAT_NUMBER:
                pV->Set(*((float *)&pRunCodeBase[TLR_DataOffset]));
                break;
            case STRING:
                pV->Set((char *)&pRunCodeBase[TLR_DataOffset + 4]); // 4 - string length
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(*((uint32_t *)&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                pVV = real_var->value.get();
                // pVV = pVV->GetVarPointer();
                // if(!pVV) { SetError("invalid ref"); break; }
                if (pVV->IsReference() && !pVV->IsArray()) // an array of references indexes as any array
                {
                    if (bUseIndex)
                    {
                        pVV = pVV->GetVarPointer();
                        pVV = pVV->GetArrayElement(dwBXIndex);
                        if (!pVV)
                        {
                            SetError("invalid ref");
                            break;
                        }

                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVV);
                    }
                    else
                    {
                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVV->GetReference());
                    }
                    break;
                }

                if (bUseIndex)
                {
                    pVV = pVV->GetArrayElement(dwBXIndex);
                    if (!pVV)
                    {
                        SetError("invalid array index");
                        break;
                    }
                    pV->Copy(pVV);
                }
                else
                    pV->Copy(pVV);

                break;
            case LOCAL_VARIABLE:
                pVar = SStack.Read(pRun_fi->stack_offset, *((uint32_t *)&pCodeBase[ip]));
                if (pVar == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                if (pVar->IsReference() && !pVar->IsArray()) // an array of references indexes as any array
                {
                    if (bUseIndex)
                    {
                        pVar = pVar->GetVarPointer();
                        pVar = pVar->GetArrayElement(dwBXIndex);
                        if (!pVar)
                        {
                            SetError("invalid ref");
                            break;
                        }

                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVar);
                    }
                    else
                    {
                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVar->GetReference());
                    }
                    break;
                }

                // if(!pVar) { SetError("invalid ref"); break; }

                if (bUseIndex)
                {
                    pVar = pVar->GetArrayElement(dwBXIndex);
                    if (!pVar)
                    {
                        SetError("invalid array index");
                        break;
                    }
                    pV->Copy(pVar);
                }
                else
                    pV->Copy(pVar);

                break;
            case AX:
                pV->Copy(&rAX);
                break;
            case BX:
                pV->Copy(&rBX);
                break;
            case EX:
                pV->Copy(&ExpressionResult);
                break;
            case AP_VALUE:
                if (!rAP)
                {
                    SetError("no rAP data");
                    pV->Set("error");
                    break; /*return false;*/
                }
                pV->Set(to_string(rAP->GetThisAttr()));
                break;
            default:
                SetError("invalid argument for STACK_PUSH");
                return false;
            }
            break;

        case STACK_POP_VOID:
            SStack.Pop();
            break;
        case STACK_POP:
        case STACK_READ:
            if (SStack.GetDataNum() == 0)
                break;
            if (Token_type == STACK_POP)
                pV = SStack.Pop();
            else
                pV = SStack.Read();

            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case VARIABLE:
                real_var = VarTab.GetVar(*((uint32_t *)&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                real_var->value->Copy(pV);
                break;
            case LOCAL_VARIABLE:
                pVar = SStack.Read(pRun_fi->stack_offset, *((uint32_t *)&pCodeBase[ip]));
                if (pVar == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                pVar->Copy(pV);
                break;
            case AX:
                rAX.ClearType();
                rAX.Copy(pV);
                break;
            case BX:
                rBX.ClearType();
                rBX.Copy(pV);
                break;
            case EX:
                ExpressionResult.ClearType();
                ExpressionResult.Copy(pV);
                break;
            case LEFTOP_INDEX:
                pV->Convert(VAR_INTEGER);
                pV->Get(nLeftOperandIndex);
                break;
            default:
                SetError("invalid argument for STACK_POP");
                return false;
            }
            break;

        case OP_BIT_OR:
        case OP_BIT_AND:
        case OP_SHL:
        case OP_SHR: {
            pV = GetOperand(pCodeBase, ip);
            pVV = GetOperand(pCodeBase, ip);
            if (!pV || !pVV)
                return false;
            const auto a = static_cast<uint32_t>(IntValue(pV));
            const auto b = static_cast<uint32_t>(IntValue(pVV));
            uint32_t r = 0;
            switch (Token_type)
            {
            case OP_BIT_OR:
                r = a | b;
                break;
            case OP_BIT_AND:
                r = a & b;
                break;
            case OP_SHL:
                r = b < 32 ? a << b : 0;
                break;
            default:
                r = b < 32 ? a >> b : 0;
                break;
            }
            pV->ClearType();
            pV->Set(static_cast<int32_t>(r));
            break;
        }
        case OP_BIT_NOT:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            lvalue = ~IntValue(pV);
            pV->ClearType();
            pV->Set(lvalue);
            break;
        case OP_CAST: {
            // int(x) float(x) string(x) bool(x) fref(x): a converted copy of the value on the stack top
            uint32_t target;
            memcpy(&target, &pCodeBase[ip], sizeof(uint32_t));
            pV = SStack.Read();
            if (!CastValue(pV, static_cast<S_TOKEN_TYPE>(target)))
                return false;
            break;
        }
        case PUSH_NULL:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            break;
        case OP_IS_NULL: {
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            const bool null = pV->IsNull();
            pV->ClearType();
            pV->Set(static_cast<int32_t>(null));
            break;
        }
        case OP_TYPEID: {
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            const int32_t id = TypeidOf(pV);
            pV->ClearType();
            pV->Set(id);
            break;
        }
        case PUSH_AP:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_AREFERENCE);
            pV->SetAReference(rAP);
            break;
        case ATTR_DEFAULT: {
            // stack: attribute reference, default value
            uint32_t target;
            memcpy(&target, &pCodeBase[ip], sizeof(uint32_t));
            pVV = SStack.Pop(); // default
            pV = SStack.Read(); // attribute reference -> result
            ATTRIBUTES *attribute = pV->AttributesClass; // the attribute reference PUSH_AP made, null if missing
            if (attribute != nullptr)
            {
                pV->ClearType();
                pV->Set(to_string(attribute->GetThisAttr()));
            }
            else
            {
                pV->ClearType();
                pV->Copy(pVV);
            }
            if (!CastValue(pV, static_cast<S_TOKEN_TYPE>(target)))
                return false;
            break;
        }
        case ADVANCE_AP_QUIET: {
            // as ADVANCE_AP without creating or reporting missing attributes: rAP becomes null
            Token_type = BC_TokenGet();
            const char *name = nullptr;
            if (Token_type == ACCESS_WORD_CODE)
            {
                if (rAP)
                    rAP = rAP->GetAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                break;
            }
            pVV = Token_type == VARIABLE ? VarTab.GetVar(*((int32_t *)&pRunCodeBase[TLR_DataOffset]))->value.get()
                                         : SStack.Read(pRun_fi->stack_offset, *((int32_t *)&pRunCodeBase[TLR_DataOffset]));
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pVV);
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(name))
            {
                SetError("invalid type for attribute var");
                return false;
            }
            if (rAP)
                rAP = rAP->FindAClass(rAP, name);
            break;
        }
        case OP_IN: {
            // stack: key, object reference / attribute reference -> 1 if the attribute (path) exists
            pVV = SStack.Pop();
            pV = SStack.Read();
            ATTRIBUTES *root = AttributesOf(pVV);
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pV);
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("'in': the key must be a string");
                return false;
            }
            const bool found = root != nullptr && root->FindAClass(root, key) != nullptr;
            pV->ClearType();
            pV->Set(static_cast<int32_t>(found));
            break;
        }
        case OP_SIZEOF_REF: {
            // @x: elements of an array, attributes of an object or attribute reference
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            DATA *target = Deref(pV);
            int32_t count = 0;
            if (target && target->IsArray())
                count = static_cast<int32_t>(target->GetElementsNum());
            else if (ATTRIBUTES *attributes = AttributesOf(pV))
                count = static_cast<int32_t>(attributes->GetAttributesNum());
            else if (!(target == nullptr || pV->IsNull() || (target && target->IsNull())))
            {
                // a null reference has no attributes: 0 (CL: x = a.b$aref; @x when b is missing)
                SetError("'@' of a %s value", Token.GetTypeName(target->GetType()));
                return false;
            }
            pV->ClearType();
            pV->Set(count);
            break;
        }
        case RESIZE_ARRAY: // @array = BX
            pV = GetOperand(pCodeBase, ip);
            if (!pV || !(pV = pV->GetVarPointer()) || !pV->IsArray())
            {
                SetError("'@x = size': x is not an array");
                return false;
            }
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            if (lvalue < 1)
            {
                SetError("array size %d", lvalue);
                return false;
            }
            pV->SetElementsNum(static_cast<uint32_t>(lvalue));
            break;
        case CALL_METHOD: {
            uint32_t args;
            const char *method = &pRunCodeBase[TLR_DataOffset + sizeof(uint32_t)];
            memcpy(&args, &pCodeBase[InstructionPointer + 2], sizeof(uint32_t)); // ARGS_NUM data
            func_code = FindMethod(SStack.Read(SStack.GetDataNum() - args, 0), method);
            if (func_code == INVALID_FUNC_CODE)
                return false;
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            StoreDynamicCallStatus(pVResult != nullptr);
            break;
        }
        case RANGE_NEXT: {
            // container, position, item, index (locals): the next element or attribute, EX = 0 at the end
            uint32_t slots[4];
            memcpy(slots, &pCodeBase[ip], sizeof(slots));
            DATA *container = SStack.Read(pRun_fi->stack_offset, slots[0]);
            DATA *position = SStack.Read(pRun_fi->stack_offset, slots[1]);
            DATA *item = SStack.Read(pRun_fi->stack_offset, slots[2]);
            const int32_t at = position->GetInt();
            DATA *array = container->GetType() == VAR_REFERENCE ? Deref(container) : nullptr;
            ATTRIBUTES *attributes = array ? nullptr : AttributesOf(container);
            const int32_t count = array ? (array->IsArray() ? static_cast<int32_t>(array->GetElementsNum()) : 1)
                                        : attributes ? static_cast<int32_t>(attributes->GetAttributesNum()) : 0;
            ExpressionResult.ClearType();
            if (at >= count)
            {
                ExpressionResult.Set(int32_t{0});
                break;
            }
            if (array)
            {
                DATA *element = array->IsArray() ? array->GetArrayElement(at) : array;
                if (item->GetType() == VAR_REFERENCE)
                    item->SetReference(element->GetVarPointer());
                else
                    item->Copy(element);
            }
            else
                item->SetAReference(attributes->GetAttributeClass(static_cast<uint32_t>(at)));
            if (slots[3] != INVALID_VAR_CODE)
            {
                // the key: the position, or for attributes into a string key the attribute's name
                DATA *key = SStack.Read(pRun_fi->stack_offset, slots[3]);
                if (!array && key->GetType() == VAR_STRING)
                    key->Set(attributes->GetAttributeClass(static_cast<uint32_t>(at))->GetThisName());
                else
                    key->Set(at);
            }
            position->Set(at + 1);
            ExpressionResult.Set(int32_t{1});
            break;
        }
        case OP_SLICE: {
            // stack: slot or text, [start], [end]; operand: the text (a variable or the stack top)
            uint32_t flags;
            memcpy(&flags, &pCodeBase[ip], sizeof(uint32_t));
            int32_t end = INT32_MAX, start = 0;
            if (flags & 2)
                end = IntValue(SStack.Pop());
            if (flags & 1)
                start = IntValue(SStack.Pop());
            const bool character = flags & 4; // text[i]
            pVV = GetOperand(pCodeBase, ip);
            pV = SStack.Read();
            if (!pVV || !pV)
                return false;
            ExpressionResult.ClearType();
            if (DATA *text = Deref(pVV))
                ExpressionResult.Copy(text);
            const char *chars = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(chars))
            {
                SetError("slice of a value that is not text");
                return false;
            }
            const auto length = static_cast<int32_t>(strlen(chars));
            auto clamp = [length](int32_t i) { return std::clamp(i < 0 ? length + i : i, 0, length); };
            start = clamp(start);
            end = character ? std::min(start + 1, length) : clamp(end);
            const std::string slice = start < end ? std::string(chars + start, end - start) : std::string();
            pV->ClearType();
            pV->Set(slice);
            break;
        }
        case ARRAY_BUILD: {
            // stack: count items -> an array of the first item's type
            uint32_t count;
            memcpy(&count, &pCodeBase[ip], sizeof(uint32_t));
            std::vector<DATA> items(count);
            for (uint32_t n = count; n-- > 0;)
            {
                items[n].SetVCompiler(this);
                DATA *value = Deref(SStack.Pop());
                if (!value)
                {
                    SetError("array literal item is a null reference");
                    return false;
                }
                items[n].Copy(value);
            }
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(count ? items[0].GetType() : VAR_INTEGER);
            pV->SetArraySize(count);
            for (uint32_t n = 0; n < count; n++)
                pV->GetArrayElement(n)->Copy(&items[n]);
            break;
        }
        case ADVANCE_AP_EXPR: {
            // stack: saved attribute reference, key -> rAP = its attribute named key
            uint32_t mode;
            memcpy(&mode, &pCodeBase[ip], sizeof(uint32_t));
            ExpressionResult.ClearType();
            ExpressionResult.Copy(SStack.Pop());
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("attribute name expression is not text");
                return false;
            }
            pV = SStack.Pop();
            rAP = pV->AttributesClass;
            if (!rAP)
            {
                if (mode != kAccessQuiet)
                    SetError("missed attribute before (%s)", key);
                break;
            }
            ATTRIBUTES *base = rAP;
            rAP = mode == kAccessCreate ? base->CreateSubAClass(base, key) : base->FindAClass(base, key);
            if (!rAP && mode == kAccessRead)
                SetError("missed attribute: %s", key);
            break;
        }
        case ACCESS_EXPR: {
            // left side: the attribute named by the text on the stack
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            ExpressionResult.ClearType();
            ExpressionResult.Copy(SStack.Pop());
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("attribute name expression is not text");
                return false;
            }
            if (pLeftOperandAClass == nullptr)
            {
                pV = bLeftOperandType == VARIABLE ? VarTab.GetVar(nLeftOperandCode)->value.get()
                                                  : SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                pLeftOperandAClass =
                    nLeftOperandIndex == INVALID_ARRAY_INDEX ? pV->GetAClass() : pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, key);
            break;
        }
        case CALL_NAMED: {
            // stack: function name, arguments: calls it; the result (if any) replaces the name
            uint32_t args;
            memcpy(&args, &pCodeBase[InstructionPointer + 2], sizeof(uint32_t)); // ARGS_NUM data
            const uint32_t name_slot = SStack.GetDataNum() - args - 1;
            DATA *name = Deref(SStack.Read(name_slot, 0));
            std::string function;
            if (name && name->GetType() == VAR_FREF)
                function = name->sValue;
            else if (name)
            {
                ExpressionResult.ClearType();
                ExpressionResult.Copy(name);
                const char *text = nullptr;
                if (ExpressionResult.Convert(VAR_STRING) && ExpressionResult.Get(text))
                    function = text;
            }
            func_code = function.empty() ? INVALID_FUNC_CODE : FuncTab.FindFunc(function);
            if (func_code == INVALID_FUNC_CODE)
            {
                SetError("call: function '%s' not found", function.c_str());
                return false;
            }
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            if (pVResult)
            {
                SStack.Read(name_slot, 0)->ClearType();
                SStack.Read(name_slot, 0)->Copy(pVResult);
            }
            SStack.Pop();
            StoreDynamicCallStatus(pVResult != nullptr);
            break;
        }
        case NEW_ARRAY: {
            // stack: size -> a reference to a new array of the type
            uint32_t type;
            memcpy(&type, &pCodeBase[ip], sizeof(uint32_t));
            pV = SStack.Read();
            const int32_t size = IntValue(pV);
            if (size < 0)
            {
                SetError("new array of %d elements", size);
                return false;
            }
            DATA &array = newArrays_.emplace_back();
            array.SetVCompiler(this);
            array.SetType(static_cast<S_TOKEN_TYPE>(type));
            array.SetArraySize(static_cast<uint32_t>(size));
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            pV->SetReference(&array);
            break;
        }
        case STACK_INDEX: {
            // the stack top (an array or a reference to one) becomes a reference to its element BX
            pV = SStack.Read();
            DATA *array = Deref(pV);
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            DATA *element = array && array->IsArray() ? array->GetArrayElement(lvalue) : nullptr;
            if (!element)
            {
                SetError("[%d]: not an array element", lvalue);
                return false;
            }
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            pV->SetReference(element);
            break;
        }
        case LEFTOP_SUBINDEX: {
            // left side a[i][j]: from the element a[i] (an array or a reference to one) to its element BX
            DATA *base = pLeftOperandElement;
            if (!base)
            {
                base = bLeftOperandType == VARIABLE ? VarTab.GetVar(nLeftOperandCode)->value.get()
                                                    : SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                DATA *container = base->IsArray() ? base : base->GetVarPointer();
                base = container && nLeftOperandIndex != INVALID_ARRAY_INDEX && container->IsArray()
                           ? container->GetArrayElement(nLeftOperandIndex)
                           : nullptr;
            }
            DATA *array = base ? Deref(base) : nullptr;
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            pLeftOperandElement = array && array->IsArray() ? array->GetArrayElement(lvalue) : nullptr;
            if (!pLeftOperandElement)
            {
                SetError("[%d]: not an array element", lvalue);
                return false;
            }
            break;
        }
        case AP_FROM_STACK: // rAP = the attributes of the value on the stack (popped)
            pV = SStack.Pop();
            rAP = pV ? AttributesOf(pV) : nullptr;
            if (!rAP)
                SetError("attribute of a call result that has no attributes");
            break;
        case AP_CHILD: // rAP = its BX-th attribute (aref[i])
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            rAP = rAP && lvalue >= 0 && static_cast<size_t>(lvalue) < rAP->GetAttributesNum()
                      ? rAP->GetAttributeClass(static_cast<uint32_t>(lvalue))
                      : nullptr;
            if (!rAP)
                SetError("aref[%d]: no such attribute", lvalue);
            break;
        case ATTR_PARENT: {
            pV = SStack.Read();
            ATTRIBUTES *attribute = AttributesOf(pV);
            ATTRIBUTES *parent = attribute ? attribute->GetParent() : nullptr;
            pV->ClearType();
            pV->SetType(VAR_AREFERENCE);
            pV->SetAReference(parent);
            break;
        }
        case ATTR_DELETE: {
            // leaves 0 for the statement form to drop
            pV = SStack.Read();
            ATTRIBUTES *attribute = AttributesOf(pV);
            ATTRIBUTES *parent = attribute ? attribute->GetParent() : nullptr;
            if (parent)
                parent->DeleteAttributeClassX(attribute);
            pV->ClearType();
            pV->Set(int32_t{0});
            break;
        }
        case OBJ_NEW:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_OBJECT);
            break;
        case OBJ_SET: {
            // stack: object, value
            const char *key = &pRunCodeBase[TLR_DataOffset + sizeof(uint32_t)];
            pVV = SStack.Pop();
            pV = SStack.Read();
            ATTRIBUTES *object = pV->GetAClass();
            if (!object)
            {
                SetError("object literal without attributes");
                return false;
            }
            DATA *value = Deref(pVV);
            if (value && value->GetType() == VAR_OBJECT && !value->IsArray())
            {
                ATTRIBUTES *child = object->CreateSubAClass(object, key);
                if (value->GetAClass())
                    *child = value->GetAClass()->Copy();
                break;
            }
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pVV);
            if (ExpressionResult.GetType() == VAR_FLOAT)
            {
                float f;
                ExpressionResult.Get(f);
                sprintf_s(gs, "%.7f", f);
                ExpressionResult.Set(gs);
            }
            const char *text = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(text))
            {
                SetError("object literal: '%s' has no text value", key);
                return false;
            }
            object->SetAttribute(key, text);
            break;
        }
        case SET_ARRAY_SIZE: // array local sized at run time: BX elements
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            if (lvalue < 0)
            {
                SetError("negative array size %d", lvalue);
                return false;
            }
            pV->SetArraySize(static_cast<uint32_t>(lvalue));
            break;
        case OP_BOOL_CONVERT:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->BoolConvert();
            break;
        case OP_BOOL_AND:
        case OP_BOOL_OR:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;

            pV->BoolConvert();
            pV->RefConvert();
            pV->CompareAndSetResult(pVV, Token_type);
            break;
        case OP_NOT_EQUAL:
        case OP_LESSER_OR_EQUAL:
        case OP_LESSER:
        case OP_GREATER_OR_EQUAL:
        case OP_GREATER:
        case OP_BOOL_EQUAL:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->CompareAndSetResult(pVV, Token_type);
            break;
        case OP_PLUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Plus(pVV);
            break;
        case OP_MINUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Minus(pVV);
            break;
        case OP_MULTIPLY:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Multiply(pVV);
            break;
        case OP_DIVIDE:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Divide(pVV);
            break;
        case OP_MODUL:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Modul(pVV);
            break;
        case OP_POWER:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->Power(pVV);
            break;
        case OP_SMINUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->Inverse();
            break;
        case OP_BOOL_NEG:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->Neg();
            break;

        case MOVEAP_BXINDEX:
        case MOVEAP:

            if (Token_type == MOVEAP_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pV = GetOperand(pCodeBase, ip, &Token_type);
            switch (Token_type)
            {
            case VARIABLE:
            case LOCAL_VARIABLE:
                SetError("not implemented");
                return false;
                break;
            case AP:
                pVV = GetOperand(pCodeBase, ip, &Token_type);
                if (Token_type == AP)
                    break;
                if (!pVV)
                    return false;
                if (bUseIndex)
                    pVV = pVV->GetArrayElement(dwBXIndex);
                if (!pVV)
                {
                    SetError("invalid array index");
                    return false;
                }
                rAP = pVV->GetAClass();
                break;
            }
            break;
        case VERIFY_AP:
        case ADVANCE_AP:

            S_TOKEN_TYPE sttV = Token_type;

            const char *pChar;
            if (!rAP)
            {
                SetError("null ap");
                BC_TokenGet();
                break; /*return false;*/
            }

            Token_type = BC_TokenGet();
            switch (Token_type)
            {
            case ACCESS_WORD_CODE:
                if (sttV == VERIFY_AP)
                    rAP = rAP->VerifyAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                else
                    rAP = rAP->GetAttributeClassByCode(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                if (!rAP)
                    SetError("missed attribute: %s", SCodec.Convert(*((int32_t *)&pRunCodeBase[TLR_DataOffset])));
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(*((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                ExpressionResult.ClearType();
                ExpressionResult.Copy(real_var->value.get());
                if (!ExpressionResult.Convert(VAR_STRING))
                {
                    SetError("invalid type for attribute var");
                    break; /*return false;*/
                }
                ExpressionResult.Get(pChar);
                if (sttV == VERIFY_AP)
                    rAP = rAP->CreateSubAClass(rAP, pChar);
                else
                    rAP = rAP->FindAClass(rAP, pChar);

                /*if(sttV == VERIFY_AP)
                rAP = rAP->VerifyAttributeClass(pChar);
                  else
                rAP = rAP->GetAttributeClass(pChar);*/
                if (!rAP)
                    SetError("missed attribute: %s", pChar);
                break;
            case LOCAL_VARIABLE:
                pV = SStack.Read(pRun_fi->stack_offset, *((int32_t *)&pRunCodeBase[TLR_DataOffset]));
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    break; /*return false;*/
                }

                ExpressionResult.ClearType();
                ExpressionResult.Copy(pV);
                if (!ExpressionResult.Convert(VAR_STRING))
                {
                    SetError("invalid type for attribute var");
                    break; /*return false;*/
                }
                ExpressionResult.Get(pChar);

                if (sttV == VERIFY_AP)
                    rAP = rAP->CreateSubAClass(rAP, pChar);
                else
                    rAP = rAP->FindAClass(rAP, pChar);

                /*if(sttV == VERIFY_AP)
                  rAP = rAP->VerifyAttributeClass(pChar);
                else
                rAP = rAP->GetAttributeClass(pChar);*/

                if (!rAP)
                    SetError("missed attribute: %s", pChar);

                break;
            default:
                SetError("invalid arg type for ADVANCE_AP");
                return false;
            }

            // pLeftOperandAClass = rAP;

            break;

            //*/
            //----------------------------------------------
        }
    } while (Token_type != END_OF_PROGRAMM && bExit != true);

    if (Token_type == END_OF_PROGRAMM && inout != 0)
        SetError("Unexpected end of programm");
    // UNGUARD
    return false;
}

void COMPILER::CompileNumber(SEGMENT_DESC &Segment)
{
    int32_t lvalue;
    lvalue = ParseIntLiteral(Token.GetData());
    CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
}

void COMPILER::CompileFloatNumber(SEGMENT_DESC &Segment)
{
    float fvalue;
    fvalue = static_cast<float>(atof(Token.GetData()));
    CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&fvalue, sizeof(float));
}

void COMPILER::CompileString(SEGMENT_DESC &Segment)
{
    uint32_t string_size;
    const char *pData = Token.GetData();
    if (pData != nullptr)
    {
        string_size = strlen(Token.GetData()) + 1;
        CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), Token.GetData(), string_size);
    }
    else
    {
        string_size = strlen("") + 1;
        CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), "", string_size);
    }
}

bool COMPILER::CompileUnknown(SEGMENT_DESC &Segment)
{
    uint32_t func_code;
    uint32_t var_code;

    // check for valid function code
    func_code = FuncTab.FindFunc(Token.GetData());

    if (func_code != INVALID_FUNC_CODE)
    {
        // this is function call
        CompileToken(Segment, CALL_FUNCTION, 1, (char *)&func_code, sizeof(uint32_t));
        // Trace("function call: %s ; code = %d",Token.GetData(),func_code);
        return true;
    }

    // check for valid local variable
    var_code = FindLocal(Token.GetData());
    if (var_code != INVALID_VAR_CODE)
    {
        CompileToken(Segment, LOCAL_VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
        // Trace("local variable: %s ; code = %d",Token.GetData(),var_code);
        return true;
    }

    var_code = VarTab.FindVar(Token.GetData());
    if (var_code != INVALID_VAR_CODE)
    {
        // this is gloabl variable
        CompileToken(Segment, VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
        // Trace("variable: %s ; code = %d",Token.GetData(),var_code);
        return true;
    }

    // def_code = DefineTable.GetStringCode(Token.GetData());
    const uint32_t def_code = DefTab.FindDef(Token.GetData());
    if (def_code != INVALID_DEF_CODE)
    {
        DEFINFO di;
        // DefineTable.GetStringData(def_code,&di);
        DefTab.GetDef(di, def_code);
        switch (di.deftype)
        {
        case NUMBER:
            CompileToken(Segment, NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
            break;
        case FLOAT_NUMBER:
            CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
            break;
        case STRING:
            uint32_t string_size;
            string_size = strlen((char *)di.data4b) + 1;
            CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), (char *)di.data4b, string_size);
            break;
        }
        return true;
    }


    SetError("Undeclared identifier: %s", Token.GetData());
    return false;
}

S_TOKEN_TYPE COMPILER::DetectUnknown(uint32_t &code)
{
    // check for valid function code
    const uint32_t func_code = FuncTab.FindFunc(Token.GetData());

    if (func_code != INVALID_FUNC_CODE)
    {
        // this is function call
        code = func_code;
        return CALL_FUNCTION;
    }

    // check for valid local variable
    uint32_t var_code = FindLocal(Token.GetData());
    if (var_code != INVALID_VAR_CODE)
    {
        code = var_code;
        return LOCAL_VARIABLE;
    }

    var_code = VarTab.FindVar(Token.GetData());
    if (var_code != INVALID_VAR_CODE)
    {
        code = var_code;
        return VARIABLE;
    }

    const uint32_t def_code = DefTab.FindDef(Token.GetData());
    if (def_code != INVALID_DEF_CODE)
    {
        code = def_code;
        return DEFINE_VAL;
        /*DEFINFO di;
        DefTab.GetDef(di,def_code);
        switch(di.deftype)
        {
          case NUMBER:
            CompileToken(Segment,NUMBER,1,(char *)&di.data4b,sizeof(uint32_t));
          break;
          case FLOAT_NUMBER:
            CompileToken(Segment,FLOAT_NUMBER,1,(char *)&di.data4b,sizeof(uint32_t));
          break;
          case STRING:
            uint32_t string_size;
            string_size = strlen((char *)di.data4b) + 1;
            CompileToken(Segment,STRING,2,(char *)&string_size,sizeof(uint32_t),(char *)di.data4b,string_size);
          break;
        }
        return true;*/
    }

    return UNKNOWN;
}

void COMPILER::ExitProgram()
{
    const uint32_t function_code = FuncTab.FindFunc("ExitMain");
    if (function_code != INVALID_FUNC_CODE)
    {
        DATA *pResult;
        BC_Execute(function_code, pResult);
    }
    core_internal.Exit();
}

void COMPILER::ClearEvents()
{
    // EventTab.Release();
    EventTab.Clear();
}

void COMPILER::SaveDataDebug(char *data_PTR, ...)
{
    char LogBuffer[MAX_PATH + MAX_PATH];
    if (data_PTR == nullptr)
    {
        return;
    }
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    strcat_s(LogBuffer, "\x0d\x0a");
    va_end(args);
}

void COMPILER::SaveData(const void *data_PTR, uint32_t data_size)
{
    if (data_PTR == nullptr)
        return;

    if (dwCurPointer + data_size > dwMaxSize)
    {
        const uint32_t dwNewAllocate = (1 + (dwCurPointer + data_size) / (1024 * 1024)) * (1024 * 1024);
        // pBuffer = (char*)RESIZE(pBuffer, dwNewAllocate);
        auto *const newPtr = new char[dwNewAllocate];
        memcpy(newPtr, pBuffer, dwMaxSize);
        delete[] pBuffer;
        pBuffer = newPtr;

        dwMaxSize = dwNewAllocate;
    }

    /*    if (data_size < 16)
      {
        char * pWriteBuffer = &pBuffer[dwCurPointer];
        _asm
        {
          mov ecx, data_size
          mov edi, pWriteBuffer
          mov esi, data_PTR

          rep movsb
        }
      }
      else*/
    memcpy(&pBuffer[dwCurPointer], data_PTR, data_size);

    dwCurPointer += data_size;
}

bool COMPILER::ReadData(void *data_PTR, uint32_t data_size)
{
    if (data_PTR == nullptr)
    {
        dwCurPointer += data_size;
        return true;
    }
    if (dwCurPointer + data_size > dwMaxSize)
        return false;

    /*    if (data_size < 16)
      {
        char * pReadBuffer = &pBuffer[dwCurPointer];
        _asm
        {
          mov ecx, data_size
          mov edi, data_PTR
          mov esi, pReadBuffer

          rep movsb
        }
      }
      else*/
    memcpy(data_PTR, &pBuffer[dwCurPointer], data_size);

    dwCurPointer += data_size;

    return true;
}

bool COMPILER::FindReferencedVariable(DATA *pRef, uint32_t &var_index, uint32_t &array_index)
{
    const uint32_t nVarNum = VarTab.GetVarNum();
    for (uint32_t n = 0; n < nVarNum; n++)
    {
        const auto *real_var = VarTab.GetVarX(n);
        if (real_var == nullptr)
        {
            SetError("Invalid var code");
            continue;
        }

        const auto &vi = *real_var;
        if (!vi.value->IsArray())
        {
            if (pRef == vi.value.get())
            {
                var_index = n;
                array_index = 0xffffffff;
                return true;
            }
        }
        else
        {
            for (uint32_t i = 0; i < vi.elements; i++)
            {
                if (pRef == vi.value->GetArrayElement(i))
                {
                    var_index = n;
                    array_index = i;
                    return true;
                }
            }
        }
    }
    return false;
}

bool COMPILER::FindReferencedVariableByRootA(ATTRIBUTES *pA, uint32_t &var_index, uint32_t &array_index)
{
    VarInfo vi;

    const uint32_t nVarNum = VarTab.GetVarNum();
    for (uint32_t n = 0; n < nVarNum; n++)
    {
        const auto *real_var = VarTab.GetVarX(n);
        if (real_var == nullptr)
        {
            SetError("Invalid var code");
            continue;
        }

        const auto &vi = *real_var;
        if (vi.type != VAR_OBJECT)
            continue;
        if (!vi.value->IsArray())
        {
            if (pA == vi.value->AttributesClass)
            {
                var_index = n;
                array_index = 0xffffffff; // ***
                return true;
            }
        }
        else
        {
            for (uint32_t i = 0; i < vi.elements; i++)
            {
                if (pA == vi.value->GetArrayElement(i)->AttributesClass)
                {
                    var_index = n;
                    array_index = i;
                    return true;
                }
            }
        }
    }
    return false;
}

ATTRIBUTES *COMPILER::TraceARoot(ATTRIBUTES *pA, const char *&pAccess)
{
    if (pA == nullptr)
        return nullptr; // error or invalid argument
    if (pA->GetParent() == nullptr)
        return pA; // root found
    if (pA->GetThisNameCode() == 0)
        return nullptr; // fix crash at NewGame start

    const int32_t slen = strlen(pA->GetThisName()) + 1;

    char *pAS = new char[slen];

    memcpy(pAS, pA->GetThisName(), slen);

    if (pAccess == nullptr)
    {
        pAccess = pAS;
    }
    else
    {
        const auto len = slen + strlen(pAccess) + 1;
        // pAS = (char *)RESIZE(pAS, len);
        auto *const newPtr = new char[len];
        memcpy(newPtr, pAS, slen);
        delete[] pAS;
        pAS = newPtr;
        strcat_s(pAS, len, ".");
        strcat_s(pAS, len, pAccess);
        delete[] pAccess;
        pAccess = pAS;
    }

    return TraceARoot(pA->GetParent(), pAccess);
}

void COMPILER::WriteVDword(uint32_t v)
{
    /*    BYTE nbv;
      if(v < 0xff) { nbv = (BYTE)v; SaveData(&nbv, sizeof(nbv)); }
      else
      {
        nbv = 0xff; SaveData(&nbv, sizeof(nbv));
        SaveData(&v, sizeof(v));
      }
    */
    uint8_t nbv;
    uint16_t nwv;
    if (v < 0xfe)
    {
        nbv = static_cast<uint8_t>(v);
        SaveData(&nbv, sizeof(nbv));
    }
    else if (v < 0xffff)
    {
        nbv = 0xfe;
        SaveData(&nbv, sizeof(nbv));
        nwv = static_cast<uint16_t>(v);
        SaveData(&nwv, sizeof(nwv));
    }
    else
    {
        nbv = 0xff;
        SaveData(&nbv, sizeof(nbv));
        SaveData(&v, sizeof(v));
    }
}

uint32_t COMPILER::ReadVDword()
{
    uint8_t nbv;
    uint16_t nwv;
    uint32_t v;
    ReadData(&nbv, 1);
    if (nbv < 0xfe)
    {
        return nbv;
    }
    if (nbv == 0xfe)
    {
        ReadData(&nwv, sizeof(nwv));
        return nwv;
    }
    ReadData(&v, sizeof(uint32_t));
    return v;
}

void COMPILER::SaveString(const char *pS)
{
    if (pS == nullptr)
    {
        WriteVDword(0);
        return;
    }
    const uint32_t n = strlen(pS) + 1;
    WriteVDword(n);
    SaveData(pS, n);
}

char *COMPILER::ReadString()
{
    const uint32_t n = ReadVDword();
    if (n == 0)
        return nullptr;

    char *pBuffer = new char[n];
    ReadData(pBuffer, n);
    if (!utf8::IsValidUtf8(pBuffer))
    {
        spdlog::warn("Deserializing invalid utf8 string: {}", pBuffer);
    }
    return pBuffer;
}

bool COMPILER::ReadVariable(char *name, /* DWORD code,*/ bool bDim, uint32_t a_index)
{
    int32_t nLongValue;
    uintptr_t ptrValue;
    float fFloatValue;
    char *pString;
    uint32_t var_index;
    uint32_t array_index;
    uint32_t nElementsNum;
    ATTRIBUTES *pA;
    S_TOKEN_TYPE eType;
    const VarInfo *real_var;
    const VarInfo *real_var_ref;
    DATA *pV;
    DATA *pVRef;
    entid_t eid;
    uint32_t var_code;
    bool bSkipVariable;

    bSkipVariable = false;
    var_code = VarTab.FindVar(name);
    if (var_code == INVALID_VAR_CODE)
    {
        SetError("Load warning - variable: '%s' not found", name);
        bSkipVariable = true;
    }
    else
    {
        real_var = VarTab.GetVarX(var_code);
        if (real_var == nullptr)
        {
            SetError("Load warning - variable: '%s' has invalid var code", name);
            bSkipVariable = true;
        }
        else
        {
            pV = real_var->value.get();
        }
    }

    if (!bDim) // skip this info for array elements
    {
        // trace("Read[%d]: %s",code,vi.name);
        ReadData(&eType, sizeof(eType));
        Assert(eType < S_TOKEN_TYPE::TOKEN_TYPES_COUNT);
        if (!bSkipVariable)
            if (real_var->type != eType)
            {
                SetError("load type mismatch");
                return false;
            }
        ReadData(&nElementsNum, sizeof(nElementsNum));
        if (!bSkipVariable)
            if (real_var->elements != nElementsNum)
            {
                // ???
                // SetError("load size mismatch");
                // return false;
                real_var->value->SetElementsNum(nElementsNum);
                if (!VarTab.SetElementsNum(var_code, nElementsNum))
                {
                    core_internal.Trace("Unable to set elements num for %s", real_var->name.c_str());
                }
            }
    }
    else
    {
        if (bSkipVariable)
        {
            SetError("Load - lost element");
            return false;
        }
        nElementsNum = 1;
        eType = pV->GetType();
        pV = pV->GetArrayElement(a_index);
    }

    if (nElementsNum > 1) // array
    {
        // load array elements
        for (uint32_t n = 0; n < nElementsNum; n++)
        {
            if (bSkipVariable)
            {
                if (eType == S_TOKEN_TYPE::VAR_INTEGER)
                    ReadData(nullptr, sizeof(int32_t));
                else if (eType == S_TOKEN_TYPE::VAR_FLOAT)
                    ReadData(nullptr, sizeof(float));
                else if (eType == S_TOKEN_TYPE::VAR_STRING)
                    ReadString();
                else if (eType == S_TOKEN_TYPE::VAR_OBJECT)
                {
                    ReadData(nullptr, sizeof(uint64_t));
                    ATTRIBUTES TA(&SCodec);
                    ReadAttributesData(&TA, nullptr);
                }
                else
                    Assert(false);
                continue;
            }

            if (!ReadVariable(name, /*code,*/ true, n))
                return false;
        }
        return true;
    }

    switch (eType)
    {
    case VAR_INTEGER:
        ReadData(&nLongValue, sizeof(nLongValue));
        if (bSkipVariable)
            break;
        pV->Set(nLongValue);
        break;
    case VAR_PTR:
        ReadData(&ptrValue, sizeof(ptrValue));
        if (bSkipVariable)
            break;
        pV->SetPtr(ptrValue);
        break;
    case VAR_FLOAT:
        ReadData(&fFloatValue, sizeof(fFloatValue));
        if (bSkipVariable)
            break;
        pV->Set(fFloatValue);
        break;
    case VAR_STRING:
        pString = ReadString();
        if (pString)
        {
            if (!bSkipVariable)
                pV->Set(pString);
            delete[] pString;
        }
        break;
    case VAR_OBJECT:
        ReadData(&eid, sizeof(eid));
        if (!bSkipVariable)
        {
            // Saves contain script attributes, not the runtime entities they
            // once referenced. OnLoad recreates those entities explicitly.
            // Keep consuming the legacy handle for file compatibility, but
            // never bind it to an unrelated entity in this process.
            pV->Set(invalid_entity);

            if (pV->AttributesClass == nullptr)
                pV->AttributesClass = new ATTRIBUTES(&SCodec);
            ReadAttributesData(pV->AttributesClass, nullptr);
        }
        else
        {
            ATTRIBUTES *pTA = new ATTRIBUTES(&SCodec);
            ReadAttributesData(pTA, nullptr);
            delete pTA;
        }
        break;
    case VAR_REFERENCE:
        var_index = ReadVDword();
        if (var_index == 0xffffffff)
            break; // uninitialized ref
        array_index = ReadVDword();
        if (bSkipVariable)
            break;
        real_var_ref = VarTab.GetVarX(var_index);
        if (real_var_ref == nullptr)
        {
            SetError("State read error");
            return false;
        }

        pVRef = real_var_ref->value.get();
        if (array_index != 0xffffffff)
        {
            pVRef = pVRef->GetArrayElement(array_index);
        }
        pV->SetReference(pVRef);
        break;
    case VAR_AREFERENCE:
        pA = nullptr;
        var_index = ReadVDword();
        if (var_index == 0xffffffff)
            break;
        array_index = ReadVDword();
        pString = ReadString();
        if (bSkipVariable)
        {
            delete[] pString;
            break;
        }

        real_var_ref = VarTab.GetVarX(var_index);
        if (real_var_ref == nullptr)
        {
            delete[] pString;
            SetError("State read error");
            return false;
        }

        pVRef = real_var_ref->value.get();
        if (array_index != 0xffffffff)
        {
            pVRef = pVRef->GetArrayElement(array_index);
        }

        if (pVRef->AttributesClass == nullptr)
            pVRef->AttributesClass = new ATTRIBUTES(&SCodec);
        if (pString)
        {
            pA = pVRef->AttributesClass->CreateSubAClass(pVRef->AttributesClass, pString);
            delete[] pString;
        }
        pV->SetAReference(pA);

        break;
    default:
        SetError("Unknown token type: %i", eType);
    }

    return true;
}

void COMPILER::SaveVariable(DATA *pV, bool bdim)
{
    int32_t nLongValue;
    uintptr_t ptrValue;
    float fFloatValue;
    const char *pString;
    uint32_t var_index;
    uint32_t array_index;
    uint32_t n;
    ATTRIBUTES *pA;
    S_TOKEN_TYPE eType;
    entid_t eid;

    if (pV == nullptr)
    {
        SetError("Zero Variable Pointer");
        return;
    }
    if (!bdim) // skip this info for array elements
    {
        eType = pV->GetType();
        SaveData(&eType, sizeof(eType));

        // n = pV->GetElementsNum();
        n = pV->Number_of_elements;
        if (n == 0)
            n = 1; // for reference variables
        SaveData(&n, sizeof(n));
    }

    if (pV->IsArray())
    {
        for (n = 0; n < pV->GetElementsNum(); n++)
        {
            // sprintf_s(gs,"    [index : %d]\n",n);
            // OutputDebugString(gs);
            SaveVariable(pV->GetArrayElement(n), true);
        }
        return;
    }

    switch (pV->GetType())
    {
    case VAR_INTEGER:
        pV->Get(nLongValue);
        SaveData(&nLongValue, sizeof(nLongValue));
        break;
    case VAR_PTR:
        pV->GetPtr(ptrValue);
        SaveData(&ptrValue, sizeof(ptrValue));
        break;
    case VAR_FLOAT:
        pV->Get(fFloatValue);
        SaveData(&fFloatValue, sizeof(fFloatValue));
        break;
    case VAR_STRING:
        pV->Get(pString);
        SaveString(pString);
        /*pV->Get(pString);
        if(pString == 0) nlen = 0;
        else nlen = strlen(pString) + 1;
        //WriteVDword(nlen);
        SaveData(&nlen, sizeof(nlen));
        if(nlen) SaveData(pString, nlen);*/
        break;
    case VAR_OBJECT:
        pV->Get(eid);
        SaveData(&eid, sizeof(eid));
        SaveAttributesData(pV->AttributesClass);
        break;
    case VAR_REFERENCE:

        if (pV->GetReference() == nullptr)
        {
            // uninitialized reference
            WriteVDword(0xffffffff);
            return;
        }
        if (!FindReferencedVariable(pV->GetVarPointer(), var_index, array_index))
        {
            SetError("Ghost reference");
            // reference to none
            WriteVDword(0xffffffff);
            return;
        }

        WriteVDword(var_index);
        WriteVDword(array_index);

        break;
    case VAR_AREFERENCE:
        pString = nullptr;
#ifdef _WIN32
        __try
#else
        try
#endif
        {
            pA = TraceARoot(pV->AttributesClass, pString);
        }
#ifdef _WIN32 // TODO: check and replace __except
        __except (EXCEPTION_EXECUTE_HANDLER)
#else
        catch (...)
#endif
        {
            SetError("Save - ARef to non existing attributes branch");
            WriteVDword(0xffffffff);
            break;
        }
        if (pA == nullptr)
        {
            WriteVDword(0xffffffff);
            delete[] pString;
            break;
        }
        if (!FindReferencedVariableByRootA(pA, var_index, array_index))
        {
            SetError("Ghost A reference");
            WriteVDword(0xffffffff);
            delete[] pString;
            break;
        }
        WriteVDword(var_index);
        WriteVDword(array_index);
        SaveString(pString);
        delete[] pString;
        break;
    }
}

bool COMPILER::OnLoad()
{
    const uint32_t function_code = FuncTab.FindFunc("OnLoad");
    DATA *pResult;
    const bool executed = BC_Execute(function_code, pResult);
#ifdef __ANDROID__
    if (executed) LoadAndroidUiAdapter();
    else storm::android::traceLandAdapter(0, "onload-execution-failed");
#else
    (void)executed;
#endif
    return true;
}

bool COMPILER::SaveState(std::vector<char> &image)
{
    uint32_t n;
    delete[] pBuffer;
    pBuffer = nullptr;

    dwCurPointer = 0;
    dwMaxSize = 0;

    DATA *pResult;
    const uint32_t function_code = FuncTab.FindFunc("OnSave");
    if (function_code != INVALID_FUNC_CODE)
        BC_Execute(function_code, pResult);

    // Serialize into memory only: the caller replaces the file atomically, so a
    // failure or crash at any point leaves the previous save intact.
    std::string save_info = "save";
    auto *pVDat = static_cast<VDATA *>(core_internal.GetScriptVariable("savefile_info"));
    if (pVDat && pVDat->GetString())
        save_info = pVDat->GetString();

    // 1. Program Directory
    SaveString(ProgramDirectory);

    // 4. SCodec data
    WriteVDword(SCodec.GetNum());
    for (n = 0; n < SCodec.GetNum(); n++)
    {
        if (n == 0)
            SaveString(SCodec.Get());
        else
            SaveString(SCodec.GetNext());
    }

    const uint32_t nSegNum = 1; // SegmentsNum;
    // 2. Data Segments
    WriteVDword(nSegNum);

    // 3. Segments names
    for (n = 0; n < nSegNum; n++)
    {
        if (!SegmentTable[n].name.empty())
        {
            SaveString(SegmentTable[n].name.c_str());
        }
    }

    // 5. Variables table
    const uint32_t nVarNum = VarTab.GetVarNum();
    WriteVDword(nVarNum);

    const VarInfo *last_var{nullptr};
    for (n = 0; n < nVarNum; n++)
    {
        const VarInfo *real_var = VarTab.GetVar(n);
        if (real_var == nullptr)
        {
            real_var = last_var; // preserve old semanthics
        }
        else
        {
            last_var = real_var;
        }

        SaveString(real_var->name.c_str()); // ***
        SaveVariable(real_var->value.get());
    }

    std::string error;
    const bool built = storm::save::buildState(save_info.c_str(), pBuffer, dwCurPointer, image, error);
    if (!built)
    {
        spdlog::error("Cannot build save image: {}", error);
    }

    delete[] pBuffer;
    pBuffer = nullptr;
    dwCurPointer = 0;
    dwMaxSize = 0;

    return built;
}

// `raw` is the already validated, uncompressed script state (see save_file.hpp).
// Validation happens before the caller erases the running game, so a corrupt
// file can no longer destroy the current session.
bool COMPILER::LoadState(const std::vector<char> &raw)
{
    uint32_t n;
    char *pString;

    delete[] pBuffer;
    pBuffer = nullptr;
    if (raw.empty())
    {
        return false;
    }

    pBuffer = new char[raw.size()];
    memcpy(pBuffer, raw.data(), raw.size());
    dwMaxSize = static_cast<uint32_t>(raw.size());
    dwCurPointer = 0;

    // Release all data
    Release();

    // 1. Program Directory
    ProgramDirectory = ReadString();

    // 4. SCodec data
    const uint32_t nSCStringsNum = ReadVDword();
    for (n = 0; n < nSCStringsNum; n++)
    {
        pString = ReadString();
        if (pString)
        {
            SCodec.Convert(pString);
            delete[] pString;
        }
    }

    const uint32_t nSegments2Load = ReadVDword();

    // 3.  Segments names
    // 3.a Initialize internal functions
    // 3.b Load preprocess
    InitInternalFunctions();
    LoadPreprocess();

    for (n = 0; n < nSegments2Load; n++)
    {
        char *pSegmentName = ReadString();
        if (!BC_LoadSegment(pSegmentName))
            return false;
        delete[] pSegmentName;
    }

    // 5. Variables table, all variables created during previous step, just read value
    const uint32_t nVarNum = ReadVDword();
    for (n = 0; n < nVarNum; n++)
    {
        pString = ReadString();
        if (pString == nullptr || strcmp(pString, "") == 0)
        {
            SetError("missing variable name");
            return false;
        }
        ReadVariable(pString /*,n*/);

        delete[] pString;
    }

    // call to script function "OnLoad()"
    OnLoad();

    delete[] pBuffer;
    pBuffer = nullptr;

    return true;
}

void COMPILER::ReadAttributesData(ATTRIBUTES *pRoot, ATTRIBUTES *pParent)
{
    uint32_t nSubClassesNum;
    uint32_t n;
    uint32_t nNameCode;
    // char * pName;
    char *pValue;

    if (pRoot == nullptr)
    {
        nSubClassesNum = ReadVDword();
        nNameCode = ReadVDword();

        // DTrace(SCodec.Convert(nNameCode));
        pValue = ReadString();
        pParent->SetAttribute(nNameCode, pValue);
        pRoot = pParent->GetAttributeClassByCode(nNameCode);
        delete[] pValue;
        for (n = 0; n < nSubClassesNum; n++)
        {
            ReadAttributesData(nullptr, pRoot);
        }

        return;
    }

    nSubClassesNum = ReadVDword();
    nNameCode = ReadVDword();
    pValue = ReadString();
    // pRoot->SetAttribute(nNameCode,pValue);

    pRoot->SetNameCode(nNameCode);
    pRoot->SetValue(pValue);

    for (n = 0; n < nSubClassesNum; n++)
    {
        // ReadAttributesData(pRoot->GetAttributeClass(n));
        ReadAttributesData(nullptr, pRoot);
    }

    // if(pName) delete pName;
    delete[] pValue;
}

void COMPILER::SaveAttributesData(ATTRIBUTES *pRoot)
{
    if (pRoot == nullptr)
    {
        WriteVDword(0);      // number of subclasses
        SaveString(nullptr); // attribute name
        SaveString(nullptr); // attribute value
        return;
    }
    WriteVDword(pRoot->GetAttributesNum()); // number of subclasses

    // save attribute name
    // SaveString(pRoot->GetThisName());
    WriteVDword(pRoot->GetThisNameCode());

    // save attribute value
    SaveString(pRoot->GetThisAttr());
    for (uint32_t n = 0; n < pRoot->GetAttributesNum(); n++)
    {
        SaveAttributesData(pRoot->GetAttributeClass(n));
    }
}

void COMPILER::AddPostEvent(S_EVENTMSG *pEM)
{
    EventMsg.Add(pEM);
}

bool COMPILER::SetSaveData(const char *file_name, void *save_data, int32_t data_size)
{
    if (file_name == nullptr || save_data == nullptr || data_size <= 0)
    {
        return false;
    }

    // Read-modify-write through a temporary file: the save written by
    // SaveState stays loadable if this step fails or the process dies.
    const auto path = std::filesystem::u8path(fio->ConvertPathResource(file_name));
    std::string error;
    storm::save::Bytes image;
    if (!storm::save::readFile(path, image, error))
    {
        spdlog::error("Cannot attach save data to '{}': {}", path.string(), error);
        return false;
    }

    std::string save_info = "save";
    auto *pVDat = static_cast<VDATA *>(core_internal.GetScriptVariable("savefile_info"));
    if (pVDat && pVDat->GetString())
        save_info = pVDat->GetString();

    const auto validate = [](const storm::save::Bytes &stored, std::string &why) {
        storm::save::Bytes scratch;
        return storm::save::readState(stored, scratch, why) && storm::save::readExtData(stored, scratch, why);
    };
    if (!storm::save::appendExtData(image, save_info.c_str(), save_data, static_cast<uint32_t>(data_size), error) ||
        !storm::save::writeAtomic(path, image, validate, error))
    {
        spdlog::error("Cannot attach save data to '{}': {}", path.string(), error);
        return false;
    }

    return true;
}

void *COMPILER::GetSaveData(const char *file_name, int32_t &data_size)
{
    data_size = 0;
    if (file_name == nullptr)
    {
        return nullptr;
    }

    const auto path = std::filesystem::u8path(fio->ConvertPathResource(file_name));
    std::string error;
    storm::save::Bytes data;
    if (!std::filesystem::exists(path))
    {
        SetError("cant open save file: %s", file_name);
        return nullptr;
    }
    // Corrupt or truncated extended data is reported as "no data" instead of
    // handing the caller a buffer of uninitialized bytes.
    if (!storm::save::readExtDataFromFile(path, data, error))
    {
        spdlog::warn("Ignoring damaged save description in '{}': {}", path.string(), error);
        return nullptr;
    }
    if (data.empty())
    {
        return nullptr;
    }

    char *pResult = new char[data.size()];
    memcpy(pResult, data.data(), data.size());
    data_size = static_cast<int32_t>(data.size());
    return pResult;
}

void COMPILER::AddRuntimeEvent()
{
    if (!bRuntimeLog)
        return;
    nRuntimeLogEventsNum++;
    if (nRuntimeLogEventsNum >= nRuntimeLogEventsBufferSize)
    {
        if (nRuntimeLogEventsBufferSize == 0)
            nRuntimeLogEventsBufferSize = 180;
        else
            nRuntimeLogEventsBufferSize *= 2;
        // pRuntimeLogEvent = (uint32_t *)RESIZE(pRuntimeLogEvent,nRuntimeLogEventsBufferSize*sizeof(uint32_t));
        pRuntimeLogEvent.resize(nRuntimeLogEventsBufferSize);
    }
    pRuntimeLogEvent[nRuntimeLogEventsNum - 1] = nRuntimeTicks;
    nRuntimeTicks = 0;
}

uint32_t COMPILER::SetScriptFunction(IFUNCINFO *pFuncInfo)
{
    FuncInfo fi;

    if (pFuncInfo->pFuncName == nullptr)
    {
        SetError("Invalid function name in SetScriptFunction(...)");
        return INVALID_FUNCHANDLE;
    }

    const uint32_t nFuncHandle = FuncTab.FindFunc(pFuncInfo->pFuncName);
    if (nFuncHandle != INVALID_FUNC_CODE)
    {
        FuncTab.GetFuncX(fi, nFuncHandle);
        if (fi.arguments != pFuncInfo->nArguments)
        {
            SetError("arguments num mismatch in function '%s'", pFuncInfo->pFuncName);
            return INVALID_FUNCHANDLE;
        }
    }
    else
    {
        // not declared (yet): bound when a later declaration appears (ScanParameterList's caller);
        // scripts that never declare it (another game's) cannot call it, so it is not an error
        pendingNatives_[LowerName(pFuncInfo->pFuncName)] = *pFuncInfo;
        if (undeclaredNatives_)
            undeclaredNatives_->emplace_back(pFuncInfo->pFuncName);
        else
            spdlog::info("Script native '{}' is registered but not declared by the scripts", pFuncInfo->pFuncName);
        return INVALID_FUNCHANDLE;
    }

    fi.name = pFuncInfo->pFuncName;
    fi.imported_func = pFuncInfo->pFuncAddress;
    fi.arguments = pFuncInfo->nArguments;
    fi.segment_id = IMPORTED_SEGMENT_ID;
    fi.offset = INVALID_FUNC_OFFSET;
    fi.stack_offset = 0xffffffff;

    if (pFuncInfo->pReturnValueName == nullptr)
        fi.return_type = TVOID;
    else
    {
        const S_TOKEN_TYPE TokenType = Token.Keyword2TokenType(pFuncInfo->pReturnValueName);
        switch (TokenType)
        {
        case TVOID:
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
            fi.return_type = TokenType;
            break;
        default:
            SetError("Invalid import function '%s' return type '%s'", pFuncInfo->pFuncName,
                     pFuncInfo->pReturnValueName);
            return INVALID_FUNCHANDLE;
        }
    }

    const uint32_t funch = FuncTab.AddFunc(fi);
    return funch;
}

DATA *COMPILER::GetOperand(const char *pCodeBase, uint32_t &ip, S_TOKEN_TYPE *pTokenType)
{
    uint32_t token_data_size;
    const VarInfo *real_var;
    DATA *pVar;

    const S_TOKEN_TYPE sttResult = BC_TokenGet(ip, token_data_size);
    if (pTokenType)
        *pTokenType = sttResult;
    switch (sttResult)
    {
    case STACK_TOP:
        pVar = SStack.Read();
        return pVar;
    case VARIABLE:
        real_var = VarTab.GetVar(*reinterpret_cast<const uint32_t *>(&pCodeBase[ip]));
        if (real_var == nullptr)
        {
            SetError("Global variable not found");
            break;
        }
        if (!real_var->value.get())
        {
            SetError("invalid global variable");
            break;
        }
        return real_var->value.get();
    case LOCAL_VARIABLE:
        pVar = SStack.Read(pRun_fi->stack_offset, *reinterpret_cast<const uint32_t *>(&pCodeBase[ip]));
        if (pVar == nullptr)
        {
            SetError("Local variable not found");
            return nullptr;
        }
        return pVar;
    case AX:
        return &rAX;
    case BX:
        return &rBX;
    case EX:
        return &ExpressionResult;
    case AP:
        return nullptr;
    default:
        SetError("invalid operand");
        return nullptr;
    }
    return nullptr;
}

bool COMPILER::ScriptCallInTime()
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                              scriptCallStart_).count();
    if (elapsed <= scriptCallTimeLimitMs_)
        return true;
    SetError("script call stopped after %lld ms (limit %u ms, engine.ini [script] call_time_limit): an endless loop?",
             static_cast<long long>(elapsed), scriptCallTimeLimitMs_);
    CollectCallStack();
    scriptCallAborted_ = true;
    return false;
}

void COMPILER::CollectCallStack() const
{
    logStack_->trace("Call stack:");
    auto callStackCopy = callStack_;
    while (!callStackCopy.empty())
    {
        const auto &record = callStackCopy.back();

        const auto &filename = std::get<0>(record);
        const auto &line = std::get<1>(record);
        const auto &name = std::get<2>(record);

        if (filename[0] == '\0')
        {
            logStack_->trace("{} (EVENT)", name);
        }
        else if (strcmp(filename, "engine") == 0)
        {
            logStack_->trace("{} (INTERNAL FUNCTION)", name);
        }
        else
        {
            logStack_->trace("{} (FUNCTION) at {}:{}", name, filename, line);
        }

        callStackCopy.pop();
    }
}

void COMPILER::PrintoutUsage()
{
    if (bRuntimeLog && core.Controls->GetDebugAsyncKeyState(VK_BACK) < 0 &&
        core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0)
    {
        logTrace_->debug("Script Function Time Usage[func name/code(release mode) : ticks]");
        FuncInfo fi;
        for (size_t n = 0; n < FuncTab.GetFuncNum(); n++)
        {
            FuncTab.GetFuncX(fi, n);
            if (fi.number_of_calls == 0)
            {
                continue;
            }

            FuncTab.GetFunc(fi, n);
            if (!fi.name.empty())
            {
                logTrace_->debug("  {}", fi.name);
            }
            else
            {
                logTrace_->debug("  {}", n);
            }
            logTrace_->debug("  ticks summary  : {}", fi.usage_time);
            logTrace_->debug("  calls          : {}", fi.number_of_calls);
            if (fi.number_of_calls != 0)
            {
                logTrace_->debug("  average ticks  : {}", static_cast<float>(fi.usage_time) / fi.number_of_calls);
            }
            logTrace_->debug("");
        }
        FuncTab.ResetTimeAndCalls();

        logTrace_->debug("Script Run Time Log [sec : ms]");
        for (size_t n = 0; n < nRuntimeLogEventsNum; n++)
        {
            logTrace_->debug("  {} : {}", n, pRuntimeLogEvent[n]);
        }
    }
}

void COMPILER::LoadVariablesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto vi = VarInfo();

        vi.segment_id = segment.id;
        vi.name = reader.ReadArray();
        vi.type = reader.Read<decltype(vi.type)>();
        vi.elements = reader.Read<decltype(vi.elements)>();
        vi.array = reader.Read<bool>();

        const auto var_code = VarTab.AddVar(vi);
        if (var_code == INVALID_VAR_CODE)
        {
            // Name taken by a segment loaded since the entry was written; the
            // compiler reports it as a duplicate with file and line.
            throw storm::script_cache::ReaderException();
        }
        const auto real_var = VarTab.GetVarX(var_code);

        if (!vi.IsArray())
        {
            ReadScriptData(reader, vi.type, real_var->value.get());
        }
        else
        {
            for (size_t j = 0; j < vi.elements; ++j)
            {
                ReadScriptData(reader, vi.type, real_var->value->GetArrayElement(j));
            }
        }
    }
}

void COMPILER::LoadFunctionsFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto fi = FuncInfo();
        fi.segment_id = segment.id;

        fi.name = reader.ReadArray();
        fi.offset = reader.Read<decltype(fi.offset)>();
        fi.return_type = reader.Read<decltype(fi.return_type)>();
        fi.decl_file_name = reader.ReadArray();
        fi.decl_line = reader.Read<decltype(fi.decl_line)>();
        fi.native = reader.Read<bool>();
        fi.varargs = reader.Read<bool>();
        fi.default_args.resize(reader.Read<size_t>());
        for (auto &value : fi.default_args)
        {
            value.type = reader.Read<S_TOKEN_TYPE>();
            value.int_value = reader.Read<int32_t>();
            value.float_value = reader.Read<float>();
            value.string_value = reader.ReadArray();
        }

        auto func_code = FuncTab.AddFunc(fi);

        // args
        auto count = reader.Read<size_t>();
        for (size_t j = 0; j < count; ++j)
        {
            auto lvi = LocalVarInfo();

            lvi.name = reader.ReadArray();
            lvi.type = reader.Read<decltype(lvi.type)>();
            lvi.elements = reader.Read<decltype(lvi.elements)>();
            lvi.array = reader.Read<bool>();

            auto is_extern = reader.Read<bool>();

            FuncTab.AddFuncArg(func_code, lvi, is_extern);
        }

        // local variables
        count = reader.Read<size_t>();
        for (size_t j = 0; j < count; ++j)
        {
            auto lvi = LocalVarInfo();

            lvi.name = reader.ReadArray();
            lvi.type = reader.Read<decltype(lvi.type)>();
            lvi.elements = reader.Read<decltype(lvi.elements)>();
            lvi.array = reader.Read<bool>();

            FuncTab.AddFuncVar(func_code, lvi);
        }
    }
}

void COMPILER::LoadScriptLibrariesFromCache(storm::script_cache::BufferReader &reader)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto name = std::string(reader.ReadArray());

        auto cls = core_internal.FindVMA(name.c_str());
        if (!cls)
        {
            SetWarning("cant load library '%s'", name.c_str());
            continue;
        }

        auto lib = static_cast<SCRIPT_LIBRIARY *>(cls->CreateClass());
        if (lib)
        {
            lib->Init();
        }

        LibriaryFuncs.emplace_back(lib, name.c_str());
    }
}

void COMPILER::LoadEventHandlersFromCache(storm::script_cache::BufferReader &reader)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto event = std::string(reader.ReadArray());
        auto name = std::string(reader.ReadArray());

        SetEventHandler(event.c_str(), name.c_str(), 0, true);
    }
}

void COMPILER::LoadByteCodeFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto code = reader.ReadArray();
    segment.BCode_Program_size = segment.BCode_Buffer_size = code.size();
    segment.pCode = new char[segment.BCode_Buffer_size];
    std::ranges::copy(code, segment.pCode);

    // relocations data
    auto strings = std::unordered_map<uint32_t, std::string>();
    auto variables = std::unordered_map<uint32_t, std::string>();
    auto functions = std::unordered_map<uint32_t, std::string>();

    auto size = reader.Read<size_t>();
    strings.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto string_code = reader.Read<uint32_t>();
        auto string = reader.ReadArray();
        strings.emplace(string_code, string);
    }

    size = reader.Read<size_t>();
    variables.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto variable_code = reader.Read<uint32_t>();
        auto name = reader.ReadArray();
        variables.emplace(variable_code, name);
    }

    size = reader.Read<size_t>();
    functions.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto function_code = reader.Read<uint32_t>();
        auto name = reader.ReadArray();
        functions.emplace(function_code, name);
    }

    // apply relocations
    pRunCodeBase = segment.pCode;
    InstructionPointer = 0;
    S_TOKEN_TYPE token_type = END_OF_PROGRAMM;
    do
    {
        token_type = BC_TokenGet();
        auto &relocated_data =
            *const_cast<uint32_t *>(reinterpret_cast<const uint32_t *>(&pRunCodeBase[TLR_DataOffset]));
        if (token_type == ACCESS_WORD_CODE)
        {
            auto string_code = relocated_data;
            relocated_data = SCodec.Convert(strings[string_code].c_str());
        }
        else if (token_type == VARIABLE)
        {
            const auto real_var_code = VarTab.FindVar(variables[relocated_data]);
            relocated_data = real_var_code;
        }
        else if (token_type == CALL_FUNCTION)
        {
            const auto real_function_code = FuncTab.FindFunc(functions[relocated_data]);
            relocated_data = real_function_code;
        }
    } while (token_type != END_OF_PROGRAMM);
}

std::filesystem::path COMPILER::GetSegmentCachePath(const SEGMENT_DESC &segment) const
{
    auto path = CacheFolder() / segment.name;
    path.replace_extension(".b");
    return path;
}

void COMPILER::SaveVariablesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.variables.size());
    for (auto &vi : script_cache_.variables)
    {
        writer.WriteArray(vi.name);
        writer.WriteData(vi.type);
        writer.WriteData(vi.elements);
        writer.WriteData(vi.array);

        if (!vi.IsArray())
        {
            WriteScriptData(writer, vi.type, vi.value.get());
        }
        else
        {
            for (size_t i = 0; i < vi.elements; ++i)
            {
                WriteScriptData(writer, vi.type, vi.value->GetArrayElement(i));
            }
        }
    }
}

void COMPILER::SaveFunctionsToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.functions.size());
    for (auto &[fi, arguments, local_variables] : script_cache_.functions)
    {
        writer.WriteArray(fi.name);
        writer.WriteData(fi.offset);
        writer.WriteData(fi.return_type);
        writer.WriteArray(fi.decl_file_name);
        writer.WriteData(fi.decl_line);
        writer.WriteData(fi.native);
        writer.WriteData(fi.varargs);
        writer.WriteData(fi.default_args.size());
        for (const auto &value : fi.default_args)
        {
            writer.WriteData(value.type);
            writer.WriteData(value.int_value);
            writer.WriteData(value.float_value);
            writer.WriteArray(value.string_value);
        }

        writer.WriteData(arguments.size());
        for (auto &[lvi, is_extern] : arguments)
        {
            writer.WriteArray(lvi.name);
            writer.WriteData(lvi.type);
            writer.WriteData(lvi.elements);
            writer.WriteData(lvi.array);
            writer.WriteData(is_extern);
        }

        writer.WriteData(local_variables.size());
        for (auto &lvi : local_variables)
        {
            writer.WriteArray(lvi.name);
            writer.WriteData(lvi.type);
            writer.WriteData(lvi.elements);
            writer.WriteData(lvi.array);
        }
    }
}

void COMPILER::SaveScriptLibrariesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.script_libs.size());
    for (auto &lib_name : script_cache_.script_libs)
    {
        writer.WriteArray(lib_name);
    }
}

void COMPILER::SaveEventHandlersToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.event_handlers.size());
    for (auto &[event, handler] : script_cache_.event_handlers)
    {
        writer.WriteArray(event);
        writer.WriteArray(handler);
    }
}

void COMPILER::SaveByteCodeToCache(storm::script_cache::BufferWriter &writer, const SEGMENT_DESC &segment)
{
    auto code = std::string_view(segment.pCode, segment.pCode + segment.BCode_Program_size);
    writer.WriteArray(code);

    // relocations
    auto strings = std::unordered_map<uint32_t, std::string>();
    auto variables = std::unordered_map<uint32_t, std::string>();
    auto functions = std::unordered_map<uint32_t, std::string>();

    pRunCodeBase = segment.pCode;
    InstructionPointer = 0;
    auto token_type = S_TOKEN_TYPE();
    do
    {
        token_type = BC_TokenGet();
        auto &relocated_data = *reinterpret_cast<const uint32_t *>(&pRunCodeBase[TLR_DataOffset]);

        if (token_type == ACCESS_WORD_CODE)
        {
            strings.emplace(relocated_data, SCodec.Convert(relocated_data));
        }
        else if (token_type == VARIABLE)
        {
            auto vi = VarTab.GetVarX(relocated_data);
            variables.emplace(relocated_data, vi->name);
        }
        else if (token_type == CALL_FUNCTION)
        {
            auto fi = FuncInfo();
            FuncTab.GetFuncX(fi, relocated_data);
            functions.emplace(relocated_data, fi.name);
        }
    } while (token_type != END_OF_PROGRAMM);

    writer.WriteData(strings.size());
    for (auto &[string_code, string] : strings)
    {
        writer.WriteData(string_code);
        writer.WriteArray(string);
    }

    writer.WriteData(variables.size());
    for (auto &[variable_code, name] : variables)
    {
        writer.WriteData(variable_code);
        writer.WriteArray(name);
    }

    writer.WriteData(functions.size());
    for (auto &[function_code, name] : functions)
    {
        writer.WriteData(function_code);
        writer.WriteArray(name);
    }
}

void COMPILER::SaveSegmentToCache(const SEGMENT_DESC &segment)
{
    storm::script_cache::BufferWriter writer;

    // defines (in case of new compiled code depending on defines from cache)
    SaveDefinesToCache(writer);

    // variables with values
    SaveVariablesToCache(writer);

    // functions
    SaveFunctionsToCache(writer);

    // script libs
    SaveScriptLibrariesToCache(writer);

    // event handlers
    SaveEventHandlersToCache(writer);

    // bytecode
    SaveByteCodeToCache(writer, segment);

    const auto data = writer.GetDataBuffer();
    if (const auto error = storm::script_cache::writeEntry(GetSegmentCachePath(segment),
                                                           std::string_view(std::data(data), std::size(data)));
        !error.empty())
    {
        spdlog::warn("Script cache: {}; the segment is compiled again next start", error);
    }
}

void COMPILER::SaveDefinesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.defines.size());
    for (auto &[name, type, value] : script_cache_.defines)
    {
        writer.WriteArray(name);
        writer.WriteData(type);
        switch (type)
        {
        case NUMBER:
            writer.WriteData(static_cast<int32_t>(value));
            break;

        case FLOAT_NUMBER:
            writer.WriteData(static_cast<float>(value));
            break;

        case STRING:
            writer.WriteArray(reinterpret_cast<const char *>(value));
            break;
        }
    }
}

bool COMPILER::LoadSegmentFromCache(SEGMENT_DESC &segment)
{
    const auto path = GetSegmentCachePath(segment);
    std::vector<char> data;
    switch (storm::script_cache::readEntry(path, data))
    {
    case storm::script_cache::Entry::Missing:
        return false;
    case storm::script_cache::Entry::Damaged: {
        spdlog::warn("Script cache entry '{}' is damaged; recompiling", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return false;
    }
    case storm::script_cache::Entry::Read:
        break;
    }

    storm::script_cache::BufferReader reader(std::move(data));

    try
    {
        // defines (in case of new compiled code depending on defines from cache)
        LoadDefinesFromCache(reader, segment);

        // variables with values
        LoadVariablesFromCache(reader, segment);

        // functions
        LoadFunctionsFromCache(reader, segment);

        // script libraries
        LoadScriptLibrariesFromCache(reader);

        // event handlers
        LoadEventHandlersFromCache(reader);

        // bytecode
        LoadByteCodeFromCache(reader, segment);
    }
    catch (const storm::script_cache::ReaderException &)
    {
        // Intact file that does not parse or apply: a layout bug or a name clash,
        // not disk damage. Undo what was registered for this segment and compile from source.
        spdlog::error("Script cache entry '{}' cannot be applied; recompiling", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        delete[] segment.pCode;
        segment.pCode = nullptr;
        segment.BCode_Program_size = 0;
        segment.BCode_Buffer_size = 0;
        FuncTab.InvalidateBySegmentID(segment.id);
        VarTab.InvalidateBySegmentID(segment.id);
        EventTab.InvalidateBySegmentID(segment.id);
        DefTab.InvalidateBySegmentID(segment.id);
        return false;
    }

    return true;
}

void COMPILER::LoadDefinesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto di = DEFINFO();

        di.segment_id = segment.id;
        auto name = std::string(reader.ReadArray());
        di.name = const_cast<char *>(name.c_str());
        di.deftype = reader.Read<uint32_t>();

        switch (di.deftype)
        {
        case NUMBER:
            di.data4b = static_cast<uintptr_t>(reader.Read<int32_t>());
            break;

        case FLOAT_NUMBER:
            di.data4b = static_cast<uintptr_t>(reader.Read<float>());
            break;

        case STRING: {
            auto value = reader.ReadArray();
            const auto new_ptr = new char[value.size() + 1];
            std::ranges::copy(value, new_ptr);
            new_ptr[value.size()] = '\0';
            di.data4b = reinterpret_cast<uintptr_t>(new_ptr);
            break;
        }
        }

        DefTab.AddDef(di);
    }
}

void COMPILER::FormatAllDialog(const char *directory_name)
{
    const auto vPaths = fio->_GetPathsOrFilenamesByMask(directory_name, "*.c", true);
    for (const std::string &curPath : vPaths)
    {
        FormatDialog(curPath.c_str());
    }
}

void COMPILER::FormatDialog(const char *file_name)
{
    uint32_t FileSize;
    TOKEN Token;
    S_TOKEN_TYPE Token_type;
    char sFileName[MAX_PATH];
    char buffer[MAX_PATH]{};
    char sNewLine[] = {0xd, 0xa, 0};
    bool bExportString;

    if (file_name == nullptr)
    {
        return;
    }

    uint32_t nTxt = 0;
    uint32_t nLnk = 0;

    // sprintf_s(sFileName,"PROGRAM\\%sc",file_name);
    strcpy_s(sFileName, file_name);

    char *pFileData = LoadFile(file_name, FileSize, true);
    if (pFileData == nullptr)
    {
        return;
    }

    auto fileS = fio->_CreateFile(sFileName, std::ios::binary | std::ios_base::out);
    if (!fileS.is_open())
    {
        return;
    }

    // sprintf_s(sFileName,"PROGRAM\\%s",file_name);
    strcpy_s(sFileName, file_name);
    sFileName[strlen(sFileName) - 1] = 0;
    strcat_s(sFileName, "h");
    auto fileS2 = fio->_CreateFile(sFileName, std::ios::binary | std::ios_base::out);
    if (!fileS2.is_open())
    {
        fio->_CloseFile(fileS);
        delete[] pFileData;
        return;
    }

    // pFileData = LoadFile(file_name,FileSize,true);
    // if(pFileData == 0) {fio->_CloseHandle(fh); fio->_CloseHandle(fhH); return;}

    uint32_t n;
    const uint32_t nFullNameLen = strlen(file_name);
    for (n = nFullNameLen; n > 0; n--)
    {
        if (file_name[n] == '\\')
            break;
    }
    sprintf_s(sFileName, "DIALOGS%s", file_name + n);
    sFileName[strlen(sFileName) - 1] = 0;
    strcat_s(sFileName, "h");

    fio->_WriteFile(fileS, buffer, strlen(buffer));
    fio->_WriteFile(fileS, sNewLine, strlen(sNewLine));

    sprintf_s(buffer, "string DLG_TEXT[0] = {        ");
    fio->_WriteFile(fileS2, buffer, strlen(buffer));
    fio->_WriteFile(fileS2, sNewLine, strlen(sNewLine));

    Token.SetProgram(pFileData, pFileData);

    do
    {
        Token_type = Token.FormatGet();
        switch (Token_type)
        {
        case DOT:
            fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
            Token_type = Token.FormatGet();
            if (Token_type != OPEN_BRACKET)
            {
                if (Token.GetData())
                {
                    // node text --------------------------------------------
                    if (storm::iEquals(Token.GetData(), "text"))
                    {
                        fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));

                        // fio->_WriteFile(fhH,sNewLine,strlen(sNewLine),&dwR);
                        // sprintf_s(sFileName,"// [NODE START] ",nTxt);
                        // fio->_WriteFile(fhH,sFileName,strlen(sFileName),&dwR);
                        // fio->_WriteFile(fhH,sNewLine,strlen(sNewLine),&dwR);

                        constexpr size_t newline_len = std::size(sNewLine) - 1;
                        do
                        {
                            Token_type = Token.FormatGet();
                            if (Token_type == STRING)
                            {
                                bExportString = true;
                                if (strlen(Token.GetData()) == 3 && Token.GetData()[1] <= 0x39)
                                {
                                    switch (Token.GetData()[1])
                                    {
                                    case '!':
                                    case ',':
                                    case '.':
                                    case '?':
                                    case '-':
                                    case ':':
                                        bExportString = true;
                                        break;
                                    default:
                                        bExportString = false;
                                        break;
                                    }
                                }

                                // if(strlen(Token.GetData()) == 3 && Token.GetData()[1] <= 0x39)
                                if (!bExportString)
                                {
                                    fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                                }
                                else
                                {
                                    fio->_WriteFile(fileS2, Token.GetData(), strlen(Token.GetData()));
                                    fio->_WriteFile(fileS2, ",", std::size(",") - 1);
                                    fio->_WriteFile(fileS2, sNewLine, newline_len);
                                    sprintf_s(sFileName, "DLG_TEXT[%d]", nTxt);
                                    fio->_WriteFile(fileS, sFileName, strlen(sFileName));
                                    nTxt++;
                                }
                            }
                            else
                            {
                                fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                            }
                            if (Token_type == END_OF_PROGRAMM)
                            {
                                break;
                            }
                        } while (Token_type != SEPARATOR);
                        break;
                    }
                }
            }
            if (Token.GetData())
            {
                fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
            }
            break;
        case UNKNOWN:
            if (Token.GetData())
            {
                fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                if (storm::iEquals(Token.GetData(), "link"))
                {
                    Token_type = Token.FormatGet();
                    fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                    if (Token_type == DOT)
                    {
                        Token_type = Token.FormatGet();
                        if (Token.GetData())
                        {
                            fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                        }
                        if (Token_type == UNKNOWN)
                        {
                            Token_type = Token.FormatGet();
                            if (Token.GetData())
                            {
                                fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                            }
                            if (Token_type != DOT)
                            {
                                constexpr size_t newline_len = std::size(sNewLine) - 1;
                                do
                                {
                                    Token_type = Token.FormatGet();
                                    if (Token_type == STRING)
                                    {
                                        bExportString = true;
                                        if (strlen(Token.GetData()) == 3 && Token.GetData()[1] <= 0x39)
                                        {
                                            switch (Token.GetData()[1])
                                            {
                                            case '!':
                                            case ',':
                                            case '.':
                                            case '?':
                                            case '-':
                                            case ':':
                                                bExportString = true;
                                                break;
                                            default:
                                                bExportString = false;
                                                break;
                                            }
                                        }

                                        // if(strlen(Token.GetData()) == 3 && Token.GetData()[1] <= 0x39)
                                        if (!bExportString)
                                        {
                                            fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                                        }
                                        else
                                        {
                                            fio->_WriteFile(fileS2, Token.GetData(), strlen(Token.GetData()));
                                            fio->_WriteFile(fileS2, ",", std::size(",") - 1);
                                            fio->_WriteFile(fileS2, sNewLine, newline_len);
                                            sprintf_s(sFileName, "DLG_TEXT[%d]", nTxt);
                                            fio->_WriteFile(fileS, sFileName, strlen(sFileName));
                                            nTxt++;
                                        }
                                    }
                                    else
                                    {
                                        fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
                                    }
                                    if (Token_type == END_OF_PROGRAMM)
                                    {
                                        break;
                                    }
                                } while (Token_type != SEPARATOR);
                                break;
                            }
                        }
                    }
                }
            }
            break;
        default:
            if (Token.GetData())
            {
                fio->_WriteFile(fileS, Token.GetData(), strlen(Token.GetData()));
            }
            break;
        }
    } while (Token_type != END_OF_PROGRAMM);

    delete[] pFileData;

    sprintf_s(buffer, "};");
    fio->_WriteFile(fileS2, sNewLine, strlen(sNewLine));
    fio->_WriteFile(fileS2, buffer, strlen(buffer));
    fio->_SetFilePointer(fileS2, 0, std::ios::beg);
    sprintf_s(buffer, "string DLG_TEXT[%d] = {", nTxt);
    fio->_WriteFile(fileS2, buffer, strlen(buffer));

    fio->_CloseFile(fileS2);
    fio->_CloseFile(fileS);
}

void STRING_CODEC::VariableChanged()
{
#ifdef _WIN32 // S_DEBUG
    CDebug->SetTraceMode(TMODE_MAKESTEP);
#endif
}
