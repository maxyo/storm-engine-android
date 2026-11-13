$input v_uv
// One step of fx::SeaWaves: the discrete 2D wave equation (explicit leapfrog of the classic ripple scheme) with
// velocity damping, a sponge along the border and this frame's sources.
#include "fx_waves.sh"

#define kWavesSources 16
// Per source: [0] xy position (metres from the centre), zw forward; [1] x half length, y half width (metres),
// z speed (m/s), w heave (m/s up); [2] x kind (0 body, 1 hull), y stroke (0..1), z phase (radians)
uniform vec4 u_wavesSrc[kWavesSources * 3];

void main()
{
    if (u_wavesStep[2].y > 0.5)
    {
        gl_FragColor = fxWavesOut(vec2_splat(0.0));
        return;
    }
    float texel = u_wavesStep[1].x;
    float dt = u_wavesStep[1].z;
    vec2 uv = v_uv + u_wavesStep[0].xy;
    vec2 c = fxWavesState(uv);
    float lap = fxWavesState(uv - vec2(texel, 0.0)).x + fxWavesState(uv + vec2(texel, 0.0)).x +
                fxWavesState(uv - vec2(0.0, texel)).x + fxWavesState(uv + vec2(0.0, texel)).x - 4.0 * c.x;
    float h = c.x + (c.x - c.y) * u_wavesStep[0].w + u_wavesStep[0].z * lap;

    // Sources: a displacement rate (m/s) each, and the still water inside the hulls.
    vec2 p = (v_uv - 0.5) * u_wavesStep[2].w;
    float rate = 0.0;
    float hold = 1.0;
    float time = u_wavesStep[2].x;
    for (int i = 0; i < kWavesSources; ++i)
    {
        if (float(i) >= u_wavesStep[1].w)
            break;
        vec4 a = u_wavesSrc[i * 3];
        vec4 b = u_wavesSrc[i * 3 + 1];
        vec4 k = u_wavesSrc[i * 3 + 2];
        vec2 d = p - a.xy;
        vec2 q = vec2(dot(d, a.zw) / b.x, dot(d, vec2(-a.w, a.z)) / b.y);
        float e = dot(q, q);
        if (e > 4.0)
            continue;
        if (k.x < 0.5)
        {
            // A body: pushes down where it moves and, swimming, strokes at about 0.9 Hz.
            float g = exp(-2.5 * e);
            rate -= g * (0.10 * abs(b.z) + 0.30 * k.y * sin(5.65 * time + k.z));
        }
        else
        {
            // A hull: the water inside its waterline stays still (incoming waves reflect off it); around the
            // waterline it is pushed by the hull's speed, up at the bow and down along the run aft, and by its heave.
            hold *= smoothstep(0.45, 0.8, e);
            float ring = exp(-(e - 1.0) * (e - 1.0) * 6.0);
            float bow = clamp(0.5 + 0.5 * q.x, 0.0, 1.0);
            // Heave only from the hull's own bobbing beyond the swell's (a storm lifts every hull metres a second):
            // a small push, clamped, or every hull rings like a bell.
            rate += ring * (abs(b.z) * (0.2 * bow * bow * bow - 0.04) - 0.06 * clamp(b.w, -1.0, 1.0));
        }
    }
    h += rate * dt;
    h *= hold;
    // Sponge: the outer 8 % of the field absorbs what reaches it.
    vec2 edge = min(v_uv, 1.0 - v_uv);
    h *= mix(0.94, 1.0, smoothstep(0.0, 0.08, min(edge.x, edge.y)));
    h = clamp(h, -0.95 * kWavesRange, 0.95 * kWavesRange);
    gl_FragColor = fxWavesOut(vec2(h, c.x * hold));
}
