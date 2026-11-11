// GPU skinning of GEOS::AVERTEX0 vertices (one blend weight, two bone indices) exactly like the former CPU kernel
// (src/libs/model/src/skinning.hpp). Included by ff_vertex.sh (FF_SKIN), vs_ff_caster_skin.sc and the Caribbean
// Legend PBR vertex stage (pbr/shaders/vs_pbr_skin.sc).
#ifndef FF_SKIN_SH
#define FF_SKIN_SH
// The palette holds 3 texels per bone (row = bone): texel k = (m[k], m[4+k], m[8+k], m[12+k]) of the bone
// matrix, texel 0 negated (the CPU kernel negates elements 0, 4, 8 and 12 after blending); see
// src/libs/model/include/skinning_palette.hpp.
SAMPLER2D(s_bones, 13);

vec4 blendedRow(int row, int bone0, int bone1, float weight)
{
    return texelFetch(s_bones, ivec2(row, bone0), 0) * weight + texelFetch(s_bones, ivec2(row, bone1), 0) * (1.0 - weight);
}

// Position and normal alike, the normal with the translation included (as the CPU kernel did).
vec3 skinPoint(vec4 r0, vec4 r1, vec4 r2, vec3 v)
{
    return vec3((r0.x * v.x + r0.y * v.y) + (r0.z * v.z + r0.w),
                (r1.x * v.x + r1.y * v.y) + (r1.z * v.z + r1.w),
                (r2.x * v.x + r2.y * v.y) + (r2.z * v.z + r2.w));
}
#endif
