$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_normal, v_position, v_matDiffuse, v_matAmbient
// Geometry with baked vertex colours (lighting off) under the per-pixel lighting presets: dynamic point lights
// only (src/effects/lighting.cpp). The variant fs_ff_baked_l adds FX_LIGHT_LINEAR.
// The inputs are those of vs_ff_lit (bgfx links only programs whose varyings match exactly).
#define FX_FF_BAKED 1
#define FX_FF_COVERAGE 1
#define FX_LIGHT_POINT 1
#define FX_LIGHT_POINT_MAX 8
#include "ff_fragment.sh"
