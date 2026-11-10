$input a_position, a_normal, a_color0, a_color1
$output v_color0, v_albedo, v_vnormal, v_vpos
// Stand-in for the fixed-function vertex pipeline. Two kinds of geometry, as in the game:
//   lit    D3DRS_LIGHTING on: one directional light, a global ambient and up to eight point / spot lights,
//          per vertex unless the fx lighting flags take terms over (see the contract in fx_lighting.sh);
//   baked  D3DRS_LIGHTING off: a_color1 is the lighting baked into the vertex colours (src/libs/lighter).
// a_color0 plays the role of the texture.
#include <bgfx_shader.sh>

// [0] xyz: view-space direction to the sun, w: 1 lit / 0 baked  [1] sun diffuse  [2] ambient
uniform vec4 u_previewLight[3];
#ifndef FX_LIGHT_POINT
uniform vec4 u_previewPoint[32]; // same layout as u_fxPoint
uniform vec4 u_previewPointCount;
#endif

void main()
{
    vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
    gl_Position = mul(u_proj, viewPos);
    vec3 N = normalize(mul(u_modelView, vec4(a_normal, 0.0)).xyz);
    vec3 light = a_color1.rgb;
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
#ifndef FX_LIGHT_POINT
        // D3D9 fixed-function point and spot lights.
        for (int i = 0; i < 8; ++i)
        {
            if (float(i) >= u_previewPointCount.x)
                break;
            vec4 position = u_previewPoint[4 * i];
            vec4 params = u_previewPoint[4 * i + 2];
            vec4 spot = u_previewPoint[4 * i + 3];
            vec3 toLight = position.xyz - viewPos.xyz;
            float dist = length(toLight);
            vec3 L = toLight / max(dist, 1e-5);
            float attenuation = dist > position.w ? 0.0 : 1.0 / max(u_previewPoint[4 * i + 1].w + params.x * dist + params.y * dist * dist, 1e-6);
            float cone = pow(clamp(dot(spot.xyz, -L) * params.z + params.w, 0.0, 1.0), spot.w);
            light += u_previewPoint[4 * i + 1].rgb * (max(dot(N, L), 0.0) * attenuation * cone);
        }
#endif
    }
    v_color0 = vec4(a_color0.rgb * light, 1.0);
    v_albedo = vec4(a_color0.rgb, u_previewLight[0].w);
    v_vnormal = N;
    v_vpos = viewPos.xyz;
}
