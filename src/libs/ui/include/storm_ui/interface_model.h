// Platform-neutral description of a game interface (a menu screen of the game) as the
// engine exports it: the screen id and its widgets in reading order. The UI routes this
// model to a screen implementation; nothing here knows about JNI, Android or a renderer.
//
// Today the engine serialises the model as JSON (xinterface/mobile_widget.hpp) and the
// platform host forwards those bytes to Host::SetInterface. Once the UI is drawn inside
// the engine frame the engine fills the struct directly.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::ui
{

// A picture the engine refers to: an atlas cell of pictures.ini, a battle icon, a portrait or
// a texture file of the game ("interfaces/maps/kayman") with the rectangle of it shown.
struct ImageRef
{
    int64_t handle = 0;
    int64_t revision = 0;
    int width = 0;
    int height = 0;
    uint32_t tint_argb = 0xffffffff;
    std::string kind;  // "atlas", "battle_icon", "portrait", "texture" or empty
    std::string group; // atlas: picture group ("GOODS")
    std::string image; // atlas: picture name ("Balls"); portrait: the face ("12"); texture: its name
    float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // texture: left, top, right, bottom
    std::string mode;  // battle_icon
    std::string atlas; // battle_icon
    int pic = -1;      // battle_icon
};

struct Cell
{
    int column = 0;
    std::string text;
    uint32_t color_argb = 0xffffffff;
    std::vector<ImageRef> images;
};

struct Widget
{
    std::string node; // name of the interface node; rows of one table share it
    std::string type; // heading, text, button, choice, check, edit, slider, stepper, image, or a form-specific type
    std::string label;
    std::string value;
    int index = -1; // row / option number inside the node, -1 for a single control
    int order = 0;  // reading order of the game layout (top, then left)
    bool enabled = true;
    std::string row_id; // stable identity of a table row
    std::vector<Cell> cells;
    std::vector<ImageRef> images;
    std::vector<std::string> actions; // verbs the engine accepts; empty = the defaults of the type
    int min = 0, max = 0, step = 1;   // slider
    bool exclusive = false;           // check: radio group
    std::string unavailable_reason;
    std::vector<std::string> column_headers;
    std::optional<std::vector<Cell>> header_cells;
    std::map<std::string, std::string> context;
    std::optional<uint32_t> text_color_argb;

    // True when the engine accepts `verb` for this widget now (enabled and offered).
    bool Offers(std::string_view verb) const;
    // Text of a table column: the cell with that column number, else the tab-separated label part.
    std::string CellText(int column) const;
    const ImageRef *FirstImage() const;
};

struct InterfaceModel
{
    std::string screen;   // interface id: the ini name in lower case ("store")
    std::string language; // game language name
    uint64_t opening = 0;  // changes every time a screen is opened ("screenRevision" on the wire)
    uint64_t revision = 0; // changes whenever the widgets change
    std::vector<Widget> widgets;

    const Widget *Find(std::string_view node, std::string_view type = {}) const;
};

// Reads the engine's JSON wire format (schemaVersion 2). Unknown fields are ignored.
bool ParseInterfaceModel(std::string_view json, InterfaceModel &out, std::string *error = nullptr);

// Case-insensitive comparison of node names (the game's ini files are not consistent).
bool SameNode(std::string_view a, std::string_view b);

} // namespace storm::ui
