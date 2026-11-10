// View builders shared by every screen: one widget (or one group of widgets) becomes the
// JSON of one row of the pack parts <s-control> / <s-block>, with its events bound.
#pragma once

#include "screen.h"

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

// {id, label, variant, disabled, reason, busy} of a c-button; a tap presses the widget.
Json ButtonView(Bindings &bindings, const Widget &button, const std::string &id, const char *variant);
// {kind:"action", id, title, disabled, reason, selected, busy}: a button or choice as a list row.
Json ActionRow(Bindings &bindings, const Widget &widget, const std::string &id);
// Sends the verb a tap on a choice row means: activate on the selected row when offered,
// else select, else open.
void TapChoice(Bindings &bindings, const Widget &choice);
// Appends the rows of a run of controls (check, edit, slider, stepper) to `rows`.
// `labels` (empty, or one per widget) holds the short text that preceded a control and names it.
void ControlRows(Bindings &bindings, std::span<const Widget *const> widgets, const std::string &id_prefix,
                 const std::vector<std::string> &labels, Json &rows);

} // namespace storm::ui::screens
