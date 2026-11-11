$input a_position, a_normal, a_texcoord0, a_texcoord1, a_weight, a_indices
$output v_texcoord0, v_fog, v_color1, v_normal, v_position
// GPU-skinned models (GEOS::AVERTEX0 with the bone palette, as vs_ff_skin).
#define PBR_SKIN 1
#include "pbr_vertex.sh"
