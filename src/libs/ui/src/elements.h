// Elements drawn by code: <ui-icon>, <ui-ring>, <ui-paper>, <ui-clamp>, <ui-wheel>, <ui-game-image>.
#pragma once

#include "storm_ui/render_backend.h"
#include "storm_ui/ui.h"
#include "template.h"

#include <RmlUi/Core.h>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::ui
{

void RegisterDrawElements();
// True once after a <ui-clamp> changed its text during layout: lay out again before drawing.
bool TextClampsChanged();
// Scroll edges: every element that scrolls (overflow-y auto or scroll) gets the class
// scroll-below while content is hidden below its bottom edge, and a child div.scroll-fade
// pinned to the bottom of its visible part, which the pack draws as a fade (base.rcss).
// True when something changed (the layout needs another update).
bool ScrollEdgesChanged(Rml::Element *root);
// Whether <ui-clamp> text inside `root` was cut to fit: 1 cut, 0 all of it shown, -1 none there.
int ClampedTextIn(Rml::Element *root);
void ReleaseDrawTextures();

// icons.json of the UI pack: shared defs plus one SVG symbol per icon id.
void LoadIconPack(const Json &pack);
void RegisterIconElement();
// Symbol id of an icon name ("ship" → "i-ship"; c-* and nation-* are full colour).
std::string IconSymbolId(std::string name, bool &plain);
bool HasIcon(const std::string &name);
void ReleaseIconTextures();

void RegisterGameImageElement();
// UVs (u0, v0, u1, v1) of atlas cell 1…grid² (0 = the whole texture); false when the cell
// does not exist. `uv` may be null to only check.
bool AtlasCell(int cell, int grid, float uv[4]);
void SetGameImageRoot(const std::string &game_root);
// The renderer that supplies game pictures (RenderBackend::AcquireGamePicture).
void SetGameImageRenderer(RenderBackend *renderer);
// True once after a picture changed state: the host draws another frame.
bool GameImagesChanged();
// True while a picture changed state and that is not yet taken: the host must keep drawing
// frames to show it (nothing else wakes an idle host).
bool GameImagesPending();
// Adds what acquiring game pictures cost since the last call (milliseconds, pictures) and resets it.
void TakeGameImageCost(double &ms, int &count);
void ShutdownGameImages();

} // namespace storm::ui
