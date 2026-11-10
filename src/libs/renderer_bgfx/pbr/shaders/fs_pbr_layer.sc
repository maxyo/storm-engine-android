$input v_texcoord0, v_fog, v_color1, v_normal, v_position
// Three PBR layers splatted over the base by an RGB mask (variant Layer).
#define PBR_NORMAL_MAP 1
#define PBR_LAYER 1
#include "pbr_shading.sh"
