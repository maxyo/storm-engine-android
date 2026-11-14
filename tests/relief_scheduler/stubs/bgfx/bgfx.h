#pragma once
#include <cstdint>
namespace bgfx { struct TextureHandle { uint16_t idx; }; }
#define BGFX_INVALID_HANDLE {UINT16_MAX}
