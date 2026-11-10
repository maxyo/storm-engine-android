// The application's own screens, outside the game: the launcher (game files, import,
// play), the download library (github flavor) and about / licences. Mockups
// V2-Launcher-Setup, -Ready, -Folder, -Importing, -ImportError, -Downloads,
// -DownloadsEmpty, -AddDownload, -TorrentFiles, V2-Menu-About, -About-License.
//
// The platform host publishes the state channel "launcher":
//   {screen: launcher|downloads|about, language: <game language name>, <screen>: {...}}
// with the raw data of that screen; every text the player reads is formatted here from the
// pack's string table.
//   launcher:  {phase: setup|ready|busy, files, library, hasGame, problem: ""|no_game|
//               recovery_required|start_failed, result: {key, args}, exit: {reason, when},
//               import: {recovering, cancelling, canCancel, stage, bytes, total, files,
//               choices: [{id, name, version, languages, defaultLanguage, bytes, files,
//               patches, place, missing}], password: {for, retry}}}
//   downloads: {sets: [{id, kind, name, state, downloaded, wanted, rate, peers, stalled,
//               networkError, error, errorKind, discovered: ""|checking|game|none|undecided,
//               main: <verb>, menu: [<verb>], busy}],
//               error, magnetDraft, dialog: {kind: candidates|details|error, ...}}
//               (the app's DownloadLibraryState defines states, verbs and candidates)
//   about:     {app: {version, code, flavor, appCommit, engineCommit, appSource,
//               engineSource}, notices: [<file>], open: {file, text, truncated}}
// What the player does goes back as Action{node "APP", verb, value} (text fields:
// node "PASSWORD" / "MAGNET", verb "edit"); the global back without a ← binding is
// Action{verb "back"}. The Android host is UiScreenActivity and its subclasses.
#include "controls.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <tuple>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {{"APP", "state"}};
constexpr const char *kAppScreenPrefix = "app:";

// The last state the host published, read by the screen that it routed to.
Json &HostState()
{
    static Json state = Json::object();
    return state;
}

std::string Str(const Json &object, const char *key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

double Num(const Json &object, const char *key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<double>() : 0.0;
}

bool Flag(const Json &object, const char *key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() && found->get<bool>();
}

const Json &Obj(const Json &object, const char *key)
{
    static const Json none;
    const auto found = object.find(key);
    return found != object.end() && found->is_object() ? *found : none;
}

const Json &List(const Json &object, const char *key)
{
    static const Json empty = Json::array();
    const auto found = object.find(key);
    return found != object.end() && found->is_array() ? *found : empty;
}

std::string Join(const Json &strings, const std::string &separator)
{
    std::string text;
    for (const Json &item : strings)
        if (item.is_string())
            text += (text.empty() ? "" : separator) + item.get<std::string>();
    return text;
}

// "{1}", "{2}" … of a string table entry replaced by the arguments.
std::string Fill(std::string text, std::initializer_list<std::string> args)
{
    int number = 1;
    for (const std::string &arg : args)
    {
        const std::string mark = "{" + std::to_string(number++) + "}";
        for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + arg.size()))
            text.replace(at, mark.size(), arg);
    }
    return text;
}

// One decimal with the decimal mark of the language ("2,3").
std::string Decimal(const Bindings &bindings, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    std::string text = buffer;
    const size_t dot = text.find('.');
    if (dot != std::string::npos)
        text.replace(dot, 1, bindings.Text("launcher.decimal_mark"));
    return text;
}

// A size in GB from a tenth of a GB, else in MB; `scale` (when given) picks the unit, so
// that "done of total" share one.
std::string Size(const Bindings &bindings, double bytes, double scale = -1)
{
    constexpr double kMiB = 1048576.0, kGiB = 1073741824.0;
    if ((scale < 0 ? bytes : scale) >= kGiB / 10)
        return Fill(bindings.Text("launcher.gb"), {Decimal(bindings, bytes / kGiB)});
    return Fill(bindings.Text("launcher.mb"), {Decimal(bindings, bytes / kMiB)});
}

// "осталось ~3 мин" for `seconds` left, empty while unknown.
std::string TimeLeft(const Bindings &bindings, double seconds)
{
    if (!(seconds > 0) || seconds > 7 * 24 * 3600)
        return {};
    if (seconds < 60)
        return bindings.Text("launcher.left_minute");
    return Fill(bindings.Text("launcher.left"), {std::to_string(static_cast<long>(std::ceil(seconds / 60)))});
}

// A command the host receives as Action{node "APP", verb, value}.
Widget AppCommand()
{
    Widget widget;
    widget.node = "APP";
    widget.type = "state";
    return widget;
}

// Rate of a growing byte count, measured from the first sample of a run (a stage of an
// import, one download), for the time left.
class Rate
{
  public:
    double SecondsLeft(const std::string &run, double now, double done, double total)
    {
        if (run != run_ || done < start_bytes_)
        {
            run_ = run;
            start_time_ = now;
            start_bytes_ = done;
        }
        const double elapsed = now - start_time_;
        if (elapsed < 3.0 || done <= start_bytes_ || total <= done)
            return 0.0;
        return (total - done) * elapsed / (done - start_bytes_);
    }

  private:
    std::string run_;
    double start_time_ = 0.0, start_bytes_ = 0.0;
};

// --- launcher ------------------------------------------------------------------------

const char *const kStages[] = {"copying", "reading", "nested", "choosing", "installing", "verifying", "finishing"};
// Their texts, "launcher.stage." + stage, named here for the pack build's key check.
[[maybe_unused]] const char *const kStageKeys[] = {
    StringKey("launcher.stage.copying"),    StringKey("launcher.stage.reading"),   StringKey("launcher.stage.nested"),
    StringKey("launcher.stage.choosing"),   StringKey("launcher.stage.installing"), StringKey("launcher.stage.verifying"),
    StringKey("launcher.stage.finishing"),
};
// What the import's discovery found in a finished set's files (the app's SetDiscovery; never
// file names): "downloads.discovered." + result. "undecided" (password, missing volumes) says
// nothing; the import explains it.
const char *const kDiscovered[] = {StringKey("downloads.discovered.checking"), StringKey("downloads.discovered.game"),
                                   StringKey("downloads.discovered.none")};

// Short status for a finished import or a launcher message: {tone, lead, text}, null when none.
Json ResultNotice(Bindings &bindings, const Json &result, bool has_game)
{
    const std::string key = Str(result, "key");
    if (key.empty())
        return nullptr;
    const Json &args = List(result, "args");
    const auto arg = [&args](size_t i) -> std::string {
        if (i >= args.size())
            return {};
        return args[i].is_string() ? args[i].get<std::string>() : std::string();
    };
    const auto gib = [&args, &bindings](size_t i) {
        return i < args.size() && args[i].is_number() ? Decimal(bindings, args[i].get<double>()) : std::string("?");
    };
    static const char *const kKnown[] = {
        StringKey("launcher.result.import_finished"),          StringKey("launcher.result.import_cancelled"),
        StringKey("launcher.result.import_no_space"),          StringKey("launcher.result.import_invalid_game"),
        StringKey("launcher.result.import_failed"),            StringKey("launcher.result.import_recovery_failed"),
        StringKey("launcher.result.import_archive_unavailable"), StringKey("launcher.result.import_archive_unsupported"),
        StringKey("launcher.result.import_archive_encrypted"), StringKey("launcher.result.import_archive_damaged"),
        StringKey("launcher.result.import_archive_memory"),    StringKey("launcher.result.import_archive_limit"),
        StringKey("launcher.result.import_archive_volumes"),   StringKey("launcher.result.import_archive_selection"),
        StringKey("launcher.result.import_archive_no_game"),   StringKey("launcher.result.import_archive_too_deep"),
        StringKey("launcher.result.import_archive_multiple"),  StringKey("launcher.result.picker_unavailable"),
        StringKey("launcher.result.exit_game_first"),          StringKey("launcher.result.recovery_required"),
        StringKey("launcher.result.start_failed"),
    };
    Json notice = Json::object();
    const std::string table = "launcher.result." + key;
    if (key == "import_no_space_need")
    {
        notice["text"] = Fill(bindings.Text("launcher.result.import_no_space_need"), {gib(0), gib(1), gib(2)});
    }
    else if (std::find_if(std::begin(kKnown), std::end(kKnown), [&table](const char *k) { return table == k; }) != std::end(kKnown))
    {
        notice["text"] = bindings.Text(table);
        // The file names the reader reported (missing volumes, the clashing names).
        if (!arg(0).empty())
            notice["text"] = notice["text"].get<std::string>() + " " + arg(0);
    }
    else
    {
        bindings.Error("launcher: unknown result key " + key);
        notice["text"] = key;
    }
    if (key == "import_finished")
        notice["tone"] = "ok";
    else if (key == "import_cancelled" || key == "picker_unavailable" || key == "exit_game_first")
        notice["tone"] = "info";
    else
    {
        notice["tone"] = "danger";
        notice["lead"] = bindings.Text("launcher.failed");
        // Every failed import keeps the game that was there.
        if (has_game && key.rfind("import_", 0) == 0)
            notice["text"] = notice["text"].get<std::string>() + " " + bindings.Text("launcher.kept");
    }
    return notice;
}

// Second line of a found game copy: version · languages · size · patch · place.
std::string RootDetail(const Bindings &bindings, const Json &root)
{
    std::vector<std::string> parts;
    if (!Str(root, "version").empty())
        parts.push_back(Str(root, "version"));
    const Json &languages = List(root, "languages");
    if (!languages.empty())
    {
        std::string list;
        for (const Json &language : languages)
            if (language.is_string())
                list += (list.empty() ? "" : ", ") + language.get<std::string>() +
                        (language.get<std::string>() == Str(root, "defaultLanguage") ? "*" : "");
        parts.push_back(Fill(bindings.Text("launcher.root_languages"), {list}));
    }
    parts.push_back(Fill(bindings.Text("launcher.root_size"),
                         {Size(bindings, Num(root, "bytes")), std::to_string(static_cast<long long>(Num(root, "files")))}));
    if (!List(root, "patches").empty())
        parts.push_back(Fill(bindings.Text("launcher.root_patched"), {Join(List(root, "patches"), ", ")}));
    if (!Str(root, "place").empty())
        parts.push_back(Fill(bindings.Text("launcher.root_place"), {Str(root, "place")}));
    std::string text;
    for (const std::string &part : parts)
        text += (text.empty() ? "" : " · ") + part;
    return text;
}

class LauncherScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-launcher";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &, Bindings &bindings) override
    {
        const Json &state = Obj(HostState(), "launcher");
        const Json &import = Obj(state, "import");
        const std::string phase = Str(state, "phase");
        const bool library = Flag(state, "library");
        const bool has_game = Flag(state, "hasGame");

        Json view = Json::object();
        view["actions"] = Json::array();
        view["back"] = false;
        view["notices"] = Json::array();
        view["password"] = nullptr;
        const auto send = [this, &bindings](const char *verb, const std::string &value = {}) {
            bindings.Send(command_, verb, value);
        };
        const auto about = [&] {
            view["actions"].push_back(Json{{"id", "about"}, {"icon", "info"}, {"label", bindings.Text("launcher.about")}});
            bindings.On("about", "", [send](const Event &) { send("about"); });
        };
        if (!state.is_object() || (phase != "setup" && phase != "ready" && phase != "busy"))
        {
            bindings.Error("launcher: state without a known phase: " + state.dump());
            view["view"] = "setup";
            view["title"] = bindings.Text("launcher.title_setup");
            return view;
        }

        if (phase == "busy" && !List(import, "choices").empty())
            return Choose(view, import, bindings);

        if (phase == "busy")
        {
            view["view"] = "busy";
            view["title"] = bindings.Text("launcher.title_busy");
            std::string title, status;
            double percent = -1;
            if (!import.is_object())
                bindings.Error("launcher: busy without an import");
            else
            {
                const std::string stage = Str(import, "stage");
                const double bytes = Num(import, "bytes"), total = Num(import, "total");
                if (Flag(import, "recovering"))
                    title = bindings.Text("launcher.recovering");
                else if (Flag(import, "cancelling"))
                    title = bindings.Text("launcher.cancelling");
                else if (std::find(std::begin(kStages), std::end(kStages), stage) != std::end(kStages))
                    title = bindings.Text("launcher.stage." + stage);
                else
                {
                    if (!stage.empty())
                        bindings.Error("launcher: unknown import stage " + stage);
                    title = bindings.Text("launcher.stage.reading");
                }
                if ((stage == "copying" || stage == "installing") && total > 0)
                {
                    percent = std::min(100.0, std::floor(bytes * 100 / total));
                    status = Fill(bindings.Text("launcher.progress_of"),
                                  {std::to_string(static_cast<int>(percent)), Size(bindings, bytes, total), Size(bindings, total)});
                    const std::string left = TimeLeft(bindings, rate_.SecondsLeft(stage, bindings.Now(), bytes, total));
                    if (!left.empty())
                        status += " · " + left;
                }
                else if (bytes > 0)
                    status = Size(bindings, bytes);
                if (stage == "installing" && Num(import, "files") > 0)
                    status += (status.empty() ? "" : " · ") +
                              Fill(bindings.Text("launcher.files_count"), {std::to_string(static_cast<long long>(Num(import, "files")))});
                view["cancel"] = Json{{"label", bindings.Text("launcher.cancel")}, {"disabled", !Flag(import, "canCancel")},
                                      {"busy", bindings.Pending("cancel")}};
                bindings.On("cancel", "", [send](const Event &) { send("cancel"); });
                const Json &password = Obj(import, "password");
                if (password.is_object())
                    view["password"] = Password(password, bindings);
            }
            view["busy"] = Json{{"title", title}, {"status", status}, {"percent", percent}};
            return view;
        }

        const Json result = ResultNotice(bindings, Obj(state, "result"), has_game);
        const Json &exit = Obj(state, "exit");
        if (exit.is_object())
        {
            static const char *const kReasons[] = {StringKey("launcher.exit.native_crash"), StringKey("launcher.exit.crash"),
                                                   StringKey("launcher.exit.anr"), StringKey("launcher.exit.low_memory"),
                                                   StringKey("launcher.exit.resources")};
            const std::string reason = "launcher.exit." + Str(exit, "reason");
            const bool known = std::find_if(std::begin(kReasons), std::end(kReasons), [&reason](const char *k) { return reason == k; }) !=
                               std::end(kReasons);
            if (!known)
                bindings.Error("launcher: unknown exit reason " + Str(exit, "reason"));
            view["notices"].push_back(Json{{"tone", "warn"},
                                           {"text", Fill(bindings.Text("launcher.exit_previous"),
                                                         {known ? bindings.Text(reason) : Str(exit, "reason"), Str(exit, "when")})}});
        }
        if (!result.is_null())
            view["notices"].push_back(result);

        if (phase == "ready")
        {
            view["view"] = "ready";
            view["title"] = bindings.Text("launcher.app_name");
            view["play"] = Json{{"label", bindings.Text("launcher.play")}, {"busy", bindings.Pending("play")}};
            view["files"] = bindings.Text("launcher.files");
            bindings.On("play", "", [send](const Event &) { send("play"); });
            bindings.On("files", "", [send](const Event &) { send("files"); });
            about();
            return view;
        }

        // Setup: no game yet, a storage problem, or the files of a ready game being changed.
        view["view"] = "setup";
        view["title"] = bindings.Text("launcher.title_setup");
        const std::string problem = Str(state, "problem");
        if (problem == "recovery_required" || problem == "start_failed")
            view["notices"].push_back(ResultNotice(bindings, Json{{"key", problem}}, false));
        else if (!problem.empty() && problem != "no_game")
            bindings.Error("launcher: unknown storage problem " + problem);
        if (Flag(state, "files"))
        {
            view["back"] = true;
            bindings.On("header-back", "", [send](const Event &) { send("files-close"); });
        }
        view["setup"] = Json{{"prompt", bindings.Text("launcher.need")},
                             {"folder", bindings.Text("launcher.folder")},
                             {"archive", bindings.Text("launcher.archive")},
                             {"archiveMeta", bindings.Text("launcher.archive_meta")},
                             {"help", bindings.Text("launcher.help")},
                             {"library", library ? bindings.Text("launcher.library") : std::string()}};
        bindings.On("folder", "", [send](const Event &) { send("folder"); });
        bindings.On("archive", "", [send](const Event &) { send("archive"); });
        bindings.On("help", "", [&bindings](const Event &) { bindings.Read(bindings.Text("launcher.help"), bindings.Text("launcher.help_text")); });
        if (library)
            bindings.On("library", "", [send](const Event &) { send("library"); });
        about();
        return view;
    }

  private:
    // Several game copies found (V2-Launcher-Folder as a picker of copies): the ready ones
    // fit, a copy that lacks volumes says which; «Использовать» installs the chosen one.
    Json Choose(Json &view, const Json &import, Bindings &bindings)
    {
        view["view"] = "choose";
        view["title"] = bindings.Text("launcher.title_choose");
        view["back"] = true;
        const Json &choices = List(import, "choices");
        bool chosen_found = false;
        for (const Json &root : choices)
            chosen_found = chosen_found || (Str(root, "id") == chosen_ && List(root, "missing").empty());
        if (!chosen_found)
        {
            chosen_.clear();
            for (const Json &root : choices)
                if (chosen_.empty() && List(root, "missing").empty())
                    chosen_ = Str(root, "id");
        }
        Json rows = Json::array();
        std::vector<std::string> ids;
        std::string chosen_name;
        for (const Json &root : choices)
        {
            const bool ready = List(root, "missing").empty();
            const std::string id = Str(root, "id");
            const std::string name = Str(root, "name").empty() ? bindings.Text("launcher.root_unnamed") : Str(root, "name");
            if (id == chosen_)
                chosen_name = name;
            Json missing = ready ? Json("") : Json{{"text", Fill(bindings.Text("launcher.root_missing"), {Join(List(root, "missing"), ", ")})}, {"tone", "dim"}};
            rows.push_back(Json{{"cells", Json::array({Json{{"text", name}, {"icon", "load"}, {"sub", RootDetail(bindings, root)}}, missing})},
                                {"fit", ready},
                                {"selected", id == chosen_},
                                {"disabled", !ready}});
            ids.push_back(id);
        }
        view["choose"] = Json{{"columns", Json::array({Json{{"label", ""}, {"w", "1"}}, Json{{"label", ""}, {"w", "200px"}, {"align", "right"}}})},
                              {"rows", rows},
                              {"primary", Json{{"label", Fill(bindings.Text("launcher.use"), {chosen_name})}, {"disabled", chosen_.empty()}}}};
        bindings.On("roots", "row", [this, &bindings, ids](const Event &event) {
            const size_t index = static_cast<size_t>(std::atoi(event.value.c_str()));
            if (index < ids.size())
            {
                chosen_ = ids[index];
                bindings.Rebuild();
            }
        });
        bindings.Ignore("roots", "select");
        bindings.On("roots", "click", [this, &bindings](const Event &) {
            if (!chosen_.empty())
                bindings.Send(command_, "choose", chosen_);
        });
        bindings.On("header-back", "", [this, &bindings](const Event &) { bindings.Send(command_, "cancel"); });
        return view;
    }

    // An encrypted archive: the password field opens the system keyboard (masked); the
    // accepted text is the answer (Action{node "PASSWORD", verb "edit"}). «Открыть» opens it too.
    Json Password(const Json &password, Bindings &bindings)
    {
        const auto edit = [this, &bindings](const Event &) {
            bindings.EditText({"password-field", "press", ""}, password_, bindings.Text("launcher.password"), false, true);
        };
        bindings.On("password-field", "", edit);
        bindings.On("password-confirm", "", edit);
        bindings.On("password-cancel", "", [this, &bindings](const Event &) { bindings.Send(command_, "password-cancel"); });
        bindings.Ignore("password", "dismiss");
        return Json{{"title", bindings.Text(Flag(password, "retry") ? "launcher.password_wrong" : "launcher.password_title")},
                    {"subtitle", Str(password, "for")},
                    {"label", bindings.Text("launcher.password")},
                    {"confirm", bindings.Text("launcher.open")}};
    }

    Widget command_ = AppCommand();
    Widget password_ = [] {
        Widget widget;
        widget.node = "PASSWORD";
        widget.type = "edit";
        return widget;
    }();
    std::string chosen_;
    Rate rate_;
};

// --- download library ------------------------------------------------------------------

const char *const kSetStates[] = {
    StringKey("downloads.state.copying"),     StringKey("downloads.state.inspecting"),
    StringKey("downloads.state.metadata"),    StringKey("downloads.state.awaiting-selection"),
    StringKey("downloads.state.downloading"), StringKey("downloads.state.pausing"),
    StringKey("downloads.state.paused"),      StringKey("downloads.state.complete"),
    StringKey("downloads.state.interrupted"), StringKey("downloads.state.error"),
};
// Kinds of the metadata's candidates (file names only, never a verified game).
const char *const kCandidateKinds[] = {StringKey("downloads.kind.loose-game-candidate"), StringKey("downloads.kind.installer-candidate"),
                                       StringKey("downloads.kind.archive-candidate")};

// Verbs the host offers for a set: label key, and the icon when the verb is a bare glyph.
struct Verb
{
    const char *verb;
    const char *label;
    const char *icon; // inline as a glyph; null: a button
    const char *variant;
};
const Verb kVerbs[] = {
    {"import", StringKey("downloads.import"), nullptr, "primary"},
    {"choose", StringKey("downloads.choose"), nullptr, "secondary"},
    {"pause", StringKey("downloads.pause"), "pause", nullptr},
    {"resume", StringKey("downloads.resume"), "play", nullptr},
    {"details", StringKey("downloads.details"), nullptr, "secondary"},
    {"delete", StringKey("downloads.delete"), nullptr, "danger"},
};

const Verb *FindVerb(const std::string &verb)
{
    for (const Verb &known : kVerbs)
        if (verb == known.verb)
            return &known;
    return nullptr;
}

class DownloadsScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-downloads";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &, Bindings &bindings) override
    {
        const Json &state = Obj(HostState(), "downloads");
        const Json &sets = List(state, "sets");
        magnet_.value = Str(state, "magnetDraft");
        Json view = Json::object();
        view["title"] = bindings.Text("downloads.title");
        view["empty"] = sets.empty();
        view["emptyText"] = bindings.Text("downloads.empty");
        view["error"] = Str(state, "error");
        view["actions"] = sets.empty() ? Json::array() : Json::array({Json{{"id", "add"}, {"icon", "plus"}, {"label", bindings.Text("downloads.add")}}});
        view["sources"] = Sources(bindings);
        view["sheet"] = nullptr;
        view["dialog"] = nullptr;
        bindings.On("add", "", [this, &bindings](const Event &) {
            adding_ = true;
            bindings.Rebuild();
        });
        bindings.On("header-back", "", [this, &bindings](const Event &) { bindings.Send(command_, "back"); });

        Json rows = Json::array();
        bool menu_found = false;
        for (const Json &set : sets)
        {
            const std::string id = Str(set, "id");
            rows.push_back(Row(set, bindings));
            if (id == menu_)
            {
                menu_found = true;
                view["sheet"] = Menu(set, bindings);
            }
        }
        if (!menu_found)
            menu_.clear();
        view["sets"] = rows;
        // Questions of the host (candidate choice, details, error)
        // come first; the add sheet and the magnet field are this screen's own.
        const Json &dialog = Obj(state, "dialog");
        if (dialog.is_object())
            view["dialog"] = Dialog(dialog, bindings);
        else if (magnet_open_)
            view["dialog"] = Magnet(bindings);
        else if (adding_ && !sets.empty())
            view["dialog"] = Json{{"kind", "sources"}, {"title", bindings.Text("downloads.add")}, {"cancel", bindings.Text("common.cancel")}};
        bindings.On("dialog", "dismiss", [this, &bindings](const Event &) { CloseDialog(bindings); });
        bindings.On("dialog-cancel", "", [this, &bindings](const Event &) { CloseDialog(bindings); });
        bindings.On("dialog-close", "", [this, &bindings](const Event &) { CloseDialog(bindings); });
        return view;
    }

  private:
    void CloseDialog(Bindings &bindings)
    {
        const Json &dialog = Obj(Obj(HostState(), "downloads"), "dialog");
        if (dialog.is_object())
            bindings.Send(command_, "dialog-close");
        magnet_open_ = adding_ = false;
        bindings.Rebuild();
    }

    // .torrent, magnet, archive: the empty list and the add sheet.
    Json Sources(Bindings &bindings)
    {
        Json rows = Json::array({Json{{"id", "source-torrent"}, {"icon", "download"}, {"title", bindings.Text("downloads.torrent")}, {"meta", ""}},
                                 Json{{"id", "source-magnet"}, {"icon", "magnet"}, {"title", bindings.Text("downloads.magnet")}, {"meta", ""}},
                                 Json{{"id", "source-archive"}, {"icon", "archive"}, {"title", bindings.Text("downloads.archive")},
                                      {"meta", bindings.Text("downloads.archive_meta")}}});
        bindings.On("source-torrent", "", [this, &bindings](const Event &) {
            adding_ = false;
            bindings.Send(command_, "add-torrent");
        });
        bindings.On("source-archive", "", [this, &bindings](const Event &) {
            adding_ = false;
            bindings.Send(command_, "add-archive");
        });
        bindings.On("source-magnet", "", [this, &bindings](const Event &) {
            adding_ = false;
            magnet_open_ = true;
            bindings.Rebuild();
        });
        return rows;
    }

    Json Row(const Json &set, Bindings &bindings)
    {
        const std::string id = Str(set, "id"), state = Str(set, "state"), kind = Str(set, "kind");
        const double done = Num(set, "downloaded"), wanted = Num(set, "wanted"), rate = Num(set, "rate");
        const std::string key = "downloads.state." + state;
        const bool known = std::find_if(std::begin(kSetStates), std::end(kSetStates), [&key](const char *k) { return key == k; }) != std::end(kSetStates);
        if (!known)
            bindings.Error("downloads: unknown set state " + state);
        std::string meta = known ? bindings.Text(key) : state;
        const std::string error = Str(set, "error");
        const std::string discovered = Str(set, "discovered");
        if (!discovered.empty() && discovered != "undecided")
        {
            const std::string found = "downloads.discovered." + discovered;
            if (std::find_if(std::begin(kDiscovered), std::end(kDiscovered), [&found](const char *k) { return found == k; }) != std::end(kDiscovered))
                meta += " · " + bindings.Text(found);
            else
                bindings.Error("downloads: unknown discovery result " + discovered);
        }
        if (state == "downloading")
        {
            const std::string left = TimeLeft(bindings, rate > 0 && wanted > done ? (wanted - done) / rate : 0.0);
            if (!left.empty())
                meta += " · " + left;
        }
        else if (wanted > 0 || done > 0)
            meta += " · " + Size(bindings, wanted > 0 ? wanted : done);
        // Nothing moves: say why (no peers, or peers without data) and the latest network problem.
        const bool stalled = Flag(set, "stalled");
        if (stalled)
        {
            meta += " · " + bindings.Text(Num(set, "peers") > 0 ? "downloads.stalled_no_data" : "downloads.stalled_no_peers");
            if (!Str(set, "networkError").empty())
                meta += ": " + Str(set, "networkError");
        }
        if (!error.empty())
            meta += ": " + (Str(set, "errorKind") == "storage-full" ? bindings.Text("downloads.storage_full") + " — " : std::string()) + error;
        Json row = Json{{"id", id},
                        {"icon", state == "complete" ? "check" : kind == "magnet" ? "magnet" : kind == "archive" ? "archive" : "download"},
                        {"title", Str(set, "name")},
                        {"meta", meta},
                        {"metaTone", error.empty() && !stalled && state != "error" && state != "interrupted" ? "" : "danger"},
                        {"progress", state == "downloading" && wanted > 0 ? std::max(1.0, std::floor(done * 100 / wanted)) : 0.0},
                        {"menu", !List(set, "menu").empty()},
                        {"busy", Flag(set, "busy") || bindings.Pending("set-" + id)},
                        {"action", ""},
                        {"actionLabel", ""},
                        {"button", nullptr}};
        const std::string main = Str(set, "main");
        const Verb *verb = main.empty() ? nullptr : FindVerb(main);
        if (!main.empty() && !verb)
            bindings.Error("downloads: unknown set verb " + main);
        if (verb && verb->icon)
        {
            row["action"] = verb->icon;
            row["actionLabel"] = bindings.Text(verb->label);
        }
        else if (verb)
        {
            row["button"] = Json{{"id", "btn-" + id}, {"label", bindings.Text(verb->label)}, {"variant", verb->variant}};
            bindings.On("btn-" + id, "", [this, &bindings, main, id](const Event &) { bindings.Send(command_, main, id); });
        }
        // The row reports its glyph as "action", its «⋯» as "menu"; a tap elsewhere does nothing.
        bindings.On("set-" + id, "", [this, &bindings, main, id](const Event &event) {
            if (event.verb == "menu")
            {
                menu_ = id;
                bindings.Rebuild();
            }
            else if (event.verb == "action" && !main.empty())
                bindings.Send(command_, main, id);
        });
        return row;
    }

    // «⋯» of a set: its rare actions; delete is confirmed by holding.
    Json Menu(const Json &set, Bindings &bindings)
    {
        const std::string id = Str(set, "id");
        Json actions = Json::array();
        for (const Json &item : List(set, "menu"))
        {
            const std::string verb = item.is_string() ? item.get<std::string>() : std::string();
            const Verb *known = FindVerb(verb);
            if (!known)
            {
                bindings.Error("downloads: unknown menu verb " + verb);
                continue;
            }
            const std::string element = "menu-" + verb;
            actions.push_back(Json{{"id", element}, {"label", bindings.Text(known->label)}, {"variant", known->variant ? known->variant : "secondary"},
                                   {"hold", verb == "delete"}});
            // Delete is held (the button's ring): its press reports only when the ring closed.
            bindings.On(element, "", [this, &bindings, verb, id](const Event &) {
                menu_.clear();
                bindings.Send(command_, verb, id);
            });
        }
        bindings.On("sheet", "dismiss", [this, &bindings](const Event &) {
            menu_.clear();
            bindings.Rebuild();
        });
        bindings.On("sheet-close", "", [this, &bindings](const Event &) {
            menu_.clear();
            bindings.Rebuild();
        });
        return Json{{"title", Str(set, "name")}, {"actions", actions}};
    }

    Json Magnet(Bindings &bindings)
    {
        bindings.On("magnet-field", "", [this, &bindings](const Event &event) { bindings.EditText(event, magnet_, bindings.Text("downloads.magnet_label"), false); });
        bindings.On("dialog-confirm", "", [this, &bindings](const Event &event) {
            if (magnet_.value.empty())
            {
                bindings.EditText({"magnet-field", "press", ""}, magnet_, bindings.Text("downloads.magnet_label"), false);
                return;
            }
            magnet_open_ = false;
            bindings.Send(command_, "magnet");
            (void)event;
        });
        return Json{{"kind", "magnet"},
                    {"title", bindings.Text("downloads.magnet")},
                    {"label", bindings.Text("downloads.magnet_label")},
                    {"value", magnet_.value},
                    {"placeholder", "magnet:?xt=urn:btih:…"},
                    {"confirm", bindings.Text("downloads.add_confirm")},
                    {"cancel", bindings.Text("common.cancel")}};
    }

    Json Dialog(const Json &dialog, Bindings &bindings)
    {
        const std::string kind = Str(dialog, "kind"), id = Str(dialog, "id");
        if (kind == "candidates")
        {
            // One candidate of the metadata (with its volume hints), or every file: never combined.
            Json rows = Json::array();
            const Json &candidates = List(dialog, "candidates");
            const auto offer = [&](const std::string &element, const char *icon, const std::string &title, const Json &choice, int number) {
                rows.push_back(Json{{"id", element}, {"icon", icon}, {"title", title},
                                    {"meta", Fill(bindings.Text("downloads.details_total"),
                                                  {std::to_string(static_cast<long long>(Num(choice, "files"))), Size(bindings, Num(choice, "bytes"))})}});
                bindings.On(element, "", [this, &bindings, id, number](const Event &) { bindings.Send(command_, "download", id + ":" + std::to_string(number)); });
            };
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                const std::string key = "downloads.kind." + Str(candidates[i], "kind");
                const bool known = std::find_if(std::begin(kCandidateKinds), std::end(kCandidateKinds), [&key](const char *k) { return key == k; }) !=
                                   std::end(kCandidateKinds);
                if (!known)
                    bindings.Error("downloads: unknown candidate kind " + Str(candidates[i], "kind"));
                offer("candidate-" + std::to_string(i), Str(candidates[i], "kind") == "loose-game-candidate" ? "load" : "archive",
                      Str(candidates[i], "label") + (known ? " · " + bindings.Text(key) : std::string()), candidates[i], static_cast<int>(i));
            }
            offer("candidate-all", "download", bindings.Text("downloads.all_files"), Obj(dialog, "all"), -1);
            return Json{{"kind", "pick"}, {"title", bindings.Text("downloads.files_title")}, {"rows", rows}, {"cancel", bindings.Text("common.cancel")}};
        }
        if (kind == "details" || kind == "error")
        {
            std::string text;
            if (kind == "details")
            {
                for (const Json &path : List(dialog, "files"))
                    if (path.is_string())
                        text += path.get<std::string>() + "\n";
                text += Fill(bindings.Text("downloads.details_total"), {std::to_string(static_cast<long long>(Num(dialog, "count"))), Size(bindings, Num(dialog, "bytes"))});
            }
            else
                text = Str(dialog, "text");
            return Json{{"kind", "info"},
                        {"title", kind == "details" ? Str(dialog, "title") : bindings.Text("downloads.error")},
                        {"text", text},
                        {"actions", Json::array({Json{{"id", "dialog-close"}, {"label", bindings.Text("common.close")}, {"variant", "secondary"}}})}};
        }
        bindings.Error("downloads: unknown dialog " + kind);
        return nullptr;
    }

    Widget command_ = AppCommand();
    Widget magnet_ = [] {
        Widget widget;
        widget.node = "MAGNET";
        widget.type = "edit";
        return widget;
    }();
    std::string menu_;
    bool adding_ = false, magnet_open_ = false;
};

// --- about and licences ------------------------------------------------------------------

// "ui-RmlUi-MIT.txt" → {"RmlUi", "MIT"}: a notice file is named <component>-<licence>
// (or <component>-LICENSE[-<licence>]); a few carry no licence in their name.
std::pair<std::string, std::string> NoticeName(std::string file)
{
    const auto ends = [&file](const std::string &tail) {
        return file.size() > tail.size() && file.compare(file.size() - tail.size(), std::string::npos, tail) == 0;
    };
    for (const char *prefix : {"ui-", "archive-", "font-"})
        if (file.rfind(prefix, 0) == 0)
            file = file.substr(std::char_traits<char>::length(prefix));
    for (const char *suffix : {".txt", ".rst", ".h", ".c"})
        if (ends(suffix))
            file.resize(file.size() - std::char_traits<char>::length(suffix));
    static const std::pair<const char *, std::pair<const char *, const char *>> kNamed[] = {
        {"storm-GPLv3", {"Storm Engine", "GPL-3.0"}}, {"SDL", {"SDL", "zlib"}}, {"NDK-toolchain-NOTICE", {"Android NDK", ""}},
        {"miniaudio-storm-patch", {"miniaudio (patch)", ""}}, {"try_signal-license", {"try_signal", ""}},
        {"fast_float-LICENSE-APACHE", {"fast_float", "Apache-2.0"}}, {"fast_float-LICENSE-MIT", {"fast_float", "MIT"}},
    };
    for (const auto &[stem, shown] : kNamed)
        if (file == stem)
            return {shown.first, shown.second};
    for (const char *notice : {"-LICENSE", "-SOURCE"})
        if (ends(notice))
            return {file.substr(0, file.size() - std::char_traits<char>::length(notice)), ""};
    static const char *const kLicences[] = {"BSD-3-Clause", "Apache-2.0", "BSL-1.0", "MIT", "FTL", "OFL"};
    for (const char *licence : kLicences)
    {
        const std::string tail = std::string("-") + licence, head = std::string(licence) + "-";
        if (ends(tail))
            return {file.substr(0, file.size() - tail.size()), licence};
        if (file.size() > head.size() && file.compare(0, head.size(), head) == 0)
            return {file.substr(head.size()), licence};
    }
    return {file, ""};
}

class AboutScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-about";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &, Bindings &bindings) override
    {
        const Json &state = Obj(HostState(), "about");
        const Json &app = Obj(state, "app");
        const Json &open = Obj(state, "open");
        Json view = Json::object();
        bindings.On("header-back", "", [this, &bindings, open](const Event &) { bindings.Send(command_, open.is_object() ? "license-close" : "back"); });
        if (open.is_object())
        {
            // One notice, verbatim, on parchment; paragraphs as the file has them.
            const auto [name, licence] = NoticeName(Str(open, "file"));
            Json paragraphs = Json::array();
            if (Flag(open, "truncated"))
                paragraphs.push_back(bindings.Text("about.license_tail"));
            const std::string text = Str(open, "text");
            size_t start = 0;
            while (start < text.size())
            {
                size_t end = text.find("\n\n", start);
                if (end == std::string::npos)
                    end = text.size();
                std::string paragraph = text.substr(start, end - start);
                if (paragraph.find_first_not_of(" \t\r\n") != std::string::npos)
                    paragraphs.push_back(paragraph);
                start = end + 2;
            }
            view["title"] = bindings.Text("about.license");
            view["license"] = Json{{"name", name}, {"kind", licence}, {"paragraphs", paragraphs}};
            return view;
        }
        const auto short_commit = [](const std::string &commit) { return commit.substr(0, 7); };
        const auto short_url = [](std::string url) {
            if (url.rfind("https://", 0) == 0)
                url = url.substr(8);
            const size_t tree = url.find("/tree/");
            return tree == std::string::npos ? url : url.substr(0, tree);
        };
        view["title"] = bindings.Text("about.title");
        view["license"] = nullptr;
        view["name"] = bindings.Text("launcher.app_name");
        view["version"] = Fill(bindings.Text("about.version"), {Str(app, "version"), std::to_string(static_cast<long long>(Num(app, "code")))});
        view["purpose"] = bindings.Text("about.purpose");
        view["facts"] = Json::array({Json{{"k", bindings.Text("about.engine")}, {"v", "Storm Engine, GPL-3.0"}},
                                     Json{{"k", bindings.Text("about.app_commit")}, {"v", short_commit(Str(app, "appCommit"))}},
                                     Json{{"k", bindings.Text("about.engine_commit")}, {"v", short_commit(Str(app, "engineCommit"))}},
                                     Json{{"k", bindings.Text("about.flavor")}, {"v", Str(app, "flavor")}}});
        Json sources = Json::array();
        for (const auto &[id, key, field] : {std::tuple{"source-app", StringKey("about.app_source"), "appSource"},
                                             std::tuple{"source-engine", StringKey("about.engine_source"), "engineSource"}})
        {
            const std::string url = Str(app, field);
            if (url.rfind("https://", 0) != 0)
            {
                bindings.Error(std::string("about: no source address for ") + field);
                continue;
            }
            sources.push_back(Json{{"id", id}, {"title", bindings.Text(key)}, {"meta", short_url(url)}});
            bindings.On(id, "", [this, &bindings, url](const Event &) { bindings.Send(command_, "open-url", url); });
        }
        view["sources"] = sources;
        view["note"] = bindings.Text("about.note");
        view["noticesTitle"] = bindings.Text("about.notices");
        // The engine's own licence first, then the components by name.
        std::vector<std::tuple<std::string, std::string, std::string>> notices;
        for (const Json &file : List(state, "notices"))
            if (file.is_string())
            {
                const auto [name, licence] = NoticeName(file.get<std::string>());
                notices.emplace_back(name, licence, file.get<std::string>());
            }
        const auto key = [](const std::string &name) {
            std::string lower = name == "Storm Engine" ? std::string(1, '\0') : name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower;
        };
        std::stable_sort(notices.begin(), notices.end(), [&key](const auto &a, const auto &b) { return key(std::get<0>(a)) < key(std::get<0>(b)); });
        Json rows = Json::array();
        for (const auto &[name, licence, path] : notices)
        {
            const std::string id = "notice-" + std::to_string(rows.size());
            rows.push_back(Json{{"id", id}, {"title", name}, {"value", licence}});
            bindings.On(id, "", [this, &bindings, path = path](const Event &) { bindings.Send(command_, "license", path); });
        }
        view["notices"] = rows;
        return view;
    }

  private:
    Widget command_ = AppCommand();
};

class LauncherConsumer final : public StateConsumer
{
  public:
    const char *Channel() const override
    {
        return "launcher";
    }
    void OnState(const Json &state, Router &router) override
    {
        const std::string screen = state.is_object() ? Str(state, "screen") : std::string();
        if (screen != "launcher" && screen != "downloads" && screen != "about")
        {
            router.ReportError("launcher channel: unknown screen '" + screen + "'");
            return;
        }
        HostState() = state;
        if (screen != screen_)
        {
            screen_ = screen;
            ++opening_;
        }
        InterfaceModel model;
        // The application's screens live in their own id space: game interface ids are ini
        // file stems (the game's credits are "about"), which can never contain ':'.
        model.screen = kAppScreenPrefix + screen;
        model.language = Str(state, "language");
        model.opening = opening_;
        model.revision = ++revision_;
        router.SetInterface(model);
    }

  private:
    std::string screen_;
    uint64_t opening_ = 0, revision_ = 0;
};

} // namespace

std::unique_ptr<Screen> CreateLauncherScreen()
{
    return std::make_unique<LauncherScreen>();
}

std::unique_ptr<Screen> CreateDownloadsScreen()
{
    return std::make_unique<DownloadsScreen>();
}

std::unique_ptr<Screen> CreateAboutScreen()
{
    return std::make_unique<AboutScreen>();
}

std::unique_ptr<StateConsumer> CreateLauncherConsumer()
{
    return std::make_unique<LauncherConsumer>();
}

} // namespace storm::ui::screens
