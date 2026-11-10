// Internal script functions added for Caribbean Legend 2.0 (list: natives_ext_list.h).
//
// CL declares them in its scripts and its engine provides them built in. Their behaviour is
// inferred from how CL 2.0 scripts call them and from the comments its scripts carry next to the
// declarations; nothing here comes from CL's engine. Natives CL declares but never calls are
// explicit stubs: they consume their arguments, return a default value and log once (see
// kStubs); implement them when a script starts calling them.

#include "natives_ext.h"

#include "compiler.h"
#include "core_impl.h"
#include "natives_logic.hpp"
#include "script_stub.hpp"
#include "string_compare.hpp"
#include "watermark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace storm::natives
{
namespace
{
enum NativeExtId : uint32_t
{
#define STORM_NATIVE(id, name, arguments, return_type, varargs) id,
#include "natives_ext_list.h"
#undef STORM_NATIVE
    kNativeExtCount
};

struct NativeDesc
{
    const char *name;
    S_TOKEN_TYPE return_type;
    bool varargs;
};

constexpr NativeDesc kNatives[] = {
#define STORM_NATIVE(id, name, arguments, return_type, varargs) {name, return_type, varargs},
#include "natives_ext_list.h"
#undef STORM_NATIVE
};
static_assert(std::size(kNatives) == kNativeExtCount);

// What each stub does instead of the real behaviour.
struct StubDesc
{
    NativeExtId id;
    const char *behaviour;
};
constexpr StubDesc kStubs[] = {
    {FUNC_X_POST_EVENT_FOR_OBJECT, "object-scoped events are not implemented (unused by CL 2.0); nothing is posted"},
    {FUNC_X_POST_EVENT_FOR_OBJECT_NTS, "object-scoped events are not implemented (unused by CL 2.0); nothing is posted"},
    {FUNC_X_EVENT_FOR_OBJECT, "object-scoped events are not implemented (unused by CL 2.0); nothing is sent"},
    {FUNC_X_SET_EVENT_HANDLER_FOR_OBJECT, "object-scoped events are not implemented (unused by CL 2.0)"},
    {FUNC_X_DEL_EVENT_HANDLER_FOR_OBJECT, "object-scoped events are not implemented (unused by CL 2.0)"},
    {FUNC_X_SET_EVENT_STATIC_HANDLER_FOR_OBJECT, "object-scoped events are not implemented (unused by CL 2.0)"},
    {FUNC_X_SET_EVENT_FORMAT, "event argument formats are not checked (unused by CL 2.0)"},
    {FUNC_X_CREATE_RECOVERY_FILE, "no recovery file is written (unused by CL 2.0)"},
    {FUNC_X_VARIABLE_CHANGED_OBSERVER, "attribute observers are not implemented (unused by CL 2.0)"},
    {FUNC_X_CREATE_INDEX, "attribute indices are not implemented (unused by CL 2.0); returns -1"},
    {FUNC_X_REBUILD_INDEX, "attribute indices are not implemented (unused by CL 2.0)"},
    {FUNC_X_DELETE_INDEX, "attribute indices are not implemented (unused by CL 2.0)"},
    {FUNC_X_FIND_BY_INDEX, "attribute indices are not implemented (unused by CL 2.0); the result is unchanged"},
    {FUNC_X_LOAD_MODULE, "script modules are not implemented (unused by CL 2.0); returns false"},
    {FUNC_X_UNLOAD_MODULE, "script modules are not implemented (unused by CL 2.0)"},
    {FUNC_X_MODULE_IS_LOADED, "script modules are not implemented (unused by CL 2.0); returns false"},
    {FUNC_X_GET_SUPER_SEGMENT, "script modules are not implemented (unused by CL 2.0); returns \"\""},
    {FUNC_X_RUN_SCRIPT_TESTS, "CL's developer script self-tests do not exist in this engine; nothing runs"},
    {FUNC_X_GET_SCRIPT_PROFILE, "script profiling output is not implemented (unused by CL 2.0)"},
    {FUNC_X_RESET_SCRIPT_PROFILE, "script profiling output is not implemented (unused by CL 2.0)"},
    {FUNC_X_REINIT_SPEED_COLORS, "the ship speed colour table of CL's battle interface is not wired yet"},
    {FUNC_X_UPDATE_CONTROL_GROUP, "control groups of CL's input layer are not wired yet (touch input drives controls)"},
    {FUNC_X_SET_CONTROL_HOLD, "hold-to-activate controls are not wired yet (touch input drives controls)"},
    {FUNC_X_SET_HOLD_THRESHOLD, "hold-to-activate controls are not wired yet (touch input drives controls)"},
    {FUNC_X_RESET_HOLD_CONTROLS, "hold-to-activate controls are not wired yet (touch input drives controls)"},
    {FUNC_X_SET_HOLD_CONTROLS, "hold-to-activate controls are not wired yet; returns false"},
    {FUNC_X_SET_CLASSIC_COMBAT_CONTROLS, "the combat control scheme switch is not wired yet"},
    {FUNC_X_SET_CONTROL_SCHEME, "the combat control scheme switch is not wired yet"},
};

// Seed of hrand, set by SetGlobalSeed (CL sets it from a script variable once per game day and
// again after loading a save).
std::string g_global_seed;

class Args
{
  public:
    explicit Args(NativeCall &call)
        : call_(call), base_(call.stack.GetDataNum() - call.arguments)
    {
    }

    ~Args()
    {
        for (uint32_t n = 0; n < call_.arguments; ++n)
            call_.stack.Pop();
    }

    Args(const Args &) = delete;
    Args &operator=(const Args &) = delete;

    [[nodiscard]] uint32_t size() const
    {
        return call_.arguments;
    }

    // the argument with references resolved
    [[nodiscard]] DATA *value(uint32_t n) const
    {
        DATA *v = n < call_.arguments ? call_.stack.Read(base_, n) : nullptr;
        return v ? v->GetVarPointer() : nullptr;
    }

    [[nodiscard]] DATA *raw(uint32_t n) const
    {
        return n < call_.arguments ? call_.stack.Read(base_, n) : nullptr;
    }

    [[nodiscard]] int32_t integer(uint32_t n) const
    {
        DATA *v = value(n);
        if (!v)
            return 0;
        if (v->GetType() == VAR_FLOAT)
            return static_cast<int32_t>(v->GetFloat());
        return v->GetInt();
    }

    [[nodiscard]] float real(uint32_t n) const
    {
        DATA *v = value(n);
        if (!v)
            return 0.0f;
        if (v->GetType() == VAR_INTEGER)
            return static_cast<float>(v->GetInt());
        return v->GetFloat();
    }

    [[nodiscard]] std::string text(uint32_t n) const
    {
        DATA *v = value(n);
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

    [[nodiscard]] uint32_t base() const
    {
        return base_;
    }

  private:
    NativeCall &call_;
    uint32_t base_;
};

DATA *pushInt(NativeCall &call, DATA *&result, int32_t value)
{
    DATA *v = call.stack.Push();
    v->Set(value);
    return result = v;
}

DATA *pushFloat(NativeCall &call, DATA *&result, float value)
{
    DATA *v = call.stack.Push();
    v->Set(value);
    return result = v;
}

DATA *pushString(NativeCall &call, DATA *&result, const std::string &value)
{
    DATA *v = call.stack.Push();
    v->Set(value);
    return result = v;
}

DATA *pushDefault(NativeCall &call, DATA *&result, S_TOKEN_TYPE type)
{
    switch (type)
    {
    case VAR_INTEGER:
        return pushInt(call, result, 0);
    case VAR_FLOAT:
        return pushFloat(call, result, 0.0f);
    case VAR_STRING:
        return pushString(call, result, {});
    default:
        result = nullptr;
        return nullptr;
    }
}

void warnOnce(std::string_view key, std::string_view message)
{
    static std::vector<std::string> seen;
    if (std::ranges::find(seen, key) != seen.end())
        return;
    seen.emplace_back(key);
    spdlog::warn("{}", message);
}

// --- sorting -------------------------------------------------------------------------------

std::string elementText(DATA *e)
{
    switch (e->GetType())
    {
    case VAR_INTEGER:
        return std::to_string(e->GetInt());
    case VAR_FLOAT:
        return fmt::format("{}", e->GetFloat());
    case VAR_STRING: {
        const char *s = e->GetString();
        return s ? s : "";
    }
    default:
        return {};
    }
}

// Reorders the primitive elements of a script array: `order[i]` is the old index of the
// element that ends at position i.
bool permuteArray(COMPILER &compiler, DATA *array, const std::vector<size_t> &order)
{
    const uint32_t n = array->GetElementsNum();
    if (n == 0)
        return true;
    const S_TOKEN_TYPE type = array->GetArrayElement(0)->GetType();
    if (type != VAR_INTEGER && type != VAR_FLOAT && type != VAR_STRING)
    {
        compiler.SetError("sorting/shuffling an array of this type is not supported (only int, float, string)");
        return false;
    }
    std::vector<int32_t> ints;
    std::vector<float> floats;
    std::vector<std::string> strings;
    for (uint32_t i = 0; i < n; ++i)
    {
        DATA *e = array->GetArrayElement(i);
        if (type == VAR_INTEGER)
            ints.push_back(e->GetInt());
        else if (type == VAR_FLOAT)
            floats.push_back(e->GetFloat());
        else
            strings.emplace_back(elementText(e));
    }
    for (uint32_t i = 0; i < n; ++i)
    {
        const size_t from = order[i];
        if (type == VAR_INTEGER)
            array->Set(ints[from], i);
        else if (type == VAR_FLOAT)
            array->Set(floats[from], i);
        else
            array->Set(strings[from].c_str(), i);
    }
    return true;
}

DATA *arrayArgument(COMPILER &compiler, const Args &args, const char *native)
{
    DATA *array = args.value(0);
    if (!array || !array->IsArray())
    {
        compiler.SetError("%s: the argument is not an array", native);
        return nullptr;
    }
    return array;
}

// ArraySort(&array): ascending; strings in byte order (CL sorts "_vs_" keys "lexicographically").
void arraySort(NativeCall &call, const Args &args)
{
    if (args.size() > 1)
        warnOnce("ArraySort.extra", "ArraySort: arguments after the array are ignored (CL 2.0 never passes them)");
    DATA *array = arrayArgument(call.compiler, args, "ArraySort");
    if (!array)
        return;
    const uint32_t n = array->GetElementsNum();
    if (n == 0)
        return;
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    const S_TOKEN_TYPE type = array->GetArrayElement(0)->GetType();
    std::vector<std::string> texts;
    std::vector<double> numbers;
    for (uint32_t i = 0; i < n; ++i)
    {
        DATA *e = array->GetArrayElement(i);
        if (type == VAR_STRING)
            texts.emplace_back(elementText(e));
        else
            numbers.push_back(type == VAR_FLOAT ? e->GetFloat() : static_cast<double>(e->GetInt()));
    }
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
        return type == VAR_STRING ? texts[a] < texts[b] : numbers[a] < numbers[b];
    });
    permuteArray(call.compiler, array, order);
}

void arrayShuffle(NativeCall &call, const Args &args)
{
    DATA *array = arrayArgument(call.compiler, args, "ArrayShuffle");
    if (!array)
        return;
    const uint32_t n = array->GetElementsNum();
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    for (size_t i = n; i > 1; --i)
        std::swap(order[i - 1], order[static_cast<size_t>(rand()) % i]); // the engine's rand(), as script rand()
    permuteArray(call.compiler, array, order);
}

// SmartSort(rObject, sAttr, sType, bAscend, iOffset, sPrefix[, selection]) and SmartSortTable.
// Documented by CL in Program/interface/utils/sorting.c: rObject is an object/aref (its children
// whose names start with sPrefix are the records) or an array of primitives; sAttr is the
// attribute path inside a record ("" = the record's own value); iOffset leading characters of
// the value are skipped. SmartSort moves whole records with their names; SmartSortTable keeps
// the row names in place (tr1, tr2, ...) and moves the contents. The optional selection
// argument is never passed by CL 2.0 and is ignored with a warning.
void smartSort(NativeCall &call, const Args &args, bool table)
{
    const char *native = table ? "SmartSortTable" : "SmartSort";
    const std::string attr = args.text(1);
    const std::string type_name = args.text(2);
    const bool ascending = args.integer(3) != 0;
    const int32_t offset = args.integer(4);
    const std::string prefix = args.text(5);
    if (args.size() > 6)
        warnOnce(std::string(native) + ".selection",
                 fmt::format("{}: the selection argument is not supported and is ignored", native));

    SortType type{};
    if (!parseSortType(type_name, type))
    {
        call.compiler.SetError("%s: unknown sort type '%s'", native, type_name.c_str());
        return;
    }
    auto less = [&](const SortKey &a, const SortKey &b) {
        return ascending ? sortKeyLess(a, b, type) : sortKeyLess(b, a, type);
    };

    DATA *value = args.value(0);
    if (value && value->IsArray())
    {
        const uint32_t n = value->GetElementsNum();
        std::vector<SortKey> keys;
        for (uint32_t i = 0; i < n; ++i)
            keys.push_back(makeSortKey(elementText(value->GetArrayElement(i)), 0, type));
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), size_t{0});
        std::ranges::stable_sort(order, [&](size_t a, size_t b) { return less(keys[a], keys[b]); });
        permuteArray(call.compiler, value, order);
        return;
    }

    ATTRIBUTES *root = value ? value->GetAClass() : nullptr;
    if (!root)
    {
        call.compiler.SetError("%s: the first argument is neither an object nor an array", native);
        return;
    }
    std::vector<size_t> slots; // positions of the records among the children
    std::vector<SortKey> keys;
    const size_t count = root->GetAttributesNum();
    for (size_t i = 0; i < count; ++i)
    {
        ATTRIBUTES *child = root->GetAttributeClass(static_cast<uint32_t>(i));
        const std::string_view name = child->GetThisName();
        if (!prefix.empty() && !storm::iEquals(name.substr(0, std::min(name.size(), prefix.size())), prefix))
            continue;
        ATTRIBUTES *field = attr.empty() ? child : child->FindAClass(child, attr.c_str());
        slots.push_back(i);
        keys.push_back(makeSortKey(field && field->HasValue() ? std::string_view(field->GetValue()) : "", offset, type));
    }
    std::vector<size_t> sorted(slots.size());
    std::iota(sorted.begin(), sorted.end(), size_t{0});
    std::ranges::stable_sort(sorted, [&](size_t a, size_t b) { return less(keys[a], keys[b]); });

    std::vector<uint32_t> names; // row names in their original order (SmartSortTable)
    for (const size_t slot : slots)
        names.push_back(root->GetAttributeClass(static_cast<uint32_t>(slot))->GetThisNameCode());
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t{0});
    for (size_t k = 0; k < slots.size(); ++k)
        order[slots[k]] = slots[sorted[k]];
    root->ReorderAttributes(order);
    if (table)
        for (size_t k = 0; k < slots.size(); ++k)
            root->GetAttributeClass(static_cast<uint32_t>(slots[k]))->SetNameCode(names[k]);
}

// --- events --------------------------------------------------------------------------------

// PostEventNoTimeScale(name, delay[, format, data...]): PostEvent whose delay runs in real time.
// The post-event queue advances by the scaled frame time, so the delay is scaled by the current
// time scale when posting. Approximation: a time scale change while the event waits is not
// accounted for (CL posts these with delay 0 or a short UI delay).
void postEventNoTimeScale(NativeCall &call, const Args &args)
{
    const std::string name = args.text(0);
    const float scale = core_internal.GetTimeScale();
    const auto delay = static_cast<uint32_t>(std::max(0.0f, static_cast<float>(args.integer(1)) * scale));
    MESSAGE *message = nullptr;
    if (args.size() >= 4)
    {
        message = new MESSAGE();
        call.compiler.CreateMessage(message, args.base(), 2);
        message->Move2Start();
    }
    call.compiler.AddPostEvent(new S_EVENTMSG(name.c_str(), message, delay));
}

void clearPostEventsForEvent(NativeCall &call, const Args &args)
{
    const std::string name = args.text(0);
    for (uint32_t n = 0; n < call.posts.GetClassesNum(); ++n)
    {
        S_EVENTMSG *message = call.posts.Read(n);
        if (message && message->pEventName && storm::iEquals(std::string_view(message->pEventName), name))
            message->Invalidate();
    }
}

std::string callerOfCaller(const CallStack &calls)
{
    auto copy = calls;
    for (int skip = 0; skip < 2 && !copy.empty(); ++skip)
        copy.pop(); // the native itself, then the function asking for its caller
    if (copy.empty())
        return {};
    const char *name = std::get<2>(copy.back());
    return name ? name : "";
}

// AppendAttributes(dst, src): copies src's values and children into dst without clearing dst
// (CopyAttributes replaces dst).
void appendAttributes(ATTRIBUTES *dst, const ATTRIBUTES *src)
{
    if (src->HasValue())
        dst->SetValue(std::string_view(src->GetValue()));
    for (size_t i = 0; i < src->GetAttributesNum(); ++i)
    {
        const ATTRIBUTES *child = src->GetAttributeClass(static_cast<uint32_t>(i));
        appendAttributes(&dst->CreateAttribute(child->GetThisName()), child);
    }
}

int32_t ticks()
{
    using namespace std::chrono;
    return static_cast<int32_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

bool isNativeExtVarArgs(uint32_t index)
{
    return index < kNativeExtCount && kNatives[index].varargs;
}

DATA *callNativeExt(uint32_t index, NativeCall &call, DATA *&result)
{
    result = nullptr;
    if (index >= kNativeExtCount)
    {
        call.compiler.SetError("invalid internal function code");
        return nullptr;
    }
    for (const auto &stub : kStubs)
    {
        if (stub.id == index)
        {
            reportScriptStub(kNatives[index].name, stub.behaviour);
            {
                Args args(call);
            }
            if (index == FUNC_X_CREATE_INDEX)
                return pushInt(call, result, -1);
            return pushDefault(call, result, kNatives[index].return_type);
        }
    }

    switch (static_cast<NativeExtId>(index))
    {
    case FUNC_X_CEILF: {
        float v;
        {
            Args args(call);
            v = args.real(0);
        }
        return pushFloat(call, result, std::ceil(v));
    }
    case FUNC_X_HASH: {
        std::string s;
        {
            Args args(call);
            s = args.text(0);
        }
        return pushInt(call, result, hash32(s));
    }
    case FUNC_X_HRAND: {
        // Approximation: CL also adds the dialogue character's id and name to the tag in dialogues.
        int32_t n;
        std::string tag;
        {
            Args args(call);
            n = args.integer(0);
            if (args.size() > 1)
                tag = args.text(1);
        }
        const std::string site = fmt::format("{}@{}", call.caller, call.instruction);
        return pushInt(call, result, hashRandom(n, g_global_seed, site, tag));
    }
    case FUNC_X_SET_GLOBAL_SEED: {
        Args args(call);
        g_global_seed = args.text(0);
        return nullptr;
    }
    case FUNC_X_ASSERT: {
        // assert(condition, message parts...): a failed assertion is a script error with the call stack.
        bool ok;
        std::string message;
        {
            Args args(call);
            ok = args.integer(0) != 0;
            if (!ok)
                for (uint32_t n = 1; n < args.size(); ++n)
                    message += args.text(n);
        }
        if (!ok)
        {
            call.compiler.SetError("assertion failed: %s", message.c_str());
            call.compiler.CollectCallStack();
        }
        return nullptr;
    }
    case FUNC_X_SPRINTF: {
        std::string out;
        std::string error;
        {
            Args args(call);
            std::vector<FormatArg> values;
            for (uint32_t n = 1; n < args.size(); ++n)
            {
                DATA *v = args.value(n);
                if (v && v->GetType() == VAR_INTEGER)
                    values.emplace_back(v->GetInt());
                else if (v && v->GetType() == VAR_FLOAT)
                    values.emplace_back(v->GetFloat());
                else
                    values.emplace_back(args.text(n));
            }
            out = formatScript(args.text(0), values, error);
        }
        if (!error.empty())
            call.compiler.SetError("Sprintf: %s", error.c_str());
        return pushString(call, result, out);
    }
    case FUNC_X_ARRAY_SORT: {
        Args args(call);
        arraySort(call, args);
        return nullptr;
    }
    case FUNC_X_ARRAY_SHUFFLE: {
        Args args(call);
        arrayShuffle(call, args);
        return nullptr;
    }
    case FUNC_X_SMART_SORT:
    case FUNC_X_SMART_SORT_TABLE: {
        // returns its first argument: the sorted object (aref) or variable
        ATTRIBUTES *aref = nullptr;
        DATA *variable = nullptr;
        {
            Args args(call);
            smartSort(call, args, index == FUNC_X_SMART_SORT_TABLE);
            DATA *raw = args.raw(0);
            if (raw && raw->GetType() == VAR_AREFERENCE)
                aref = raw->GetAClass();
            else if (raw)
                variable = raw->GetVarPointer();
        }
        DATA *v = call.stack.Push();
        if (aref)
        {
            v->SetType(VAR_AREFERENCE);
            v->SetAReference(aref);
        }
        else
        {
            v->SetType(VAR_REFERENCE);
            v->SetReference(variable);
        }
        return result = v;
    }
    case FUNC_X_CLEAR_POST_EVENTS_FOR_EVENT: {
        Args args(call);
        clearPostEventsForEvent(call, args);
        return nullptr;
    }
    case FUNC_X_POST_EVENT_NO_TIME_SCALE: {
        Args args(call);
        postEventNoTimeScale(call, args);
        return nullptr;
    }
    case FUNC_X_SET_EVENT_STATIC_HANDLER: {
        std::string event, handler;
        int32_t flag;
        {
            Args args(call);
            event = args.text(0);
            handler = args.text(1);
            flag = args.integer(2);
        }
        call.compiler.SetEventHandler(event.c_str(), handler.c_str(), flag, true);
        return nullptr;
    }
    case FUNC_X_COLLECT_CALL_STACK:
        call.compiler.CollectCallStack();
        return nullptr;
    case FUNC_X_GET_CALLER_NAME:
        return pushString(call, result, callerOfCaller(call.calls));
    case FUNC_X_GET_RDELTA_TIME:
        return pushFloat(call, result, static_cast<float>(core_internal.GetRDeltaTime()));
    case FUNC_X_GET_LAST_COMMIT:
        // CL stores its build revision in saves; ours is the build watermark (platform, commit, options).
        return pushString(call, result, fmt::format("{}", STORM_BUILD_WATERMARK_STRING));
    case FUNC_X_ATTR_RENAME: {
        bool ok = false;
        {
            Args args(call);
            DATA *v = args.value(0);
            ATTRIBUTES *a = v ? v->GetAClass() : nullptr;
            const std::string name = args.text(1);
            if (a && !name.empty())
            {
                ATTRIBUTES *parent = a->GetParent();
                ATTRIBUTES *existing = parent ? parent->GetAttributeClass(name) : nullptr;
                if (!existing || existing == a)
                {
                    a->SetName(name);
                    ok = true;
                }
            }
        }
        return pushInt(call, result, ok ? 1 : 0);
    }
    case FUNC_X_APPEND_ATTRIBUTES: {
        Args args(call);
        DATA *dst = args.value(0);
        DATA *src = args.value(1);
        ATTRIBUTES *a = dst ? dst->GetAClass() : nullptr;
        ATTRIBUTES *b = src ? src->GetAClass() : nullptr;
        if (!a || !b)
            call.compiler.SetError("AppendAttributes: both arguments must be objects or attribute references");
        else if (a != b)
            appendAttributes(a, b);
        return nullptr;
    }
    case FUNC_X_RDTSC_B:
        return pushInt(call, result, ticks());
    case FUNC_X_RDTSC_E: {
        int32_t start;
        {
            Args args(call);
            start = args.integer(0);
        }
        return pushInt(call, result, static_cast<int32_t>(static_cast<uint32_t>(ticks()) - static_cast<uint32_t>(start)));
    }
    default:
        break;
    }
    call.compiler.SetError("internal function '%s' has no implementation", kNatives[index].name);
    {
        Args args(call);
    }
    return pushDefault(call, result, kNatives[index].return_type);
}
} // namespace storm::natives
