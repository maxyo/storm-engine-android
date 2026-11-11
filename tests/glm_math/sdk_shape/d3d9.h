#pragma once
// Math's old packed Matrix header only needs this type's declaration. The
// row-vector test uses its documented 16-float shape, without a graphics SDK
// or a rendering claim. Engine builds take D3DMATRIX from storm_render_types.h.
struct D3DMATRIX { float m[4][4]; };
