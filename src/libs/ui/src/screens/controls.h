// View builders shared by every screen: one widget (or one group of widgets) becomes the
// JSON of one row of the pack parts <s-control> / <s-block>, with its events bound.
#pragma once

#include "screen.h"

#include <functional>
#include <initializer_list>

namespace storm::ui::screens
{

std::string Upper(std::string_view text);
// A game text on one line: line breaks ("\r", "\n"), tabs and runs of spaces become one space, trimmed.
std::string OneLine(const std::string &text);
bool Short(const std::string &text, size_t chars); // at most `chars` code points, one line
size_t Length(const std::string &text);            // code points
// "{1}", "{2}" … of a string table entry replaced by the arguments.
std::string Fill(std::string text, std::initializer_list<std::string> args);

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
// The amount a quantity control sets is the player's draft until he confirms it (owner rule: picking an
// amount moves nothing). A change or a drag of the control moves only the draft; the deal's button
// («Взять / Отдать N» for a free transfer, «Купить / Продать N» for a money deal) sends it to the game,
// «Отмена» drops it. Every screen whose quantity moves something keeps one per open control: chests,
// bodies and the exchange with an officer (itemsbox), storage, town loot, the store and its kin
// (trade_commerce.cpp), the cargo and crew of the loot / exchange of ships (extra_transfer.cpp), the
// treasurer's standing order (extra_goods.cpp).
class AmountDraft
{
  public:
    struct Labels
    {
        const char *more; // string key of the operation that raises our side («Взять», «Купить»)
        const char *less; // … and of the one that lowers it («Отдать», «Продать»)
        bool absolute = false; // the button names the amount set, not the change («Сохранить 500» of an order)
    };
    // The control of `key` (a row) shows `origin` (ours) beside `other` (theirs) as the game has them now. A new
    // key, or figures the game changed (a move went through), open the draft again at the origin. A draft that
    // was sent waits for the game's answer (its figures changed, or kAnswer s pass: refused): the control keeps
    // showing it, and no second deal can be sent meanwhile. (Not the model revision: a move that goes through
    // the game's own window takes several, and the first ones still show the old figures.)
    void Follow(const std::string &key, int origin, int other, Bindings &bindings);
    void Close(); // the control is gone (the game closed its window)
    int Origin() const
    {
        return origin_;
    }
    int Other() const
    {
        return other_;
    }
    // The amount the control shows, within [low, high].
    int Value(int low, int high) const;
    // Binds `<id> change` and `<id> commit` of the control: both move the draft and build again, nothing goes
    // to the game. Past `high` a `more_reason` (the purse, the room) is said once per push.
    void Bind(Bindings &bindings, const std::string &id, int low, int high, const std::string &more_reason);
    // The deal's buttons while the draft differs from its origin: returns the primary {label "<operation> N",
    // sub, disabled, reason, busy}, whose press calls `commit(delta)` (delta > 0 raises our side), and appends
    // «Отмена» (id "deal-cancel") to `actions`, which drops the draft and calls `cancel` (closing the game's
    // window, where it has one). Null while the amount is unchanged; while a sent draft waits for its answer
    // the button stays, busy, and takes no press.
    Json Buttons(Bindings &bindings, Json &actions, int low, int high, Labels labels, const std::string &sub, bool disabled,
                 const std::string &reason, std::function<void(int)> commit, std::function<void()> cancel);
    // The deal was sent by other means (the quick take of a whole stack): wait for the answer like after Buttons.
    void Sent(int value, double now);
    // Drops a changed draft (the platform back); false when there was nothing to drop.
    bool Drop();

  private:
    static constexpr double kAnswer = 1.5; // s: how long a sent draft waits for the game's answer
    void Open(const std::string &key, int origin, int other);
    bool open_ = false;
    std::string key_;
    int origin_ = 0, other_ = 0;
    int target_ = 0;
    bool sent_ = false;
    double sent_at_ = 0.0;
    double reason_at_ = -10.0; // when the "cannot go further" reason was last said
};
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
