// The ripple state of fx::SeaWaves: height now (x) and one step ago (y), RG16F or packed into RGBA8 (16-bit fixed
// point per height, +-kWavesRange metres). Read with point sampling at texel centres only.
#ifndef FX_WAVES_SH
#define FX_WAVES_SH
#include "fx_common.sh"

#define kWavesRange 2.0

SAMPLER2D(s_wavesState, 0);
// [0] xy: uv offset of the previous state (the centre moved), z: (c dt / dx)^2, w: velocity kept per step
// [1] x: 1 / size, y: metres per texel, z: dt, w: sources
// [2] x: time (s), y: 1 = write still water, z: 1 = packed RGBA8 state, w: metres the field covers
uniform vec4 u_wavesStep[3];

float fxWavesUnpack(vec2 hl)
{
    float q = dot(floor(hl * 255.0 + 0.5), vec2(256.0, 1.0)) / 65535.0;
    return (q * 2.0 - 1.0) * kWavesRange;
}

vec2 fxWavesPack(float h)
{
    float q = floor(clamp(h / kWavesRange * 0.5 + 0.5, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(q / 256.0);
    return vec2(hi, q - hi * 256.0) / 255.0;
}

// (height now, height one step ago); zero outside the field.
vec2 fxWavesState(vec2 uv)
{
    vec4 t = texture2D(s_wavesState, uv);
    vec2 h = u_wavesStep[2].z > 0.5 ? vec2(fxWavesUnpack(t.rg), fxWavesUnpack(t.ba)) : t.rg;
    vec2 inside = step(vec2_splat(0.0), uv) * step(uv, vec2_splat(1.0));
    return h * (inside.x * inside.y);
}

vec4 fxWavesOut(vec2 h)
{
    return u_wavesStep[2].z > 0.5 ? vec4(fxWavesPack(h.x), fxWavesPack(h.y)) : vec4(h, 0.0, 1.0);
}
#endif
