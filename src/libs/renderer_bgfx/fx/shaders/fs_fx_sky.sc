$input v_dir
// Analytic sky. Variant FX_SKY_CLOUDS adds the procedural cloud deck.
#include "fx_atmosphere.sh"

void main()
{
    vec3 d = normalize(v_dir);
    vec3 c = fxSkyColor(d) + fxSunHalo(d);
    float disc = smoothstep(u_fxSky[0].w - 0.00004, u_fxSky[0].w + 0.00002, dot(d, u_fxSky[0].xyz));
    c += u_fxSky[3].rgb * disc * step(0.0, d.y);
#ifdef FX_SKY_CLOUDS
    c = fxClouds(d, c);
#endif
    // Gradients this smooth band in 8 bits; half a step of ordered noise hides it.
    gl_FragColor = vec4(fxEncode(c) + vec3_splat((fxNoise(gl_FragCoord.xy) - 0.5) * u_fxSky[3].w), 1.0);
}
