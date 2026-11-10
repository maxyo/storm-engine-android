$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
#define STAGE1_CUBE 1
#define STAGE1_SAMPLE textureCube(s_cube1, v_texcoord1.xyz)
#include "ff_fragment.sh"
