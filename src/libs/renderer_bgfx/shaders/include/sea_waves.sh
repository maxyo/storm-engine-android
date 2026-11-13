// Waves from objects on the Sea2 passes (effects::Host::prepare_sea sets everything here):
// - ripples: the surface of fx::SeaWaves (fx/include/storm/fx/sea_waves.hpp) around the camera (R height, GB slope in
//   x / z, A crest foam; RGBA16F as they are, or RGBA8 around 128/255), in the normals, the Fresnel, the crest foam
//   and (lift) the vertices. The pixel shaders read it as a cubic B-spline (four linear taps): the slopes, and with
//   them the reflection, are smooth across the 0.5 m texels instead of showing their bilinear facets;
// - Kelvin wakes behind the ships under way: the transverse and divergent waves of the classic stationary-phase
//   solution for a point disturbance in deep water (Lord Kelvin 1887; e.g. Lighthill, Waves in Fluids, 3.10): inside
//   the 19.47-degree V, a point at x behind the bow and y to the side gets two waves whose propagation angles theta
//   solve tan(theta) / (1 + 2 tan^2(theta)) = y / x; each has the phase k0 (x + y tan) / cos (k0 = g / U^2) and the
//   wave vector k0 / cos * (1, tan). Their sum is evaluated per pixel, band-limited by the phase change per pixel;
//   the churned water along the track behind the hull is foam.
// Positions are those of the sea's vertices (a_position: world minus the sea's own camera offset, small numbers).
#ifndef SEA_WAVES_SH
#define SEA_WAVES_SH

SAMPLER2D(s_seaWaves, 15); // unit of fx::kShadowMapStage: never bound on a Sea2 draw
// [0] xy: field uv at the sea's vertex origin, z: 1 / metres the field covers (uv = pos.xz * z + xy), w: lift (0 off)
// [1] x: ripple normals (0 off, else their strength), y: crest foam strength, z: wakes, w: metres per field texel
// [2] x: debug view (fx_sea_waves_debug), y: 1 = RGBA8 surface, z: the sea's relative refractive index (its
//     vertex Fresnel's Sea2.Frenel), w: field texels per side
// [3] xy: foam texture uv at the sea's vertex origin, z: foam texture repeats per metre, w: 0
uniform vec4 u_seaWaves[4];
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

// A surface texel as metres of height (x), slope (yz) and foam (w).
vec4 seaWavesDecode(vec4 s)
{
    if (u_seaWaves[2].y > 0.5)
        return vec4(s.r - 128.0 / 255.0, (s.gb - 128.0 / 255.0) * 2.0, s.a);
    return s;
}

#if BGFX_SHADER_TYPE_VERTEX
// Metres the ripples raise the sea's vertex at `pos`.
float seaWavesLift(vec2 pos)
{
    if (u_seaWaves[0].w <= 0.0)
        return 0.0;
    vec2 uv = seaWavesUv(pos);
    return seaWavesDecode(texture2DLod(s_seaWaves, uv, 0.0)).x * seaWavesInside(uv) * u_seaWaves[0].w;
}
#endif

#if BGFX_SHADER_TYPE_FRAGMENT
// The surface at `uv` filtered as a uniform cubic B-spline (C2: smooth slopes, no bilinear facets or terraces), from
// four linear taps placed between the texels by the weights (Sigg and Hadwiger, GPU Gems 2, ch. 20).
vec4 seaWavesCubic(vec2 uv)
{
    float size = u_seaWaves[2].w;
    vec2 st = uv * size - 0.5;
    vec2 i = floor(st);
    vec2 f = st - i;
    vec2 f2 = f * f, f3 = f2 * f;
    vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    vec2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    vec2 w3 = f3 / 6.0;
    vec2 g0 = w0 + w1, g1 = w2 + w3;
    vec2 p0 = (i - 0.5 + w1 / g0) / size; // the texel centres of i - 1, i blended by w0 : w1
    vec2 p1 = (i + 1.5 + w3 / g1) / size; // those of i + 1, i + 2 by w2 : w3
    vec4 a = texture2D(s_seaWaves, vec2(p0.x, p0.y)) * g0.x + texture2D(s_seaWaves, vec2(p1.x, p0.y)) * g1.x;
    vec4 b = texture2D(s_seaWaves, vec2(p0.x, p1.y)) * g0.x + texture2D(s_seaWaves, vec2(p1.x, p1.y)) * g1.x;
    return a * g0.y + b * g1.y;
}

// The vertex shader's Fresnel term (sea_sea2_vertex_shader: unpolarised Fresnel reflectance for the cosine `c`
// between the eye and the normal and the relative refractive index `eta`), for the pixel's own normal.
float seaFresnel(float c, float eta)
{
    c = clamp(c, 1e-3, 1.0);
    float g = sqrt(max(eta * eta * (c * c - 1.0) + 1.0, 0.0));
    float s = (c - eta * g) / (c + eta * g);
    float p = (eta * c - g) / (eta * c + g);
    return min(0.5 * (s * s + p * p), 1.0);
}

// Slope of one wake at `pos` (xy, world x / z), how much of it the band limit took (z, 0..1 of a wave) and the foam
// of the churned water along its track (w). `breakup` (about 0.5..1.5) varies the divergent waves along their crests.
vec4 seaWake(vec4 a, vec4 b, vec2 pos, float pixel, float breakup)
{
    vec2 d = pos - a.xy;
    float x = -dot(d, a.zw);                // metres behind the bow
    float y = dot(d, vec2(-a.w, a.z));      // metres to the side
    if (x <= 0.0 || x >= b.y)
        return vec4_splat(0.0);
    float ay = abs(y);
    float t = ay / x;                       // tangent of the angle off the track
    float life = 1.0 - x / b.y;
    // The track: as wide as the hull at the stern, spreading slowly, foam fading over the first third of the wake.
    float lane = ay / (b.w * (1.0 + 0.03 * x));
    float churn = exp(-lane * lane * 1.5) * exp(-3.0 * x / b.y) * smoothstep(0.6 * b.w, 2.5 * b.w, x);
    float foam = churn * clamp(b.z / max(0.3 * b.w, 1e-3), 0.0, 1.4);
    float disc = 1.0 - 8.0 * t * t;         // zero on the cusp line (19.47 degrees)
    if (disc <= -0.06)
        return vec4(0.0, 0.0, 0.0, foam);
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
    // systems, disc^-1/4), dies over the wake's length and close to the hull; the divergent waves vanish on the track,
    // where the churned water has smoothed them.
    float spread = b.z * life * life * inversesqrt(1.0 + 0.05 * k0 * x) * smoothstep(0.5 * b.w, 3.0 * b.w, x);
    float edge = smoothstep(-0.06, 0.01, disc);
    float cusp = min(pow(max(disc, 0.0) + 0.03, -0.25), 2.2) * edge;
    float ht = 0.55 * spread * edge;
    float hd = spread * cusp * smoothstep(0.02, 0.12, t) * breakup * (1.0 - 0.7 * churn);
    // Band limit: a wave whose phase turns by more than half a radian per pixel fades to its mean (a crest then
    // spans more than a dozen pixels: no stair-stepped stripes at a grazing view).
    float ft = 1.0 - smoothstep(0.5, 1.5, length(kt) * pixel);
    float fd = 1.0 - smoothstep(0.5, 1.5, length(kd) * pixel);
    vec2 slope = -(ht * ft * sin(phase_t)) * kt - (hd * fd * sin(phase_d)) * kd;
    vec2 back = -a.zw, side = vec2(-a.w, a.z) * (y < 0.0 ? -1.0 : 1.0);
    float lost = (ht * length(kt) * (1.0 - ft) + hd * length(kd) * (1.0 - fd));
    return vec4(slope.x * back + slope.y * side, lost, foam);
}

// The world normal `n` of the Sea2 pixel with the ripples and wakes at the sea position t4.zw (field uv t4.xy) on it,
// as a tilt of the sea's own (bumped) normal; `foam`: crest and wake foam there (0..1), `rough`: the wake detail the
// band limit took (brightens the far wake a little). `wakes`: false leaves the wakes out (the sun road pass: its
// glints come from the ripples, the wakes' cost once). `breakup`: seaWake's.
vec3 seaWavesNormal(vec3 n, vec4 t4, float pixel, bool wakes, float breakup, out float foam, out float rough,
                    out vec3 debug)
{
    vec2 slope = vec2_splat(0.0);
    vec2 wake = vec2_splat(0.0);
    foam = 0.0;
    rough = 0.0;
    float wake_foam = 0.0;
    if (u_seaWaves[1].x > 0.0)
    {
        float keep = seaWavesInside(t4.xy) * (1.0 - smoothstep(1.0, 4.0, pixel / u_seaWaves[1].w));
        if (keep > 0.0)
        {
            vec4 s = seaWavesDecode(seaWavesCubic(t4.xy));
            slope += s.yz * keep * u_seaWaves[1].x;
            foam = s.w * keep * u_seaWaves[1].y;
        }
    }
    for (int i = 0; i < kSeaWakes; ++i)
    {
        if (!wakes || float(i) >= u_seaWaves[1].z)
            break;
        vec4 w = seaWake(u_seaWakes[i * 2], u_seaWakes[i * 2 + 1], t4.zw, pixel, breakup);
        wake += w.xy;
        rough += w.z;
        wake_foam += w.w;
    }
    foam = min(foam + wake_foam * u_seaWaves[1].y, 1.0);
    debug = vec3(length(slope) * 4.0, length(wake) * 4.0, foam);
    slope += wake;
    n = normalize(n);
    // The height field's normal (-sx, 1, -sz) on the sea's own: its slope added in the sea's tangent plane.
    return normalize(n + vec3(-slope.x, 0.0, -slope.y) * n.y);
}
#endif

#endif
