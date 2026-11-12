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

vec2 ffCasterUv(float set)
{
    return set < 0.5 ? v_texcoord0.xy : (set < 1.5 ? v_texcoord1.xy : v_texcoord2.xy);
}

// True where the material is cut away: nothing is cast there.
bool ffCasterCut()
{
    float alpha = 1.0;
    if (u_fxCasterCut.y > 0.5)
        alpha *= texture2D(s_tex0, ffCasterUv(u_fxCasterUv.x)).a;
    if (u_fxCasterCut.z > 0.5)
        alpha *= texture2D(s_tex1, ffCasterUv(u_fxCasterUv.y)).a;
    if (u_fxCasterCut.w > 0.5)
        alpha *= texture2D(s_tex2, ffCasterUv(u_fxCasterUv.z)).a;
    return alpha <= u_fxCasterCut.x;
}
#endif
