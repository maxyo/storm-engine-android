// Waves from objects on the Sea2 passes (effects::Host::prepare_sea sets everything here):
// - ripples: the surface of fx::SeaWaves (fx/include/storm/fx/sea_waves.hpp) around the camera, RGBA8 (R height
//   +-0.5 m, GB slope in x / z +-1, zero at 128/255, A crest foam), in the normals, the crest foam and (lift) the vertices;
// - Kelvin wakes behind the ships under way: the transverse and divergent waves of the classic stationary-phase
//   solution for a point disturbance in deep water (Lord Kelvin 1887; e.g. Lighthill, Waves in Fluids, 3.10): inside
//   the 19.47-degree V, a point at x behind the bow and y to the side gets two waves whose propagation angles theta
//   solve tan(theta) / (1 + 2 tan^2(theta)) = y / x; each has the phase k0 (x + y tan) / cos (k0 = g / U^2) and the
//   wave vector k0 / cos * (1, tan). Their sum is evaluated per pixel, band-limited by the phase change per pixel.
// Positions are those of the sea's vertices (a_position: world minus the sea's own camera offset, small numbers).
#ifndef SEA_WAVES_SH
#define SEA_WAVES_SH

SAMPLER2D(s_seaWaves, 15); // unit of fx::kShadowMapStage: never bound on a Sea2 draw
// [0] xy: field uv at the sea's vertex origin, z: 1 / metres the field covers (uv = pos.xz * z + xy), w: lift (0 off)
// [1] x: ripple normals (0 off, else their strength), y: crest foam strength, z: wakes, w: metres per field texel
// [2] x: debug view (fx_sea_waves_debug)
uniform vec4 u_seaWaves[3];
#define kSeaWakes 4
// Per wake: [0] xy: bow (sea vertex space, metres), zw: unit forward; [1] x: k0 = g / U^2 (rad/m), y: length (m),
// z: wave height (m), w: half beam (m)
uniform vec4 u_seaWakes[kSeaWakes * 2];

vec2 seaWavesUv(vec2 pos)
{
    return pos * u_seaWaves[0].z + u_seaWaves[0].xy;
}

float seaWavesInside(vec2 uv)
{
    vec2 inside = step(vec2_splat(0.0), uv) * step(uv, vec2_splat(1.0));
    return inside.x * inside.y;
}

#if BGFX_SHADER_TYPE_VERTEX
// Metres the ripples raise the sea's vertex at `pos`.
float seaWavesLift(vec2 pos)
{
    if (u_seaWaves[0].w <= 0.0)
        return 0.0;
    vec2 uv = seaWavesUv(pos);
    return (texture2DLod(s_seaWaves, uv, 0.0).r - 128.0 / 255.0) * seaWavesInside(uv) * u_seaWaves[0].w;
}
#endif

#if BGFX_SHADER_TYPE_FRAGMENT
// Slope of one wake at `pos` (xy, world x / z) and how much of it the band limit took (z, 0..1 of a wave).
vec3 seaWake(vec4 a, vec4 b, vec2 pos, float pixel)
{
    vec2 d = pos - a.xy;
    float x = -dot(d, a.zw);                // metres behind the bow
    float y = dot(d, vec2(-a.w, a.z));      // metres to the side
    if (x <= 0.0 || x >= b.y)
        return vec3_splat(0.0);
    float ay = abs(y);
    float t = ay / x;                       // tangent of the angle off the track
    float disc = 1.0 - 8.0 * t * t;         // zero on the cusp line (19.47 degrees)
    if (disc <= -0.06)
        return vec3_splat(0.0);
    float root = sqrt(max(disc, 0.0));
    float tt = 2.0 * t / (1.0 + root);      // transverse waves: tan(theta)
    float td = (1.0 + root) / max(4.0 * t, 1e-3); // divergent waves
    float k0 = b.x;
    float ct = sqrt(1.0 + tt * tt), cd = sqrt(1.0 + td * td); // 1 / cos(theta)
    float phase_t = k0 * (x + ay * tt) * ct;
    float phase_d = k0 * (x + ay * td) * cd;
    vec2 kt = k0 * ct * vec2(1.0, tt);      // wave vectors in (behind, side)
    vec2 kd = k0 * cd * vec2(1.0, td);
    // Height: spreads as 1 / sqrt(distance in wavelengths), peaks along the cusp lines (the caustic of the two
    // systems, disc^-1/4), dies over the wake's length and close to the hull; the divergent waves vanish on the track.
    float life = 1.0 - x / b.y;
    float spread = b.z * life * life * inversesqrt(1.0 + 0.05 * k0 * x) * smoothstep(0.5 * b.w, 3.0 * b.w, x);
    float edge = smoothstep(-0.06, 0.01, disc);
    float cusp = min(pow(max(disc, 0.0) + 0.03, -0.25), 2.2) * edge;
    float ht = 0.55 * spread * edge;
    float hd = spread * cusp * smoothstep(0.02, 0.12, t);
    // Band limit: a wave whose phase turns by more than about a radian per pixel fades to its mean.
    float ft = 1.0 - smoothstep(1.0, 3.0, length(kt) * pixel);
    float fd = 1.0 - smoothstep(1.0, 3.0, length(kd) * pixel);
    vec2 slope = -(ht * ft * sin(phase_t)) * kt - (hd * fd * sin(phase_d)) * kd;
    vec2 back = -a.zw, side = vec2(-a.w, a.z) * (y < 0.0 ? -1.0 : 1.0);
    float lost = (ht * length(kt) * (1.0 - ft) + hd * length(kd) * (1.0 - fd));
    return vec3(slope.x * back + slope.y * side, lost);
}

// The world normal `n` of the Sea2 pixel with the ripples and wakes at the sea position t4.zw (field uv t4.xy) on it;
// `foam`: crest foam there (0..1), `rough`: the wake detail the band limit took (brightens the far wake a little).
// `wakes`: false leaves the wakes out (the sun road pass: its glints come from the ripples, the wakes' cost once).
vec3 seaWavesNormal(vec3 n, vec4 t4, float pixel, bool wakes, out float foam, out float rough, out vec3 debug)
{
    vec2 slope = vec2_splat(0.0);
    vec2 wake = vec2_splat(0.0);
    foam = 0.0;
    rough = 0.0;
    if (u_seaWaves[1].x > 0.0)
    {
        vec4 s = texture2D(s_seaWaves, t4.xy);
        float keep = seaWavesInside(t4.xy) * (1.0 - smoothstep(1.0, 4.0, pixel / u_seaWaves[1].w));
        slope += (s.gb - 128.0 / 255.0) * 2.0 * keep * u_seaWaves[1].x;
        foam = s.a * keep * u_seaWaves[1].y;
    }
    for (int i = 0; i < kSeaWakes; ++i)
    {
        if (!wakes || float(i) >= u_seaWaves[1].z)
            break;
        vec3 w = seaWake(u_seaWakes[i * 2], u_seaWakes[i * 2 + 1], t4.zw, pixel);
        wake += w.xy;
        rough += w.z;
    }
    debug = vec3(length(slope) * 4.0, length(wake) * 4.0, foam);
    slope += wake;
    return normalize(normalize(n) + vec3(-slope.x, 0.0, -slope.y));
}
#endif

#endif
