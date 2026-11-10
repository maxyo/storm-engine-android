$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog, v_normal, v_position, v_matDiffuse, v_matAmbient
// Per-pixel lighting of lit fixed-function draws (src/effects/lighting.cpp). Every lighting feature the presets
// can switch at run time without a cost worth a variant is compiled in and neutralised by zero strength; the
// variants (cmake/storm_shaders.cmake, kFfLitVariants) add FX_LIGHT_SPECULAR, FX_LIGHT_LINEAR.
#define FX_FF_LIT 1
#define FX_FF_COVERAGE 1
#define FX_LIGHT_PER_PIXEL 1
#define FX_LIGHT_HEMISPHERE 1
#define FX_LIGHT_TRANSLUCENT 1
#define FX_LIGHT_RIM 1
#define FX_LIGHT_POINT 1
#define FX_LIGHT_POINT_MAX 8
#include "ff_fragment.sh"
