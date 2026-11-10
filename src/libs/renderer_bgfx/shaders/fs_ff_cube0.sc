$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
#define STAGE0_CUBE 1
#define STAGE0_SAMPLE textureCube(s_cube0, v_texcoord0.xyz)
#include "ff_fragment.sh"
