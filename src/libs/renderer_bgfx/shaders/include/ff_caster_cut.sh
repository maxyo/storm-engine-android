// The alpha test of a caster whose material cuts holes (src/effects/casters.hpp): the product of the alphas of the
// texture stages the material's own alpha chain multiplies (stage 0 of a location model or a rope; stages 0 and 2 of
// a sail: the cloth and its tears), compared with the draw's alpha reference as the colour pass does. The stages'
// textures are the draw's own, still bound.
#ifndef FF_CASTER_CUT_SH
#define FF_CASTER_CUT_SH

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
uniform vec4 u_fxCasterCut; // x: the alpha reference (0..1); y, z, w: 1 = the alpha of stage 0, 1, 2 takes part
uniform vec4 u_fxCasterUv;  // x, y, z: the texture coordinate set (0..2) of stage 0, 1, 2

vec2 ffCasterUv(float set, vec2 uv0, vec2 uv1, vec2 uv2)
{
    return set < 0.5 ? uv0 : (set < 1.5 ? uv1 : uv2);
}

// True where the material is cut away: nothing is cast there. uv0..2: v_texcoord0..2.xy (stage inputs, passed in:
// storm_common.sh).
bool ffCasterCutAt(vec2 uv0, vec2 uv1, vec2 uv2)
{
    float alpha = 1.0;
    if (u_fxCasterCut.y > 0.5)
        alpha *= texture2D(s_tex0, ffCasterUv(u_fxCasterUv.x, uv0, uv1, uv2)).a;
    if (u_fxCasterCut.z > 0.5)
        alpha *= texture2D(s_tex1, ffCasterUv(u_fxCasterUv.y, uv0, uv1, uv2)).a;
    if (u_fxCasterCut.w > 0.5)
        alpha *= texture2D(s_tex2, ffCasterUv(u_fxCasterUv.z, uv0, uv1, uv2)).a;
    return alpha <= u_fxCasterCut.x;
}
#define ffCasterCut() ffCasterCutAt(v_texcoord0.xy, v_texcoord1.xy, v_texcoord2.xy)
#endif
