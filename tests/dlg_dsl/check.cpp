// Checks storm::dlg (src/libs/core/src/dlg_dsl.hpp) with a fake Host and invented strings.
//   check                                   unit checks
//   check --corpus <texts/<lang>> [--functions <file>]   DLG_Convert every key of a real text tree
#include "dlg_dsl.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace storm::dlg;

namespace
{
int failures = 0;

#define CHECK_EQ(actual, expected)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        const std::string a_ = (actual);                                                                               \
        const std::string e_ = (expected);                                                                             \
        if (a_ != e_)                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "%s:%d: got [%s] expected [%s]\n", __FILE__, __LINE__, a_.c_str(), e_.c_str());       \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

std::string kindName(ScriptArg::Kind kind)
{
    switch (kind)
    {
    case ScriptArg::Kind::Text:
        return "text";
    case ScriptArg::Kind::Player:
        return "@player";
    case ScriptArg::Kind::Npc:
        return "@npc";
    case ScriptArg::Kind::Context:
        return "@context";
    }
    return "?";
}

struct FakeHost final : Host
{
    std::map<std::string, std::map<std::string, std::string>> files; // file -> key -> value ("" = common)
    std::map<std::string, std::string> localisation;
    std::map<std::string, std::string> context;
    bool hasContext = true;
    std::map<Who, bool> female{{Who::Player, false}, {Who::Npc, true}, {Who::Context, false}};
    std::map<std::pair<Who, CharacterField>, std::string> fields;
    int tier = 0;
    std::map<std::string, std::function<std::string(std::span<const ScriptArg>)>> scripts;
    std::vector<std::string> scriptLog;
    std::map<std::string, std::string> constants;
    std::vector<int> randoms;
    size_t randomAt = 0;
    std::string lang = "english";
    std::vector<Diagnostic> reports;

    FakeHost()
    {
        fields[{Who::Player, CharacterField::FirstName}] = "Ana";
        fields[{Who::Player, CharacterField::LastName}] = "Quill";
        fields[{Who::Player, CharacterField::FullName}] = "Ana Quill";
        fields[{Who::Player, CharacterField::ShipName}] = "Gull";
        fields[{Who::Npc, CharacterField::FirstName}] = "Bo";
        fields[{Who::Npc, CharacterField::LastName}] = "Reed";
        fields[{Who::Npc, CharacterField::FullName}] = "Bo Reed";
        fields[{Who::Npc, CharacterField::ShipName}] = "Heron";
        fields[{Who::Npc, CharacterField::Town}] = "Port Nowhere";
    }

    std::optional<std::string> lookupKey(std::string_view file, std::string_view key) override
    {
        const auto f = files.find(std::string(file));
        if (f == files.end())
            return std::nullopt;
        const auto k = f->second.find(std::string(key));
        return k == f->second.end() ? std::nullopt : std::optional(k->second);
    }
    std::optional<std::string> lookupLocalisation(std::string_view key) override
    {
        const auto k = localisation.find(std::string(key));
        return k == localisation.end() ? std::nullopt : std::optional(k->second);
    }
    std::optional<std::string> contextAttribute(std::string_view path) override
    {
        if (!hasContext)
            return std::nullopt;
        const auto k = context.find(std::string(path));
        return k == context.end() ? std::nullopt : std::optional(k->second);
    }
    std::optional<bool> isFemale(Who who) override
    {
        const auto it = female.find(who);
        return it == female.end() ? std::nullopt : std::optional(it->second);
    }
    std::optional<std::string> characterField(Who who, CharacterField field) override
    {
        const auto it = fields.find({who, field});
        return it == fields.end() ? std::nullopt : std::optional(it->second);
    }
    int reputationTier() override
    {
        return tier;
    }
    std::optional<std::string> callScript(std::string_view function, std::span<const ScriptArg> args) override
    {
        std::string entry(function);
        for (const auto &arg : args)
            entry += "|" + (arg.kind == ScriptArg::Kind::Text ? arg.text : kindName(arg.kind));
        scriptLog.push_back(entry);
        const auto it = scripts.find(std::string(function));
        if (it == scripts.end())
            return std::nullopt;
        return it->second(args);
    }
    std::optional<std::string> scriptConstant(std::string_view name) override
    {
        const auto it = constants.find(std::string(name));
        return it == constants.end() ? std::nullopt : std::optional(it->second);
    }
    int randomInt(int bound) override
    {
        CHECK(bound >= 1);
        const int value = randoms.empty() ? 0 : randoms[randomAt++ % randoms.size()];
        return value % bound;
    }
    std::string_view language() override
    {
        return lang;
    }
    std::string markup(Markup m) override
    {
        switch (m)
        {
        case Markup::LineBreak:
            return "<BR>";
        case Markup::DialogBreak:
            return "<PAGE>";
        case Markup::Indent:
            return "<IND>";
        }
        return "?";
    }
    void report(const Diagnostic &diagnostic) override
    {
        reports.push_back(diagnostic);
    }

    size_t reportsOf(Diagnostic::Kind kind, std::string_view name) const
    {
        size_t n = 0;
        for (const auto &r : reports)
            n += r.kind == kind && r.name == name;
        return n;
    }
};

std::string echo(std::string_view name, std::span<const ScriptArg> args)
{
    std::string result = "<" + std::string(name);
    for (const auto &arg : args)
        result += ":" + (arg.kind == ScriptArg::Kind::Text ? arg.text : kindName(arg.kind));
    return result + ">";
}

void checkLiteralsAndEscapes()
{
    FakeHost host;
    Expander x(host);
    CHECK_EQ(x.expand("plain text, no markup"), "plain text, no markup");
    CHECK_EQ(x.expand("cost: 5$ and $} and $9"), "cost: 5$ and $} and $9");
    CHECK_EQ(x.expand("a \\(b\\) c"), "a (b) c");
    CHECK_EQ(x.expand("\\$br() stays"), "$br() stays");
    CHECK_EQ(x.expand("keep \\n here"), "keep \\n here");
    CHECK_EQ(x.expand("under\\_score"), "under_score");
    CHECK_EQ(x.expand("double \\\\\\_ x"), "double \\_ x");
    CHECK_EQ(x.expand("arrow \\\xE2\x86\xB4X"), "arrow \xE2\x86\xB4X");
    CHECK_EQ(x.expand("tail\\"), "tail\\");
    CHECK_EQ(x.expand("@<expr> and #token# pass"), "@<expr> and #token# pass");
    CHECK(host.reports.empty());
}

void checkVariables()
{
    FakeHost host;
    Expander x(host);
    CHECK_EQ(x.expand("$name/$Name/$lastName/$fullName/$shipName"), "Ana/Ana/Quill/Ana Quill/Gull");
    CHECK_EQ(x.expand("$npcName $npcLastName, $npcFullName of $npcShipName in $city."),
             "Bo Reed, Bo Reed of Heron in Port Nowhere.");
    CHECK_EQ(x.expand("$name... $name.. $name, $name!"), "Ana... Ana.. Ana, Ana!");
    CHECK_EQ(x.expand("$name()x"), "Anax");
    CHECK_EQ(x.expand("$fullName\xC3\xA0 town"), "Ana Quill\xC3\xA0 town"); // ends at non-ASCII
    // An identifier glued to following letters is a different (unknown) name.
    CHECK_EQ(x.expand("$nameZed hi"), "$nameZed hi");
    CHECK(host.reportsOf(Diagnostic::Kind::UnknownName, "nameZed") == 1u);
    CHECK_EQ(x.expand("again $nameZed"), "again $nameZed");
    CHECK(host.reportsOf(Diagnostic::Kind::UnknownName, "nameZed") == 1u); // reported once
    // Variables take no arguments.
    CHECK_EQ(x.expand("$name(x)"), "$name(x)");
    CHECK(host.reportsOf(Diagnostic::Kind::BadArgument, "name") == 1u);
    // No dialogue partner.
    host.fields.erase({Who::Npc, CharacterField::FirstName});
    CHECK_EQ(x.expand("hi $npcName!"), "hi $npcName!");
    CHECK(host.reportsOf(Diagnostic::Kind::MissingCharacter, "npcName") == 1u);
}

void checkScriptVariables()
{
    FakeHost host;
    Expander x(host);
    for (const char *f : {"GetAddress_Form", "GetAddress_FormToNPC", "RandSwear", "TimeGreeting", "Goodbye"})
        host.scripts[f] = [f](std::span<const ScriptArg> a) { return echo(f, a); };
    CHECK_EQ(x.expand("$sir|$npcSir|$swear|$swear()|$hello|$bye"),
             "<GetAddress_Form:@npc>|<GetAddress_FormToNPC:@npc>|<RandSwear>|<RandSwear>|<TimeGreeting>|<Goodbye:1>");
    host.scripts.erase("RandSwear");
    CHECK_EQ(x.expand("$swear go"), "$swear go");
    CHECK(host.reportsOf(Diagnostic::Kind::UnknownName, "swear") == 1u);
}

void checkMarkup()
{
    FakeHost host;
    Expander x(host);
    CHECK_EQ(x.expand("a$br()b$br c$dialogBreak()d $red e"), "a<BR>b<BR> c<PAGE>d <IND> e");
    CHECK(host.reports.empty());
}

void checkContext()
{
    FakeHost host;
    Expander x(host);
    host.context = {{"count", "7"},       {"deep.path", "x"},          {"nested", "$ctx(count) items"},
                    {"loop", "$ctx(loop)"}, {"price", "$100 \\| raw"}, {"good", "Rum"}};
    CHECK_EQ(x.expand("$ctx(count)/$ctx(deep.path)/$ctx(nested)"), "7/x/7 items");
    CHECK_EQ(x.expand("[$ctx(missing)]"), "[$ctx(missing)]");
    CHECK(host.reportsOf(Diagnostic::Kind::MissingContext, "ctx") == 1u);
    CHECK_EQ(x.expand("$ctx(loop)").find("$ctx(loop)") != std::string::npos ? "kept" : "lost", "kept");
    CHECK(host.reportsOf(Diagnostic::Kind::RecursionLimit, "ctx") == 1u);
    // $attr reads the attribute raw: no template expansion.
    CHECK_EQ(x.expand("$attr($context.price)"), "$100 \\| raw");
    CHECK_EQ(x.expand("$attr(count)"), "$attr(count)");
    CHECK(host.reportsOf(Diagnostic::Kind::BadArgument, "attr") == 1u);
    CHECK_EQ(x.expand("$ctxHasAttr(count|yes $ctx(count)|no) $ctxHasAttr(nope|yes|no) [$ctxHasAttr(nope|yes)]"),
             "yes 7 no []");
    host.hasContext = false;
    CHECK_EQ(x.expand("$ctxHasAttr(count|yes|no)"), "no");
}

void checkChoices()
{
    FakeHost host;
    Expander x(host);
    host.randoms = {2, 0};
    CHECK_EQ(x.expand("$rand(one|two|three)"), "three");
    CHECK_EQ(x.expand("$rand(first $unknownA|second $unknownB)"), "first $unknownA");
    CHECK(host.reportsOf(Diagnostic::Kind::UnknownName, "unknownB") == 0u); // unchosen branch not expanded
    CHECK_EQ(x.expand("$rand(only)"), "only");
    host.randoms = {1};
    CHECK_EQ(x.expand("$rand(a \\| b|c \\) d)"), "c ) d");
    CHECK_EQ(x.expand("$rand(x $rand(p|q) y|z)"), "z");
    CHECK_EQ(x.expand("$rand(x $rand(p|q) y|z)"), "z");
    host.randoms = {0, 1};
    CHECK_EQ(x.expand("$rand(x $rand(p|q) y|z)"), "x q y");

    CHECK_EQ(x.expand("$manWoman(he|she) $npcManWoman(he|she) $ctxManWoman(he|she)"), "he she he");
    host.female.erase(Who::Context);
    CHECK_EQ(x.expand("$ctxManWoman(he|she)"), "$ctxManWoman(he|she)");
    CHECK(host.reportsOf(Diagnostic::Kind::MissingCharacter, "ctxManWoman") == 1u);
    CHECK_EQ(x.expand("$manWoman(only)"), "$manWoman(only)");
    CHECK(host.reportsOf(Diagnostic::Kind::BadArgument, "manWoman") == 1u);
    CHECK_EQ(x.expand("$manWoman(|a)"), "");

    for (int tier = 0; tier < 3; ++tier)
    {
        host.tier = tier;
        const char *expected[] = {"kind", "plain", "rude"};
        CHECK_EQ(x.expand("$goodBad(kind|plain|rude)"), expected[tier]);
    }
    CHECK_EQ(x.expand("$goodBad(a|b)"), "$goodBad(a|b)");
}

void checkKeys()
{
    FakeHost host;
    Expander x(host);
    host.files[""] = {{"Sky", "blue"}, {"OakGen", "of the oak"}, {"Wrap", "<$key(Sky)>"}};
    host.files["Trees.txt"] = {{"leaf", "green $key(Sky)"}};
    host.context = {{"tree", "Oak"}};
    CHECK_EQ(x.expand("$key(Sky) $key($ctx(tree)Gen) $key(Wrap) $key(leaf|Trees.txt)"),
             "blue of the oak <blue> green blue");
    CHECK_EQ(x.expand("$key(Nope)"), "$key(Nope)");
    CHECK(host.reportsOf(Diagnostic::Kind::MissingKey, "key") == 1u);
    host.localisation = {{"Mood_3", "Cheerful $key(Sky)"}};
    CHECK_EQ(x.expand("[$link(Mood_3)]"), "[Cheerful blue]");
    CHECK_EQ(x.expand("$link(Mood_9)"), "$link(Mood_9)");
}

void checkNumbers()
{
    FakeHost host;
    Expander x(host);
    host.constants = {{"SPEED_BONUS", "0.15"}, {"WEIGHT", "20"}};
    CHECK_EQ(x.expand("$value(WEIGHT) $percent($value(SPEED_BONUS))"), "20 15%");
    CHECK_EQ(x.expand("$percent(0.3764) $percent(-0.05) $percent(1) $percent(0.105)"), "37.64% -5% 100% 10.5%");
    host.lang = "russian";
    CHECK_EQ(x.expand("$percent(0.2)"), "20 %");
    host.lang = "turkish";
    CHECK_EQ(x.expand("$percent(0.2)"), "%20");
    host.lang = "english";
    CHECK_EQ(x.expand("$number(20.00) $number(20.5) $number(20.761) $number( 3 ) $number(1e2)"), "20 20.5 20.76 3 100");
    CHECK_EQ(x.expand("$number(lots)"), "$number(lots)");
    CHECK(host.reportsOf(Diagnostic::Kind::BadArgument, "number") == 1u);
    CHECK_EQ(x.expand("$value(NOPE)"), "$value(NOPE)");
    host.scripts["FormatMoney"] = [](std::span<const ScriptArg> a) { return "M[" + a[0].text + "]"; };
    CHECK_EQ(x.expand("$money(1500) $abs(-12) $abs(7) $abs($number(-2.5))"), "M[1500] 12 7 2.5");
    host.files[""] = {{"day2", "day"}, {"rdays2", "dayz"}, {"rdays", "days"}};
    CHECK_EQ(x.expand("$days(1) $days(3) $days(5) $days(11) $days(21)"), "1 day 3 dayz 5 days 11 days 21 day");
    CHECK_EQ(x.expand("$decl(1|cat|cats|catz) $decl(2|cat|cats|catz) $decl(12|cat|cats|catz) $decl(5|cat|cats)"),
             "1 cat 2 cats 12 catz 5 cats");
    CHECK_EQ(x.expand("$decl($number(22)|a|b|c)"), "22 b");
}

void checkScriptBackedBuiltins()
{
    FakeHost host;
    Expander x(host);
    for (const char *f : {"GetNationNameByType", "DLG_GoodName", "DLG_HumanModifierValuePercent"})
        host.scripts[f] = [f](std::span<const ScriptArg> a) { return echo(f, a); };
    host.files[""] = {{"<GetNationNameByType:2>Adj", "Southern"}};
    CHECK_EQ(x.expand("$key($nationI(2)Adj) $goodI(4) $modifierPercent(Grip)"),
             "Southern <DLG_GoodName:4> <DLG_HumanModifierValuePercent:Grip:@context>");
}

void checkScriptFallback()
{
    FakeHost host;
    Expander x(host);
    host.scripts["Exact"] = [](std::span<const ScriptArg> a) { return echo("Exact", a); };
    host.scripts["DLG_Region"] = [](std::span<const ScriptArg> a) { return echo("DLG_Region", a); };
    host.scripts["RT_Marker"] = [](std::span<const ScriptArg> a) { return "[b]" + a[0].text + "[/b]"; };
    host.scripts["Shout"] = [](std::span<const ScriptArg> a) { return echo("Shout", a); };
    host.scripts["Raw"] = [](std::span<const ScriptArg>) { return std::string("$name stays"); };
    CHECK_EQ(x.expand("$Exact(a|b c) $region(5) $marker(Lake $name) $shout() $Exact( )"),
             "<Exact:a:b c> <DLG_Region:5> [b]Lake Ana[/b] <Shout> <Exact: >");
    CHECK_EQ(x.expand("$Raw()"), "$name stays"); // script results are not re-expanded
    // Probe order: name, DLG_Name, RT_Name, Name.
    host.scriptLog.clear();
    CHECK_EQ(x.expand("$mystery(q $rand(r)) tail"), "$mystery(q $rand(r)) tail");
    CHECK(host.scriptLog == (std::vector<std::string>{"mystery|q r", "DLG_Mystery|q r", "RT_Mystery|q r", "Mystery|q r"}));
    CHECK(host.reportsOf(Diagnostic::Kind::UnknownName, "mystery") == 1u);
    // Built-ins take precedence over a script function of the same name.
    host.scripts["rand"] = [](std::span<const ScriptArg>) { return std::string("SCRIPT"); };
    CHECK_EQ(x.expand("$rand(x)"), "x");
}

void checkUnterminated()
{
    FakeHost host;
    Expander x(host);
    CHECK_EQ(x.expand("ok $rand(a|b no end"), "ok $rand(a|b no end");
    CHECK(host.reportsOf(Diagnostic::Kind::Unterminated, "rand") == 1u);
    CHECK_EQ(x.expand("$rand(a $key(x) b"), "$rand(a $key(x) b");
}

void checkLowerMarker()
{
    FakeHost host;
    Expander x(host);
    host.files[""] = {{"Ship", "Brig"}, {"Colored", "[color=1,2,3]Galleon[/color]"}};
    const std::string m = "\xE2\x86\xB4";
    CHECK_EQ(x.expand("a " + m + "$key(Ship) and " + m + "$key(Colored)"), "a brig and [color=1,2,3]galleon[/color]");
    CHECK_EQ(x.expand(m + "Tall " + m + "\xC3\x96l " + m + "\xD0\x96uk " + m + "\xC5\x81" "ad " + m + "\xC4\xB0z"),
             "tall \xC3\xB6l \xD0\xB6uk \xC5\x82" "ad iz");
    CHECK_EQ(x.expand(m + "42 Ships"), "42 Ships");
    CHECK_EQ(x.expand(m + " $name"), " ana");
    CHECK_EQ(x.expand("end" + m), "end");
}

void checkConvertAndStringFromKey()
{
    FakeHost host;
    Expander x(host);
    host.files["Notes\\Diary.txt"] = {{"entry", "Dear $name"},
                                       {"local", "page two"},
                                       {"Mate", "$key(Mate)"},
                                       {"refs", "$key(local), $key(Mate), $key(remote|Other.txt)"}};
    host.files["Other.txt"] = {{"remote", "far $key(near)"}, {"near", "away"}};
    host.files[""] = {{"Mate", "first mate"}};
    // $key(k) without a file: common strings first, then the file being converted.
    CHECK_EQ(x.convert("Notes\\Diary.txt", "refs"), "page two, first mate, far away");
    CHECK_EQ(x.expand("$key(local)"), "$key(local)"); // DLGO has no current file
    CHECK_EQ(x.convert("Notes\\Diary.txt", "entry"), "Dear Ana");
    CHECK_EQ(x.convert("Notes\\Diary.txt", "nothing"), "nothing");
    CHECK(host.reportsOf(Diagnostic::Kind::MissingKey, "Notes\\Diary.txt:nothing") == 1u);
    const size_t before = host.reports.size();
    CHECK_EQ(x.convertOrEmpty("Notes\\Diary.txt", "nothing"), "");
    CHECK_EQ(x.convertOrEmpty("Notes\\Diary.txt", "entry"), "Dear Ana");
    CHECK(host.reports.size() == before);

    host.localisation = {{"Trade_1", "Sold @<qty> of @<GetName(item)> for @<price>"}, {"Plain_2", "Hello $name"}};
    const std::vector<std::string> three{"3", "salt", "9"};
    CHECK_EQ(x.stringFromKey("Trade_1", three), "Sold 3 of salt for 9");
    const std::vector<std::string> one{"3"};
    CHECK_EQ(x.stringFromKey("Trade_1", one), "Sold 3 of @<GetName(item)> for @<price>");
    CHECK(host.reportsOf(Diagnostic::Kind::Placeholder, "Trade_1") == 1u);
    CHECK_EQ(x.stringFromKey("Plain_2", one), "Hello $name"); // not template-expanded
    CHECK(host.reportsOf(Diagnostic::Kind::Placeholder, "Plain_2") == 1u);
    CHECK_EQ(x.stringFromKey("Absent_1", {}), "Absent_1");
}

void checkReplaceAllMatches()
{
    std::string s = "1-2-3-4";
    replaceAllMatches(s, "-", "+");
    CHECK_EQ(s, "1+2+3+4");
    s = "1-2-3-4";
    replaceAllMatches(s, "-", "--", 3);
    CHECK_EQ(s, "1-2--3--4");
    s = "aaa";
    replaceAllMatches(s, "a", "aa");
    CHECK_EQ(s, "aaaaaa");
    s = "abc";
    replaceAllMatches(s, "", "x");
    replaceAllMatches(s, "c", "x", 10);
    CHECK_EQ(s, "abc");
    s = "<p>one<p>";
    replaceAllMatches(s, "<p>", "");
    CHECK_EQ(s, "one");
}

void checkTextTable()
{
    const auto table = TextTable::parse("\xEF\xBB\xBFkey_one {first value}\r\n"
                                        "Key_Two {multi\r\nline}\r\n"
                                        "stray words\r\n"
                                        "spaced key  \r\n{\r\n\r\n}\r\n"
                                        "KEY_ONE {duplicate}\r\n"
                                        "empty {}\r\n"
                                        "{orphan}\r\n"
                                        "broken {no end");
    CHECK_EQ(std::string(table.find("key_one").value_or("?")), "first value");
    CHECK_EQ(std::string(table.find("KEY_one").value_or("?")), "first value"); // case-insensitive, first kept
    CHECK_EQ(std::string(table.find("key_two").value_or("?")), "multi\r\nline");
    CHECK_EQ(std::string(table.find("spaced key").value_or("?")), "\r\n\r\n");
    CHECK_EQ(std::string(table.find("empty").value_or("?")), "");
    CHECK(!table.find("broken"));
    CHECK(table.size() == 4);
    std::set<std::string> problems;
    for (const auto &p : table.problems())
        problems.insert(std::to_string(p.line) + ":" + p.what);
    CHECK(problems.count("4:text between values ignored") == 1);
    CHECK(problems.count("9:duplicate key 'KEY_ONE' (first kept)") == 1);
    CHECK(problems.count("11:value without key") == 1);
    CHECK(problems.count("12:value without closing '}'") == 1);
    CHECK(problems.size() == 4);
    // Short content: views must survive moves of the table.
    std::optional<TextTable> moved = TextTable::parse("a {b}");
    TextTable again = std::move(*moved);
    CHECK_EQ(std::string(again.find("A").value_or("?")), "b");
}

void checkTextCache()
{
    std::map<std::string, std::string> disk{{"one.txt", "k {1111111111}"}, {"two.txt", "k {2222222222}"},
                                            {"Dir/three.txt", "k {3}"}};
    std::map<std::string, int> reads;
    TextCache cache(
        [&](std::string_view path) -> std::optional<std::string> {
            ++reads[std::string(path)];
            const auto it = disk.find(std::string(path));
            return it == disk.end() ? std::nullopt : std::optional(it->second);
        },
        150);
    CHECK_EQ(std::string(cache.file("one.txt")->find("k").value_or("?")), "1111111111");
    CHECK_EQ(std::string(cache.file("ONE.TXT")->find("k").value_or("?")), "1111111111"); // normalised hit
    CHECK(reads["one.txt"] == 1);
    CHECK(cache.file("Dir\\three.txt") == nullptr); // reader is asked with the caller's spelling
    CHECK(cache.file("missing.txt") == nullptr);
    CHECK(cache.file("missing.txt") == nullptr);
    CHECK(reads["missing.txt"] == 1); // misses are cached
    cache.file("two.txt");             // budget 150: evicts the least recently used entries
    cache.file("one.txt");
    CHECK(reads["one.txt"] == 2);
    CHECK_EQ(normaliseTextPath("Dialogs\\Some_File.TXT"), "dialogs/some_file.txt");
}

// --- corpus -------------------------------------------------------------------------------

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// common.ini: `string = Key, "value"` lines (the engine reads it through its string service).
std::map<std::string, std::string, std::less<>> parseCommonIni(const std::string &content)
{
    std::map<std::string, std::string, std::less<>> result;
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line))
    {
        const size_t eq = line.find('=');
        const size_t comma = line.find(',', eq == std::string::npos ? 0 : eq);
        const size_t q1 = line.find('"', comma == std::string::npos ? 0 : comma);
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (eq == std::string::npos || comma == std::string::npos || q2 == std::string::npos ||
            line.compare(0, eq, "string ") != 0)
            continue;
        std::string key = line.substr(eq + 1, comma - eq - 1);
        key.erase(0, key.find_first_not_of(' '));
        key.erase(key.find_last_not_of(' ') + 1);
        std::string lower;
        for (char c : key)
            lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        result.emplace(lower, line.substr(q1 + 1, q2 - q1 - 1));
    }
    return result;
}

struct CorpusHost final : Host
{
    std::filesystem::path root;
    std::map<std::string, std::filesystem::path> paths; // normalised relative path -> file
    TextCache cache;
    std::map<std::string, std::string, std::less<>> common;
    std::map<std::string, std::string, std::less<>> localisation;
    std::set<std::string, std::less<>> functions;
    std::string lang;
    uint32_t seed = 1;
    std::map<std::string, std::pair<size_t, std::string>> issues; // "kind: name" -> count, first location
    std::string where;
    size_t fakeHostIssues = 0;

    explicit CorpusHost(std::filesystem::path dir)
        : root(std::move(dir)), cache([this](std::string_view p) -> std::optional<std::string> {
              const auto it = paths.find(normaliseTextPath(p));
              if (it == paths.end())
                  return std::nullopt;
              return readFile(it->second);
          },
                                      8u << 20)
    {
    }

    static std::string lower(std::string_view s)
    {
        std::string r(s);
        for (auto &c : r)
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return r;
    }

    std::optional<std::string> lookupKey(std::string_view file, std::string_view key) override
    {
        if (file.empty() || normaliseTextPath(file) == "common.ini")
        {
            const auto it = common.find(lower(key));
            return it == common.end() ? std::nullopt : std::optional(it->second);
        }
        const auto *table = cache.file(file);
        if (!table)
            return std::nullopt;
        const auto value = table->find(key);
        return value ? std::optional(std::string(*value)) : std::nullopt;
    }
    std::optional<std::string> lookupLocalisation(std::string_view key) override
    {
        const auto it = localisation.find(lower(key));
        return it == localisation.end() ? std::nullopt : std::optional(it->second);
    }
    std::optional<std::string> contextAttribute(std::string_view path) override
    {
        return "ctx." + std::string(path);
    }
    std::optional<bool> isFemale(Who) override
    {
        return (next() & 1) != 0;
    }
    std::optional<std::string> characterField(Who, CharacterField) override
    {
        return std::string("Someone");
    }
    int reputationTier() override
    {
        return static_cast<int>(next() % 3);
    }
    std::optional<std::string> callScript(std::string_view function, std::span<const ScriptArg>) override
    {
        if (!functions.count(function))
            return std::nullopt;
        return "{" + std::string(function) + "}";
    }
    std::optional<std::string> scriptConstant(std::string_view) override
    {
        return std::string("0.25");
    }
    int randomInt(int bound) override
    {
        return static_cast<int>(next() % static_cast<uint32_t>(bound));
    }
    std::string_view language() override
    {
        return lang;
    }
    std::string markup(Markup) override
    {
        return "\n";
    }
    void report(const Diagnostic &d) override
    {
        // Values this fake host invents ("ctx.<path>", "{Function}") are not numbers or keys;
        // issues they cause say nothing about the data.
        if (d.detail.find("ctx.") != std::string::npos || d.detail.find('{') != std::string::npos)
        {
            ++fakeHostIssues;
            return;
        }
        auto &entry = issues[std::string(diagnosticKindName(d.kind)) + ": " + d.name];
        if (entry.first++ == 0)
            entry.second = where + "  " + d.detail.substr(0, 160);
    }
    uint32_t next()
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    }
};

int runCorpus(const std::filesystem::path &dir, const std::filesystem::path &functionsFile)
{
    namespace fs = std::filesystem;
    CorpusHost host(dir);
    host.lang = CorpusHost::lower(dir.filename().string());
    if (!functionsFile.empty())
    {
        std::ifstream in(functionsFile);
        for (std::string line; std::getline(in, line);)
            if (!line.empty())
                host.functions.insert(line);
    }
    std::vector<fs::path> files;
    for (const auto &entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file())
        {
            const std::string rel = fs::relative(entry.path(), dir).generic_string();
            host.paths[normaliseTextPath(rel)] = entry.path();
            if (CorpusHost::lower(entry.path().extension().string()) == ".txt")
                files.push_back(entry.path());
        }
    std::sort(files.begin(), files.end());
    if (const auto it = host.paths.find("common.ini"); it != host.paths.end())
        host.common = parseCommonIni(readFile(it->second));

    const auto parseStart = std::chrono::steady_clock::now();
    std::map<std::string, size_t> problems;
    std::map<std::string, std::string> problemExample;
    std::vector<std::pair<std::string, TextTable>> tables;
    size_t bytes = 0;
    for (const auto &file : files)
    {
        const std::string rel = fs::relative(file, dir).generic_string();
        auto table = TextTable::parse(readFile(file));
        bytes += table.bytes();
        for (const auto &p : table.problems())
        {
            const std::string kind = p.what.rfind("duplicate key", 0) == 0 ? "duplicate key" : p.what;
            if (problems[kind]++ == 0)
                problemExample[kind] = rel + ":" + std::to_string(p.line);
        }
        if (normaliseTextPath(rel).rfind("localization_assets/", 0) == 0)
            for (const auto key : table.keys())
                host.localisation.emplace(CorpusHost::lower(key), std::string(*table.find(key)));
        tables.emplace_back(rel, std::move(table));
    }
    const double parseMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - parseStart).count();

    size_t values = 0, outputBytes = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto &[rel, table] : tables)
        for (const auto key : table.keys())
        {
            host.where = rel + ":" + std::string(key);
            Expander expander(host); // fresh per value: issue counts are per value
            outputBytes += expander.convert(rel, key).size(); // as DLG_Convert(key, file)
            ++values;
        }
    const double expandMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    std::printf("corpus %s: %zu files, %.1f MB, %zu values, parse %.0f ms, expand %.0f ms (%.2f us/value), "
                "%zu script functions known\n",
                dir.string().c_str(), files.size(), bytes / 1048576.0, values, parseMs, expandMs,
                values ? expandMs * 1000 / static_cast<double>(values) : 0.0, host.functions.size());
    std::printf("file problems:\n");
    for (const auto &[kind, count] : problems)
        std::printf("  %6zu  %s  (first: %s)\n", count, kind.c_str(), problemExample[kind].c_str());
    std::vector<std::pair<size_t, std::string>> sorted;
    for (const auto &[name, entry] : host.issues)
        sorted.emplace_back(entry.first, name);
    std::sort(sorted.rbegin(), sorted.rend());
    std::map<std::string, size_t> perKind;
    for (const auto &[count, name] : sorted)
        perKind[name.substr(0, name.find(':'))] += count;
    std::printf("expansion issues (values affected):\n");
    for (const auto &[kind, count] : perKind)
        std::printf("  %6zu  %s\n", count, kind.c_str());
    std::printf("  %6zu  caused by the fake host's invented context/script values (not listed)\n",
                host.fakeHostIssues);
    std::printf("distinct issues: %zu\n", sorted.size());
    for (const auto &[count, name] : sorted)
        if (name.rfind("missing key", 0) != 0 || count > 1)
            std::printf("  %6zu  %s\n          first: %s\n", count, name.c_str(), host.issues[name].second.c_str());
    (void)outputBytes;
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 3 && std::string_view(argv[1]) == "--corpus")
    {
        std::filesystem::path functions;
        if (argc >= 5 && std::string_view(argv[3]) == "--functions")
            functions = argv[4];
        return runCorpus(argv[2], functions);
    }
    const auto start = std::chrono::steady_clock::now();
    checkLiteralsAndEscapes();
    checkVariables();
    checkScriptVariables();
    checkMarkup();
    checkContext();
    checkChoices();
    checkKeys();
    checkNumbers();
    checkScriptBackedBuiltins();
    checkScriptFallback();
    checkUnterminated();
    checkLowerMarker();
    checkConvertAndStringFromKey();
    checkReplaceAllMatches();
    checkTextTable();
    checkTextCache();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (failures)
    {
        std::fprintf(stderr, "dlg_dsl: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("dlg_dsl: all checks passed (%.1f ms)\n", ms);
    return 0;
}
