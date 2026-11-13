$input v_uv
// The surface of fx::SeaWaves for the sea shaders (shaders/include/sea_waves.sh): RGBA8, linear filtering on every
// device. R: height (+-0.5 m), GB: slope in x / z (+-1), A: crest foam (0..1), zero at 128/255. Fades to flat
// water at the border.
#include "fx_waves.sh"

void main()
{
    float texel = u_wavesStep[1].x;
    float h = fxWavesState(v_uv).x;
    float l = fxWavesState(v_uv - vec2(texel, 0.0)).x;
    float r = fxWavesState(v_uv + vec2(texel, 0.0)).x;
    float d = fxWavesState(v_uv - vec2(0.0, texel)).x;
    float u = fxWavesState(v_uv + vec2(0.0, texel)).x;
    vec2 slope = vec2(r - l, u - d) / (2.0 * u_wavesStep[1].y);
    // Foam where a crest is high and sharp (it would break), from the curvature of the field.
    float curvature = (4.0 * h - l - r - d - u) / (u_wavesStep[1].y * u_wavesStep[1].y);
    float foam = clamp((h - 0.12) * 8.0, 0.0, 1.0) * clamp(curvature * 1.5, 0.0, 1.0);
    vec2 edge = min(v_uv, 1.0 - v_uv);
    float fade = smoothstep(0.02, 0.15, min(edge.x, edge.y));
    // Zero is 128/255, exact in 8 bits (0.5 is not: a flat field would decode to a slight tilt everywhere).
    gl_FragColor = vec4(clamp(h * fade + 128.0 / 255.0, 0.0, 1.0), clamp(slope * fade * 0.5 + 128.0 / 255.0, 0.0, 1.0),
                        foam * fade);
}
