$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// fs_ff with alpha to coverage instead of the alpha-test discard (src/effects/lighting.cpp).
#define FX_FF_COVERAGE 1
#include "ff_fragment.sh"
