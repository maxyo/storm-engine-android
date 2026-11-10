$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
#define STAGE2_CUBE 1
#define STAGE2_SAMPLE textureCube(s_cube2, v_texcoord2.xyz)
#include "ff_fragment.sh"
