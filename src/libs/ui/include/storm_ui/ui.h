// Portable game UI runtime. The platform host owns the window, the GL context and
// the frame loop; this library owns documents, components, input routing and drawing.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace storm::ui
{

// Read access to the UI pack (documents, styles, component specs, icons, fonts).
class FileSource
{
  public:
    virtual ~FileSource() = default;
    virtual bool Read(const std::string &path, std::vector<uint8_t> &out) = 0;
};
std::unique_ptr<FileSource> DirectoryFiles(std::string root);

// Migration switch, as data: true while the platform host still draws this interface id
// with its own form (the zone is not ported yet). Every other id goes to SetInterface.
bool HostOwnsInterface(const std::string &screen);
// The same switch for the in-game HUD, by gameplay mode of the "gameplay" state channel
// ("explore", "melee", "sailing", "naval", "worldmap"): true while the platform host still
// draws that mode's HUD itself. For every other mode the HUD is drawn here.
bool HostOwnsHud(const std::string &mode);

struct InterfaceModel; // interface_model.h

// The one path out of the UI. For a game interface: the widget (`node`, `index`) of the
// open `screen`, the verb the player chose and its value. For a document loaded with
// LoadDocument (gallery): `node` is the element id, `screen` is empty.
// HUD input has `screen` "hud": `verb` names the engine entry point ("move", "combat",
// "landcommands", "context", "shortcut") and `value` is its payload, so that the engine
// command is "<verb>:<value>". A non-empty `node` then names a latest-value slot (the stick
// position, a coverage acknowledgement): a host that queues actions keeps only the newest
// action of a slot. Two verbs are for the platform host itself, not for the engine queue:
// "shortcut" (value "mainmenu" | "character": open that game menu through the host's
// shortcut path) and "presence" (value "shown" | "hidden": the HUD appeared or went away).
struct Action
{
    std::string node;
    std::string verb;  // press, select, activate, open, toggle, edit, set, left, right, back, …
    std::string value; // row identity for select/activate/open; new value for toggle/edit/set
    std::string screen;
    uint64_t opening = 0; // InterfaceModel::opening the action belongs to
    int index = -1;
    std::string guard; // row the action must still apply to (commerce quantities); may be empty
};
// The line today's engine queue expects for an action ("widget:store\tTABLE_LIST\t3\tselect@7:id",
// "back"). Platform hosts forward it unchanged; an engine that hosts the UI itself reads
// the Action and does not need it.
std::string EngineCommand(const Action &action);

// A text field asks for the platform editor (IME bridge). The host shows a native editor
// over `x, y, width, height` (physical pixels) and answers with Host::SubmitText.
struct TextRequest
{
    std::string token;
    std::string text;
    std::string title;
    bool numeric = false;
    float x = 0, y = 0, width = 0, height = 0;
};

class RenderBackend; // render_backend.h

// A decoded game picture: premultiplied RGBA8, top row first.
struct Picture
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct Config
{
    FileSource *files = nullptr; // must outlive the host
    // Renderer seam, chosen by the host and required: CreateGl3Backend() of
    // render_backend.h (its GL context must be current) or the engine's own backend once
    // the UI is drawn inside its frame. There is no default and nothing else is tried.
    RenderBackend *backend = nullptr; // must outlive the host
    int width = 0;               // physical pixels
    int height = 0;
    float density = 1.0f; // physical pixels per dp
    // Directory with the player's game files (the one holding RESOURCE/). Game pictures
    // are decoded from there at run time; nothing of them ships with the application.
    std::string game_root;
    // Optional replacement for the built-in .tx reader: called on a worker thread with
    // the texture name ("INTERFACES/items6"); false keeps the placeholder. This is where
    // the engine's texture module plugs in, so that pictures are decoded in one place.
    std::function<bool(const std::string &texture, Picture &out)> game_picture_loader;
    std::function<void(const Action &)> on_action;
    std::function<void(const TextRequest &)> on_text_request;
    // Debug hosts: data and pack errors (malformed model, missing screen, unmapped event,
    // rejected ported screen) are also shown as a toast, not only logged.
    bool show_errors = false;
    // Optional clock in seconds, for deterministic captures.
    std::function<double()> clock;
};

enum class TouchPhase
{
    Down,
    Move,
    Up,
    Cancel
};

class Host
{
  public:
    // With the GL backend its context must be current on the calling thread, here and for
    // every other call. Landscape only: width is the long side.
    // One host at a time. After the context is lost, destroy the host and create a new one.
    static std::unique_ptr<Host> Create(const Config &config, std::string *error = nullptr);
    virtual ~Host() = default;

    virtual bool LoadDocument(const std::string &path) = 0;
    virtual void Resize(int width, int height, float density) = 0;
    virtual void SetGameRoot(const std::string &game_root) = 0;
    // Safe-area insets in dp (display cutout and system bars). HUD scenes and menu screens
    // keep their content inside; templates read them as `safe.left` … `safe.bottom`.
    virtual void SetSafeArea(float left, float top, float right, float bottom) = 0;
    virtual void Touch(TouchPhase phase, int pointer, float x, float y) = 0;
    // Updates and draws one frame over a transparent background. Returns the time in
    // seconds until another frame is needed: 0 while something animates.
    virtual double Frame() = 0;
    virtual void Toast(const std::string &text, const std::string &tone) = 0;
    // Replaces the props of the component with this element id (JSON object).
    virtual bool SetProps(const std::string &element_id, const std::string &json) = 0;
    virtual void ScrollTo(const std::string &element_id) = 0;

    // --- game interfaces -----------------------------------------------------------
    // Routes a game interface to its screen: the ported screen registered for the id when
    // the model passes its admission, the generic layout otherwise. The JSON overload
    // takes the engine's wire format. Call again for every model revision.
    virtual bool SetInterface(const std::string &json) = 0;
    virtual void SetInterface(const InterfaceModel &model) = 0;
    virtual void CloseInterface() = 0;
    // A named state channel of the game: JSON as the engine publishes it ("dialogue",
    // "gameplay", "combat", "vitals", "landCommands"; "worldInfo" and "contextAction" are
    // the engine's text as a JSON string), plus "presentation", which the platform host
    // writes itself: {"screen": "<interface id open in the game, or empty>", "loading",
    // "cinematic", "dialogue", "live": <the game is in front and running>}. The HUD scene
    // of the gameplay mode is drawn from these while nothing else covers the game.
    // False (and an error) for malformed JSON; a channel without a consumer and data that
    // does not fit its channel are errors too (logged, shown in debug hosts).
    virtual bool SetState(const std::string &channel, const std::string &json) = 0;
    // Answer to Config::on_text_request; `accepted` false = the editor was dismissed.
    virtual void SubmitText(const std::string &token, const std::string &text, bool accepted) = 0;
    // True while a game interface or a plain document covers the game.
    virtual bool InterfaceActive() const = 0;

    // --- touch routing over the game ------------------------------------------------
    // The UI is a layer over the game: a pointer belongs to it only when it goes down on
    // something of the UI. Ask at pointer-down, per pointer; when true, deliver that
    // pointer's whole gesture through Touch, otherwise give it to the game (camera look).
    // True on an interactive HUD element, anywhere while an interface, a modal sheet or a
    // plain document is shown. Coordinates in physical pixels.
    // Thread-safe: answered from the layout of the last frame, so the thread that receives
    // touches does not wait for the UI thread.
    virtual bool WantsTouch(float x, float y) const = 0;
    // The platform took the gestures away (focus lost, a menu opened): every pointer the
    // UI holds is cancelled and every held HUD control is released.
    virtual void CancelInput() = 0;
    // Thread-safe: the back key belongs to the UI now (an open HUD sheet).
    virtual bool WantsBack() const = 0;
    // The back key, after WantsBack said so.
    virtual void Back() = 0;
};

} // namespace storm::ui
