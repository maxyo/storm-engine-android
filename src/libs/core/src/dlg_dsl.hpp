#pragma once

// Caribbean Legend localisation template language ("dialog DSL").
//
// CL text values (Resource/INI/texts/<language>/**/*.txt, `key {value}`) contain template
// constructs that the script natives DLGO, DLG_Convert, DLG_ConvertE and StringFromKey expand:
//
//   $name            bare variable          ($fullName, $npcSir, $swear, $red ...)
//   $name(a|b|...)   call, `|` separates arguments, calls nest ($key($ctx(good)Gen))
//   \x               the character x literally (`\|`, `\)`, `\$`, `\_`); `\n` stays as is
//   U+21B4 (↴)       lower-case the first letter of what follows
//
// A call resolves to a built-in (case-insensitive) or else to the first existing script
// function among `name`, `DLG_Name`, `RT_Name`, `Name` (Name = first letter upper-cased).
// Anything unknown stays verbatim in the output and is reported once through Host::report.
// The full grammar and the evidence for each rule: local-testing/cl20/specs/dlg-dsl.md.
//
// Clean-room code: no engine dependency; the VM glue implements Host.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storm::dlg
{

// Whose property a construct reads: the player (pchar), the dialogue partner (npchar) or the
// context object passed to DLGO / DLG_Convert.
enum class Who
{
    Player,
    Npc,
    Context,
};

enum class CharacterField
{
    FirstName, // $name, $npcName
    LastName,  // $lastName, $npcLastName
    FullName,  // $fullName, $npcFullName
    ShipName,  // $shipName, $npcShipName
    Town,      // $city: display name of the dialogue partner's town
};

// Markup whose exact output belongs to the text renderer, not to the template language.
enum class Markup
{
    LineBreak,   // $br, $br()
    DialogBreak, // $dialogBreak(): page break inside one dialogue line
    Indent,      // $red: paragraph indent ("red line")
};

// One argument of a script call. Text arguments are passed as strings (the VM converts
// them to the parameter type); the other kinds pass a reference to that object.
struct ScriptArg
{
    enum class Kind
    {
        Text,
        Player,
        Npc,
        Context,
    };
    Kind kind = Kind::Text;
    std::string text;
};

struct Diagnostic
{
    enum class Kind
    {
        UnknownName,      // no built-in and no script function for a `$name`
        Unterminated,     // `$name(` without its `)`
        MissingKey,       // $key / $link / DLG_Convert / StringFromKey key not found
        MissingContext,   // $ctx / $attr / $ctxHasAttr without context or attribute
        MissingCharacter, // character property unavailable (no NPC, no context character)
        BadArgument,      // wrong argument count or a non-numeric number
        RecursionLimit,   // nested $key / $ctx expansion deeper than kMaxDepth
        Placeholder,      // StringFromKey: @<...> without argument, or unused arguments
    };
    Kind kind;
    std::string name;   // construct name, key or attribute path
    std::string detail; // human-readable explanation
};

std::string_view diagnosticKindName(Diagnostic::Kind kind);

class Host
{
  public:
    virtual ~Host() = default;

    // Raw value of `key` in text file `file` (path relative to the language directory as the
    // scripts spell it, `\` or `/`, any case). An empty `file` means the common strings
    // (common.ini, what XI_ConvertString reads). nullopt when the file or key is missing.
    virtual std::optional<std::string> lookupKey(std::string_view file, std::string_view key) = 0;

    // Raw value of a StringFromKey key (Resource/INI/texts/<language>/Localization_Assets/**).
    virtual std::optional<std::string> lookupLocalisation(std::string_view key) = 0;

    // Attribute of the context object by dotted path ("ct.equip.hpMax.hat"); nullopt when
    // there is no context or the attribute does not exist.
    virtual std::optional<std::string> contextAttribute(std::string_view path) = 0;

    // nullopt when that character is not available (no dialogue partner, context is not a
    // character).
    virtual std::optional<bool> isFemale(Who who) = 0;
    virtual std::optional<std::string> characterField(Who who, CharacterField field) = 0;

    // Player alignment for $goodBad(good|neutral|bad): 0, 1 or 2.
    virtual int reputationTier() = 0;

    // Calls script function `function`; nullopt when no such function exists. Numeric and
    // boolean results are returned in their script string form.
    virtual std::optional<std::string> callScript(std::string_view function, std::span<const ScriptArg> args) = 0;

    // Value of a script `#define` constant ($value(NAME)); nullopt when undefined.
    virtual std::optional<std::string> scriptConstant(std::string_view name) = 0;

    // Uniform integer in [0, bound), bound >= 1.
    virtual int randomInt(int bound) = 0;

    // Current language directory name in lower case ("english", "russian", ...).
    virtual std::string_view language() = 0;

    virtual std::string markup(Markup markup) = 0;

    // Called once per distinct (kind, name) for the lifetime of the Expander.
    virtual void report(const Diagnostic &diagnostic) = 0;
};

class Expander
{
  public:
    static constexpr int kMaxDepth = 16;

    explicit Expander(Host &host);

    // DLGO(sInput, context): expand a template.
    std::string expand(std::string_view text);

    // DLG_Convert(sKey, sFilename, context): look the key up and expand it. A missing key
    // yields the key itself and is reported. Inside, $key(k) without a file looks in the
    // common strings first, then in sFilename.
    std::string convert(std::string_view file, std::string_view key);

    // DLG_ConvertE: like convert, but a missing key yields "" without a report.
    std::string convertOrEmpty(std::string_view file, std::string_view key);

    // StringFromKey(key, args...): the localisation value with its `@<...>` placeholders
    // replaced by `args` in order. Not template-expanded (scripts wrap it in DLGO).
    std::string stringFromKey(std::string_view key, std::span<const std::string> args);

  private:
    struct Call;

    void expandInto(std::string &out, std::string_view text, int depth);
    std::string expandArg(std::string_view text, int depth);
    // Parses `$name` / `$name(...)` at text[pos] == '$'; returns the end position.
    size_t expandConstruct(std::string &out, std::string_view text, size_t pos, int depth);
    bool builtin(std::string &out, const Call &call, int depth);
    bool scriptFallback(std::string &out, const Call &call, int depth);
    void unknown(std::string &out, const Call &call);
    void report(Diagnostic::Kind kind, std::string_view name, std::string detail);
    std::string expandValue(std::string_view value, std::string_view name, int depth);

    // Expands `value` with `file` as the current text file (the fallback of $key(k)).
    std::string expandInFile(std::string_view value, std::string_view file, int depth);

    Host &host_;
    std::unordered_set<std::string> reported_;
    std::string currentFile_; // file of the DLG_Convert / $key(k|file) value being expanded
};

// DLG_ReplaceAllMatches(ref input, key, replace, curPos): replace every occurrence of `key`
// at or after byte offset `from`; replacements are not rescanned. Empty key: no change.
void replaceAllMatches(std::string &input, std::string_view key, std::string_view replacement, size_t from = 0);

// Lower-cases the first letter at or after `from`, skipping spaces and leading `[tag]`
// markup. Returns false when no letter or digit was found (nothing changed).
bool lowerFirstLetter(std::string &text, size_t from);

// --- text files -------------------------------------------------------------------------

// ASCII case-insensitive hash / equality for keys (keys are ASCII identifiers in practice;
// other bytes compare exactly).
struct KeyHash
{
    size_t operator()(std::string_view key) const noexcept;
};
struct KeyEqual
{
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

// A parsed `key {value}` file. The key is the last non-blank line before `{`, trimmed; the
// value is everything up to the next `}` (raw: no trimming, line breaks kept). The first of
// duplicate keys wins (as the TEHO string service does).
class TextTable
{
  public:
    struct Problem
    {
        size_t line;
        std::string what;
    };

    static TextTable parse(std::string content);

    std::optional<std::string_view> find(std::string_view key) const;
    size_t size() const
    {
        return index_.size();
    }
    size_t bytes() const
    {
        return content_->size();
    }
    const std::vector<Problem> &problems() const
    {
        return problems_;
    }
    // Keys in file order (duplicates excluded).
    const std::vector<std::string_view> &keys() const
    {
        return order_;
    }

  private:
    std::unique_ptr<const std::string> content_; // stable address: the views point into it
    std::unordered_map<std::string_view, std::string_view, KeyHash, KeyEqual> index_;
    std::vector<std::string_view> order_;
    std::vector<Problem> problems_;
};

// Least-recently-used cache of parsed text files under a byte budget. `read` returns the
// file content for a path relative to the language directory, nullopt when missing.
class TextCache
{
  public:
    using Reader = std::function<std::optional<std::string>(std::string_view relativePath)>;

    TextCache(Reader reader, size_t byteBudget);

    // nullptr when the file does not exist. The pointer stays valid until the next call.
    const TextTable *file(std::string_view relativePath);
    void clear();

  private:
    struct Entry
    {
        std::string path; // normalised: lower case, '/'
        std::optional<TextTable> table;
    };

    Reader reader_;
    size_t budget_;
    size_t bytes_ = 0;
    std::list<Entry> lru_;
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

// "Dialogs\\Roger.txt" -> "dialogs/roger.txt".
std::string normaliseTextPath(std::string_view path);

} // namespace storm::dlg
