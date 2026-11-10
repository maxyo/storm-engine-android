$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Fixed-function fragment variant for D3DRS_SLOPESCALEDEPTHBIAS (bgfx has no polygon offset):
// writes the depth with the slope term, so it is used only by the draws that set it.
#define STORM_SLOPE_BIAS 1
#include "ff_fragment.sh"
