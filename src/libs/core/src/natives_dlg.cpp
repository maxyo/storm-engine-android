// VM glue of the Caribbean Legend localisation natives: DLGO, DLG_Convert, DLG_ConvertE,
// DLG_ReplaceAllMatches, StringFromKey, __RefreshLocalization. The template language itself is
// storm::dlg (dlg_dsl.hpp); this file answers its Host questions from the running scripts:
// - text files: resource/ini/texts/<language dir>/<file>, parsed once and kept in a TextCache;
//   StringFromKey keys: every file under Localization_Assets/, indexed on first use;
//   common strings ("" file) and the language directory: the string service (dlg_text_hooks.hpp);
// - player = global `pchar`, dialogue partner = global `CharacterRef` (CL's dialog.c), context =
//   the reference passed to the native;
// - script calls: the function is looked up by name and run with its arguments converted to the
//   declared parameter types;
// - $value(NAME): the script #define table.
// Guesses (see local-testing/cl20/specs/dlg-dsl.md): $goodBad tiers from reputation.nobility
// (< 41 bad, as CL's PCharRepPhrase; > 70 good), $city = the partner's `City` attribute, $br = "\n",
// $red = four spaces; $dialogBreak becomes a line break (our dialogue window has no page split;
// reported once).

#include "natives_dlg.h"

#include "compiler.h"
#include "dlg_dsl.hpp"
#include "dlg_text_hooks.hpp"
#include "script_stub.hpp"
#include "v_file_service.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace storm::dlg
{
namespace
{
TextHooks g_hooks;

std::string languageDirectory()
{
    if (!g_hooks.language_directory)
    {
        static bool reported = false;
        if (!reported)
            spdlog::error("localisation natives: the string service has not installed its hooks; texts are not found");
        reported = true;
        return {};
    }
    return g_hooks.language_directory();
}

std::optional<std::string> readFile(const std::string &path)
{
    char *buffer = nullptr;
    uint32_t size = 0;
    if (!fio->LoadFile(path.c_str(), &buffer, &size) || !buffer)
        return std::nullopt;
    std::string content(buffer, size);
    delete[] buffer;
    return content;
}

std::string textsRoot(const std::string &language)
{
    return "resource/ini/texts/" + language + "/";
}

// Parsed text files of the current language; rebuilt when the language changes.
struct Texts
{
    std::string language;
    std::unique_ptr<TextCache> cache;
    bool localisation_loaded = false;
    std::vector<TextTable> localisation;

    void use(const std::string &lang)
    {
        if (cache && lang == language)
            return;
        language = lang;
        cache = std::make_unique<TextCache>(
            [lang](std::string_view relative) { return readFile(textsRoot(lang) + std::string(relative)); },
            size_t{32} << 20);
        localisation_loaded = false;
        localisation.clear();
    }

    std::optional<std::string> localisationKey(std::string_view key)
    {
        if (!localisation_loaded)
        {
            localisation_loaded = true;
            const std::string dir = textsRoot(language) + "Localization_Assets";
            for (const auto &path : fio->_GetPathsOrFilenamesByMask(dir.c_str(), "*.txt", true, false, true, true))
                if (auto content = readFile(path))
                    localisation.push_back(TextTable::parse(std::move(*content)));
            if (localisation.empty())
                spdlog::error("StringFromKey: no files under {}", dir);
        }
        for (const auto &table : localisation)
            if (auto value = table.find(key))
                return std::string(*value);
        return std::nullopt;
    }
} g_texts;

std::set<std::string> g_reported;

std::string dataText(DATA *v)
{
    if (!v)
        return {};
    v = v->GetVarPointer();
    if (!v)
        return {};
    switch (v->GetType())
    {
    case VAR_INTEGER:
        return std::to_string(v->GetInt());
    case VAR_FLOAT:
        return fmt::format("{}", v->GetFloat());
    case VAR_STRING: {
        const char *s = v->GetString();
        return s ? s : "";
    }
    default:
        return {};
    }
}

class VmHost final : public Host
{
  public:
    VmHost(natives::NativeCall &call, ATTRIBUTES *context) : call_(call), context_(context)
    {
    }

    std::optional<std::string> lookupKey(std::string_view file, std::string_view key) override
    {
        if (file.empty())
            return g_hooks.common_string ? g_hooks.common_string(key) : std::nullopt;
        const TextTable *table = g_texts.cache ? g_texts.cache->file(normaliseTextPath(file)) : nullptr;
        if (!table)
            return std::nullopt;
        if (auto value = table->find(key))
            return std::string(*value);
        return std::nullopt;
    }

    std::optional<std::string> lookupLocalisation(std::string_view key) override
    {
        return g_texts.localisationKey(key);
    }

    std::optional<std::string> contextAttribute(std::string_view path) override
    {
        if (!context_)
            return std::nullopt;
        ATTRIBUTES *a = context_->FindAClass(context_, std::string(path).c_str());
        if (!a || !a->HasValue())
            return std::nullopt;
        return a->GetValue();
    }

    std::optional<bool> isFemale(Who who) override
    {
        ATTRIBUTES *c = character(who);
        if (!c)
            return std::nullopt;
        ATTRIBUTES *sex = c->GetAttributeClass("sex");
        return sex && sex->HasValue() && sex->GetValue() == "woman";
    }

    std::optional<std::string> characterField(Who who, CharacterField field) override
    {
        ATTRIBUTES *c = character(who);
        if (!c)
            return std::nullopt;
        switch (field)
        {
        case CharacterField::FirstName:
            return attribute(c, "name");
        case CharacterField::LastName:
            return attribute(c, "lastname");
        case CharacterField::FullName: {
            const ScriptArg arg{who == Who::Player ? ScriptArg::Kind::Player
                                : who == Who::Npc  ? ScriptArg::Kind::Npc
                                                   : ScriptArg::Kind::Context,
                                {}};
            return callScript("GetFullName", std::span(&arg, 1));
        }
        case CharacterField::ShipName:
            return attribute(c, "Ship.Name");
        case CharacterField::Town:
            return attribute(c, "City");
        }
        return std::nullopt;
    }

    int reputationTier() override
    {
        ATTRIBUTES *c = character(Who::Player);
        const auto value = c ? attribute(c, "reputation.nobility") : std::nullopt;
        const int nobility = value ? std::atoi(value->c_str()) : 50;
        return nobility < 41 ? 2 : nobility > 70 ? 0 : 1;
    }

    std::optional<std::string> callScript(std::string_view function, std::span<const ScriptArg> args) override
    {
        const size_t code = call_.functions.FindFunc(std::string(function));
        const FuncInfo *fi = code == INVALID_FUNC_CODE ? nullptr : call_.functions.GetFuncPtr(code);
        if (!fi || fi->segment_id == INTERNAL_SEGMENT_ID || fi->segment_id == IMPORTED_SEGMENT_ID)
            return std::nullopt;
        if (args.size() != fi->arguments)
            once(fmt::format("dlg-args:{}", function),
                 fmt::format("localisation: {} takes {} arguments, the template passes {}; missing ones are 0/\"\", "
                             "extra ones dropped",
                             function, fi->arguments, args.size()));
        const uint32_t before = call_.stack.GetDataNum();
        for (uint32_t n = 0; n < fi->arguments; ++n)
        {
            const S_TOKEN_TYPE type = n < fi->local_vars.size() ? fi->local_vars[n].type : VAR_STRING;
            DATA *v = call_.stack.Push();
            if (n >= args.size())
            {
                pushDefault(v, type);
                continue;
            }
            const ScriptArg &a = args[n];
            if (a.kind != ScriptArg::Kind::Text)
            {
                pushObject(v, a.kind);
                continue;
            }
            switch (type)
            {
            case VAR_INTEGER:
                v->Set(static_cast<int32_t>(std::strtol(a.text.c_str(), nullptr, 10)));
                break;
            case VAR_FLOAT:
                v->Set(static_cast<float>(std::strtod(a.text.c_str(), nullptr)));
                break;
            default:
                v->Set(a.text);
                break;
            }
        }
        DATA *result = nullptr;
        const bool ok = call_.execute(static_cast<uint32_t>(code), result);
        std::string text = ok && result ? dataText(result) : std::string{};
        if (call_.stack.GetDataNum() > before)
            call_.stack.InvalidateFrom(before);
        if (!ok)
            spdlog::error("localisation: script function {} failed", function);
        return text;
    }

    std::optional<std::string> scriptConstant(std::string_view name) override
    {
        const uint32_t code = call_.defines.FindDef(std::string(name).c_str());
        if (code == INVALID_DEF_CODE)
            return std::nullopt;
        DEFINFO di;
        if (!call_.defines.GetDef(di, code))
            return std::nullopt;
        switch (di.deftype)
        {
        case NUMBER:
            return std::to_string(static_cast<int32_t>(di.data4b));
        case FLOAT_NUMBER: {
            const auto bits = static_cast<uint32_t>(di.data4b);
            float f;
            std::memcpy(&f, &bits, sizeof f);
            return fmt::format("{}", f);
        }
        case STRING:
            return std::string(reinterpret_cast<const char *>(di.data4b));
        default:
            return std::nullopt;
        }
    }

    int randomInt(int bound) override
    {
        return bound <= 1 ? 0 : rand() % bound; // the engine's rand(), as script rand()
    }

    std::string_view language() override
    {
        language_ = g_texts.language;
        for (auto &c : language_)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return language_;
    }

    std::string markup(Markup markup) override
    {
        switch (markup)
        {
        case Markup::LineBreak:
            return "\n";
        case Markup::DialogBreak:
            storm::reportScriptStub("$dialogBreak", "no page split in the dialogue window; becomes a line break");
            return "\n";
        case Markup::Indent:
            return "    ";
        }
        return {};
    }

    void report(const Diagnostic &d) override
    {
        once(fmt::format("{}:{}", diagnosticKindName(d.kind), d.name),
             fmt::format("localisation: {} '{}': {}", diagnosticKindName(d.kind), d.name, d.detail));
    }

  private:
    static void once(const std::string &key, const std::string &message)
    {
        if (g_reported.insert(key).second)
            spdlog::warn("{}", message);
    }

    static std::optional<std::string> attribute(ATTRIBUTES *c, const char *path)
    {
        ATTRIBUTES *a = c->FindAClass(c, path);
        if (!a || !a->HasValue())
            return std::nullopt;
        return a->GetValue();
    }

    DATA *global(const char *name) const
    {
        const size_t code = call_.variables.FindVar(name);
        const VarInfo *vi = code == INVALID_VAR_CODE ? nullptr : call_.variables.GetVar(code);
        return vi && vi->value ? vi->value->GetVarPointer() : nullptr;
    }

    ATTRIBUTES *character(Who who) const
    {
        if (who == Who::Context)
            return context_;
        DATA *v = global(who == Who::Player ? "pchar" : "CharacterRef");
        return v ? v->GetAClass() : nullptr;
    }

    void pushObject(DATA *v, ScriptArg::Kind kind) const
    {
        if (kind == ScriptArg::Kind::Context)
        {
            v->SetType(VAR_AREFERENCE);
            v->SetAReference(context_);
            return;
        }
        DATA *target = global(kind == ScriptArg::Kind::Player ? "pchar" : "CharacterRef");
        v->SetType(VAR_REFERENCE);
        v->SetReference(target);
    }

    static void pushDefault(DATA *v, S_TOKEN_TYPE type)
    {
        switch (type)
        {
        case VAR_INTEGER:
            v->Set(int32_t{0});
            break;
        case VAR_FLOAT:
            v->Set(0.0f);
            break;
        default:
            v->Set("");
            break;
        }
    }

    natives::NativeCall &call_;
    ATTRIBUTES *context_;
    std::string language_;
};

// The arguments of the current native call (the last `arguments` stack entries).
struct CallArgs
{
    natives::NativeCall &call;
    uint32_t base;

    explicit CallArgs(natives::NativeCall &c) : call(c), base(c.stack.GetDataNum() - c.arguments)
    {
    }

    DATA *raw(uint32_t n) const
    {
        return n < call.arguments ? call.stack.Read(base, n) : nullptr;
    }

    std::string text(uint32_t n) const
    {
        return dataText(raw(n));
    }

    // The context reference argument: an object, aref, or nothing (nullptr / NullObject is fine).
    ATTRIBUTES *context(uint32_t n) const
    {
        DATA *v = raw(n);
        if (!v)
            return nullptr;
        if (v->GetType() == VAR_AREFERENCE)
            return v->GetAClass();
        v = v->GetVarPointer();
        if (!v || (v->GetType() != VAR_OBJECT && v->GetType() != VAR_AREFERENCE))
            return nullptr;
        return v->GetAClass();
    }

    void popAll() const
    {
        for (uint32_t n = 0; n < call.arguments; ++n)
            call.stack.Pop();
    }
};

DATA *pushString(natives::NativeCall &call, DATA *&result, const std::string &value)
{
    DATA *v = call.stack.Push();
    v->Set(value);
    return result = v;
}
} // namespace

void setTextHooks(TextHooks hooks)
{
    g_hooks = std::move(hooks);
}

DATA *callDlgNative(DlgNative native, natives::NativeCall &call, DATA *&result)
{
    result = nullptr;
    const CallArgs args(call);
    g_texts.use(languageDirectory());
    std::string out;
    switch (native)
    {
    case DlgNative::Dlgo: {
        // DLGO(input[, context])
        VmHost host(call, args.context(1));
        Expander expander(host);
        out = expander.expand(args.text(0));
        break;
    }
    case DlgNative::Convert:
    case DlgNative::ConvertE: {
        // DLG_Convert(key, file[, context]) / DLG_ConvertE
        VmHost host(call, args.context(2));
        Expander expander(host);
        out = native == DlgNative::Convert ? expander.convert(args.text(1), args.text(0))
                                           : expander.convertOrEmpty(args.text(1), args.text(0));
        break;
    }
    case DlgNative::StringFromKey: {
        // StringFromKey(key, parameters...)
        std::vector<std::string> parameters;
        for (uint32_t n = 1; n < call.arguments; ++n)
            parameters.push_back(args.text(n));
        VmHost host(call, nullptr);
        Expander expander(host);
        out = expander.stringFromKey(args.text(0), parameters);
        break;
    }
    case DlgNative::ReplaceAllMatches: {
        // DLG_ReplaceAllMatches(&input, key, replacement[, from])
        DATA *target = args.raw(0) ? args.raw(0)->GetVarPointer() : nullptr;
        if (!target || target->GetType() != VAR_STRING)
        {
            call.compiler.SetError("DLG_ReplaceAllMatches: the first argument must be a string variable");
            args.popAll();
            return nullptr;
        }
        std::string text = dataText(target);
        const int32_t from = call.arguments > 3 && args.raw(3) ? args.raw(3)->GetVarPointer()->GetInt() : 0;
        replaceAllMatches(text, args.text(1), args.text(2), static_cast<size_t>(std::max(from, 0)));
        target->Set(text);
        args.popAll();
        return nullptr;
    }
    case DlgNative::RefreshLocalization:
        // the language or the text files changed: parse them again on next use
        g_texts.cache.reset();
        g_texts.language.clear();
        args.popAll();
        return nullptr;
    }
    args.popAll();
    return pushString(call, result, out);
}
} // namespace storm::dlg
