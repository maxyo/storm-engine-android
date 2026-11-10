$input a_position, a_normal, a_color0
$output v_color0, v_albedo, v_vnormal, v_vpos
// Stand-in for the fixed-function vertex pipeline: one directional light and
// a global ambient, lit per vertex unless the fx lighting flags take over
// (see the contract in fx_lighting.sh).
#include <bgfx_shader.sh>

uniform vec4 u_previewLight[3]; // [0] xyz: view-space direction to the sun, w: 1 lit / 0 unlit  [1] sun diffuse  [2] ambient

void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_proj, viewPos);
    vec3 N = normalize(mul(u_modelView, vec4(a_normal, 0.0)).xyz);
    vec3 light = vec3_splat(1.0);
    if (u_previewLight[0].w > 0.5)
    {
#ifdef FX_LIGHT_PER_PIXEL
        light = vec3_splat(0.0);
#else
        light = u_previewLight[1].rgb * max(dot(N, u_previewLight[0].xyz), 0.0);
#endif
#ifndef FX_LIGHT_HEMISPHERE
        light += u_previewLight[2].rgb;
#endif
    }
    v_color0 = vec4(a_color0.rgb * light, 1.0);
    v_albedo = vec4(a_color0.rgb, u_previewLight[0].w);
    v_vnormal = N;
    v_vpos = viewPos.xyz;
}
