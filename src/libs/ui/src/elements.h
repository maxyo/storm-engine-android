// Elements drawn by code: <ui-icon>, <ui-ring>, <ui-paper>, <ui-game-image>.
#pragma once

#include "template.h"

#include <RmlUi/Core.h>
#include <cstdint>
#include <string>
#include <vector>

namespace storm::ui
{

void RegisterDrawElements();
void ReleaseDrawTextures();

// icons.json of the UI pack: shared defs plus one SVG symbol per icon id.
void LoadIconPack(const Json &pack);
void RegisterIconElement();
void ReleaseIconTextures();

struct TxImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // premultiplied alpha, top row first
};
// Decodes a Storm engine .tx texture (top mip). Returns false on unknown data.
bool DecodeTx(const uint8_t *data, size_t size, TxImage &out, std::string *error = nullptr);

void RegisterGameImageElement();
void SetGameImageRoot(const std::string &game_root);
// True once after a background decode finished: the host draws another frame.
bool GameImagesChanged();
void ShutdownGameImages();

} // namespace storm::ui
