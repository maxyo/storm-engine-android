#pragma once

#include <d3d9.h>

#define D3DX_DEFAULT ((UINT)-1)

// Our replacement of the D3DX helper the renderer and the screenshot code use
HRESULT D3DXLoadSurfaceFromSurface(IDirect3DSurface9 *dst_surface,
        const PALETTEENTRY *dst_palette, const RECT *dst_rect, IDirect3DSurface9 *src_surface,
        const PALETTEENTRY *src_palette, const RECT *src_rect, DWORD filter, D3DCOLOR color_key);

