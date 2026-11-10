// Elements drawn by code: <ui-icon>, <ui-ring>, <ui-paper>, <ui-clamp>, <ui-wheel>, <ui-game-image>.
#pragma once

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

using TxImage = Picture; // premultiplied alpha, top row first
// Decodes a Storm engine .tx texture (top mip). Returns false on unknown data.
bool DecodeTx(const uint8_t *data, size_t size, TxImage &out, std::string *error = nullptr);

void RegisterGameImageElement();
// UVs (u0, v0, u1, v1) of atlas cell 1…grid² (0 = the whole texture); false when the cell
// does not exist. `uv` may be null to only check.
bool AtlasCell(int cell, int grid, float uv[4]);
void SetGameImageRoot(const std::string &game_root);
void SetGameImageLoader(std::function<bool(const std::string &, Picture &)> loader);
// True once after a background decode finished: the host draws another frame.
bool GameImagesChanged();
void ShutdownGameImages();

} // namespace storm::ui
