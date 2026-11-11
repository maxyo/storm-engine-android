// View builders shared by every screen: one widget (or one group of widgets) becomes the
// JSON of one row of the pack parts <s-control> / <s-block>, with its events bound.
#pragma once

#include "screen.h"

#include <functional>

namespace storm::ui::screens
{

std::string Upper(std::string_view text);
bool Short(const std::string &text, size_t chars); // at most `chars` code points, one line
size_t Length(const std::string &text);            // code points

enum class ButtonRole
{
    Exit,
    Confirm,
    Danger,
    Section,
    Other
};
ButtonRole RoleOf(const Widget &button);
// Header section a navigation button opens ("hero", "ship", …), or empty.
std::string SectionOf(const Widget &button);

// The unified row image {kind:"item", texture:"atlas:GROUP/Picture"} for a picture the
// engine names by its pictures.ini group, {kind:"portrait", texture:"INTERFACES/PORTRAITS/
// 128/face_N"} for a game portrait, {kind:"item", texture:"<name under RESOURCE/Textures>"} for any
// other game texture; null when the widget carries no such reference.
Json PictureOf(const ImageRef *image);
// {texture, uv} for <ui-game-image> (c-map-view, backdrops): a texture file of the game with
// the rectangle shown ("interfaces/maps/kayman", "0,0,1,1") or an atlas picture ("atlas:GROUP/
// Picture", ""); null for any other picture.
Json TextureOf(const ImageRef *image);
// The props of c-map-view for a "map" widget (a scrolled picture) and its "map_marker" rows;
// "base" is empty when the engine's context lacks it (the screen reports that).
Json MapViewOf(const Widget &map, std::span<const Widget *const> markers);
// The nation a picture of the game's NATIONS group shows, as our flag name ("england",
// "france", "spain", "holland", "pirates"), or empty.
std::string NationOf(const ImageRef *image);
// {id, label, variant, disabled, reason, busy} of a c-button; a tap presses the widget.
Json ButtonView(Bindings &bindings, const Widget &button, const std::string &id, const char *variant);
// The primary button of a money deal (store, hire): {label: "<operation> <amount>", sub: the
// price line, disabled, reason, busy}, or null while the amount is unchanged: with nothing to
// confirm there is no deal button (the game's bare "Ok" read as a dead, empty button). An
// amount the game refuses (`ok` not offering press) stays a disabled button with its reason.
// `settling`: the game does not hold the amount yet; the button waits (busy) until it does.
Json DealView(Bindings &bindings, const Widget &ok, const std::string &operation, int amount, const std::string &sub, bool settling = false);
// {kind:"action", id, title, disabled, reason, selected, busy}: a button or choice as a list row.
Json ActionRow(Bindings &bindings, const Widget &widget, const std::string &id);
// Sends the verb a tap on a choice row means: activate on the selected row when offered,
// else select, else open.
void TapChoice(Bindings &bindings, const Widget &choice);
// Appends the rows of a run of controls (check, edit, slider, stepper) to `rows`.
// `labels` (empty, or one per widget) holds the short text that preceded a control and names it.
void ControlRows(Bindings &bindings, std::span<const Widget *const> widgets, const std::string &id_prefix,
                 const std::vector<std::string> &labels, Json &rows);

// The header's subject switch of c-screen (key "header"): ‹ name k of n ›, and a tap on the name
// opens the list of every choice (c-screen's subject sheet, a c-dialog of c-list-rows: ids
// header-subjects, header-subject-<i>). ‹, › and a picked row call `choose` with the choice's
// index. The list is offered from two choices on; the view is null without choices. A screen
// keeps one switch for its lifetime and lets Android back close the open list (Open, Close).
class SubjectSwitch
{
  public:
    struct Choice
    {
        std::string label;
        std::string meta;
        Json image;       // row image (Portrait, PictureOf) or null
        std::string icon; // row icon without an image
    };
    Json View(Bindings &bindings, const std::vector<Choice> &choices, size_t current, const std::string &title,
              std::function<void(size_t)> choose);
    bool Open() const
    {
        return open_;
    }
    void Close(Bindings &bindings);

  private:
    bool open_ = false;
};

} // namespace storm::ui::screens
