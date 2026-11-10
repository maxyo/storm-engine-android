$input v_color0, v_albedo, v_vnormal, v_vpos, v_uv
// Fragment side of the fixed-function stand-in; the fx headers add their terms exactly as the emulator is
// expected to use them.
#include <bgfx_shader.sh>
#include "fx_lighting.sh"
#include "fx_coverage.sh"
#ifdef FX_AERIAL
#include "fx_aerial.sh"
#endif

SAMPLER2D(s_previewBase, 0);   // stage 0: base texture, alpha tested (foliage, rigging)
SAMPLER2D(s_previewDetail, 1); // stage 1 of GEOM_SERVICE_R::SetMaterial: the height texture as a detail layer
uniform vec4 u_previewLight[3]; // [1].w: size of the base texture in texels, [2].w: 1 = alpha to coverage instead of the alpha test

void main()
{
    vec4 base = texture2D(s_previewBase, v_uv);
    float alpha = 1.0;
    if (u_previewLight[2].w > 0.5)
        alpha = fxCoverageAlpha(base.a, 0.5, v_uv, vec2_splat(u_previewLight[1].w));
    else if (base.a < 0.5)
        discard;
    vec3 albedo = v_albedo.rgb * base.rgb;
    vec3 color = v_color0.rgb * base.rgb;
    // D3DTOP_MODULATE2X with the detail texture, as the original technique does.
    float detail = 2.0 * texture2D(s_previewDetail, v_uv).r;
#if defined(FX_LIGHT_PER_PIXEL) || defined(FX_LIGHT_POINT)
    vec3 N = normalize(v_vnormal);
    vec3 V = normalize(-v_vpos);
    // Two-sided surfaces: shade the side that faces the viewer.
    if (dot(N, V) < 0.0)
        N = -N;
#ifdef FX_LIGHT_NORMALMAP
    vec3 Ng = N;
    N = fxPerturbNormal(N, v_vpos, v_uv);
    if (v_albedo.a < 0.5)
        color *= fxBakedRelief(Ng, N);
#endif
    vec3 diffuse = vec3_splat(0.0);
    vec3 ambient = vec3_splat(0.0);
    vec3 specular = vec3_splat(0.0);
#ifdef FX_LIGHT_PER_PIXEL
    if (v_albedo.a > 0.5)
    {
        float shadow = fxSunShadow(v_vpos, N);
        diffuse = fxSunDiffuse(N, V) * shadow;
        ambient = fxAmbient(N);
        specular = fxSunSpecular(N, V) * shadow + fxRim(N, V);
    }
#endif
#ifdef FX_LIGHT_POINT
    // Lit geometry gets every selected light; baked geometry only receives the dynamic ones the tool selected.
    diffuse += fxPointDiffuse(N, v_vpos);
    if (v_albedo.a > 0.5)
        specular += fxPointSpecular(N, v_vpos, V);
#endif
    color = fxAddSpecular(fxLit(color, albedo, diffuse, albedo, ambient) * detail, specular);
#else
    color *= detail;
#endif
#ifdef FX_AERIAL
    color = fxAerial(color, v_vpos);
#endif
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), alpha);
}
