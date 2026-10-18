#pragma once
// Math's old packed Matrix header only needs this type's declaration. The
// row-vector test uses its documented 16-float shape, without a graphics SDK
// or a rendering claim. Full-engine builds continue using real D3D9 headers.
struct _D3DMATRIX { float m[4][4]; };
typedef _D3DMATRIX D3DMATRIX;
