$input v_color0, v_albedo, v_vnormal, v_vpos
// Fragment side of the fixed-function stand-in; the fx lighting header adds
// the per-pixel terms exactly as the emulator is expected to use it.
#include <bgfx_shader.sh>
#include "fx_lighting.sh"

void main()
{
    vec3 color = v_color0.rgb;
#if defined(FX_LIGHT_PER_PIXEL) || defined(FX_LIGHT_POINT)
    vec3 N = normalize(v_vnormal);
    vec3 V = normalize(-v_vpos);
    // Two-sided surfaces: shade the side that faces the viewer.
    if (dot(N, V) < 0.0)
        N = -N;
    vec3 diffuse = vec3_splat(0.0);
    vec3 specular = vec3_splat(0.0);
#ifdef FX_LIGHT_PER_PIXEL
    if (v_albedo.a > 0.5)
    {
        float shadow = fxSunShadow(v_vpos, N);
        diffuse = fxSunDiffuse(N, V) * shadow + fxAmbient(N);
        specular = fxSunSpecular(N, V) * shadow;
    }
#endif
#ifdef FX_LIGHT_POINT
    // Lit geometry gets every selected light; baked geometry only receives the dynamic ones the tool selected.
    diffuse += fxPointDiffuse(N, v_vpos);
    if (v_albedo.a > 0.5)
        specular += fxPointSpecular(N, v_vpos, V);
#endif
    color += v_albedo.rgb * diffuse + specular;
#endif
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
