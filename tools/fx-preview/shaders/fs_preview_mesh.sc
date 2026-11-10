$input v_color0, v_albedo, v_vnormal, v_vpos
// Fragment side of the fixed-function stand-in; the fx lighting header adds
// the per-pixel terms exactly as the emulator is expected to use it.
#include <bgfx_shader.sh>
#include "fx_lighting.sh"

void main()
{
    vec3 color = v_color0.rgb;
#ifdef FX_LIGHT_PER_PIXEL
    if (v_albedo.a > 0.5)
    {
        vec3 N = normalize(v_vnormal);
        vec3 V = normalize(-v_vpos);
        // Two-sided surfaces: shade the side that faces the viewer.
        if (dot(N, V) < 0.0)
            N = -N;
        color += v_albedo.rgb * (fxSunDiffuse(N, V) + fxAmbient(N)) + fxSunSpecular(N, V);
    }
#endif
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
