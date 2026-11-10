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

// What a component reports to the game: the widget id, a verb and an optional value.
struct Action
{
    std::string id;
    std::string verb;
    std::string value;
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
    // Renderer seam. Null selects the bundled GL backend (a GL context must be current).
    // The engine passes its own backend here once the UI is drawn inside its frame.
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
    virtual void Touch(TouchPhase phase, int pointer, float x, float y) = 0;
    // Updates and draws one frame over a transparent background. Returns the time in
    // seconds until another frame is needed: 0 while something animates.
    virtual double Frame() = 0;
    virtual void Toast(const std::string &text, const std::string &tone) = 0;
    // Replaces the props of the component with this element id (JSON object).
    virtual bool SetProps(const std::string &element_id, const std::string &json) = 0;
    virtual void ScrollTo(const std::string &element_id) = 0;
};

} // namespace storm::ui
