// Fixed-function texture-stage combiner. Included by fs_ff*.sc; STAGEn_SAMPLE
// macros select the sampler type of each stage (2D unless overridden).
//
// Graphics effects variants (src/effects/lighting.cpp picks them; none is used by the «Оригинал» preset):
//   FX_FF_LIT       per-pixel lighting of a lit draw (vs_ff_lit): the diffuse colour the stages read is
//                   re-lit per pixel with fx_lighting.sh (sun, hemisphere, point / spot lights, rim, sunlight through sails,
//                   specular with FX_LIGHT_SPECULAR); features the settings leave off arrive with zero strength.
//   FX_FF_BAKED     geometry with baked vertex colours (lighting off, vs_ff_lit): dynamic point lights are added,
//                   as texture * light.
//   lamps           FX_FF_LIT / FX_FF_BAKED: the location's lamps with their shadows (ffLampLight, off per draw by
//                   u_fxLamps[0].x = 0) relight the baked colours and light the lit draws; lit draws of an interior
//                   take the room light as their ambient (ffRoomLight, u_fxLampGrid[2].w).
//   FX_LIGHT_NORMALMAP  with FX_FF_LIT / FX_FF_BAKED: relief from the normal map derived from stage 1's height texture
//                   or stage 0's colour texture (stage 14, that stage's coordinates: u_fxFf.y): lit draws perturb the
//                   normal, baked ones scale their colour.
//   FX_LIGHT_SPECULAR  with FX_FF_LIT / FX_FF_BAKED: highlights. With a gloss map of stage 0's texture (stage 7,
//                   fx/gloss_map.hpp, u_fxGloss.x its share while it fades in) each texel's gloss sets the highlight
//                   (fxGlossSpec), else the material's (fxMaterialSpec). u_fxGloss.w > 0 (Settings::gloss, its value the gloss strength) adds the
//                   lamps' highlights with their shadows (lit and baked) and the sun's on baked surfaces outdoors, where
//                   the bake shows the sun; u_fxGloss.y the texture's metal class (its polished texels: the highlight in
//                   the texture's colour, a little less diffuse light; times the map's metal mask, its R); u_fxGloss.z debug
//                   views: 1 the gloss (magenta: no map), 2 the relief (the normal's tilt by the normal map, grey: none),
//                   3 the open sun the bake shows times the sun's light (yellow), its highlight (blue); lit draws
//                   dark blue; 4 the metal mask (cyan).
//   FX_LIGHT_PARALLAX   with FX_LIGHT_NORMALMAP (a stage 0 map whose material, stage 7, has a height): parallax occlusion mapping;
//                   stage 0's colour and the normal map are read where the eye's ray meets the relief.
//   FX_LIGHT_SHADOW[_SOFT]  with FX_FF_LIT: the sun's diffuse and specular terms are scaled by the sun shadow map
//                   (stage 15, src/effects/shadows.hpp).
//   AO volume       FX_FF_LIT / FX_FF_BAKED: the interior's ambient occlusion (ffAoVolume, off per draw by
//                   u_fxAoParams.x = 0): darkens the ambient of lit draws and the baked colours.
//   FX_FF_COVERAGE  alpha to coverage instead of the alpha-test discard (fx_coverage.sh); in the FX_FF_LIT /
//                   FX_FF_BAKED variants it is switched per draw by u_fxCoverage.z.
#include "ff_finish.sh"

#if defined(FX_FF_LIT) || defined(FX_FF_BAKED)
#define FX_FF_PIXEL_LIGHTING 1
#include "fx_lighting.sh"
// x: two-sided surface whose back side shows the light passing through (normal flipped towards the viewer)
// y: FX_LIGHT_NORMALMAP: the texture stage (0 or 1) whose coordinates the normal map follows
uniform vec4 u_fxFf;

#ifdef FX_LIGHT_SPECULAR
uniform vec4 u_fxGloss; // the gloss of the material texture (s_fxMaterial, fx_lighting.sh)
#endif

// Ambient-occlusion volume of the interior (src/effects/ao_volume.hpp; stage 12 = fx::kAoVolumeStage).
// u_fxAoVolume: view space -> volume uvw. u_fxAoParams: x strength (0 = no volume for this draw), y contrast,
// z normal offset in metres (half a cell: sample the open side of the surface), w 1 / what an open surface sees.
// The volume holds four cosine lobes (fx::AoLobeAxes, RGBA); Nw is the world-space normal.
SAMPLER3D(s_fxAoVolume, 12);
uniform mat4 u_fxAoVolume;
uniform vec4 u_fxAoParams;
float ffAoVolume(vec3 P, vec3 Ng, vec3 Nw)
{
    if (u_fxAoParams.x <= 0.0)
        return 1.0;
    vec3 uvw = mul(u_fxAoVolume, vec4(P + Ng * u_fxAoParams.z, 1.0)).xyz;
    vec4 lobes = texture3D(s_fxAoVolume, uvw);
    const float k = 0.57735027;
    vec4 w = max(vec4(dot(Nw, vec3(k, k, k)), dot(Nw, vec3(k, -k, -k)), dot(Nw, vec3(-k, k, -k)), dot(Nw, vec3(-k, -k, k))), 0.0);
    float seen = dot(lobes, w) / max(dot(w, vec4_splat(1.0)), 1e-4);
    float ao = clamp(seen * u_fxAoParams.w, 0.0, 1.0);
    return mix(1.0, pow(ao, u_fxAoParams.y), u_fxAoParams.x);
}

// How open to the sky the pixel's place is (src/effects/visibility.hpp, fx/visibility.hpp; stage 5 =
// fx::kVisibilityStage, the stage 0 cube map's unit, which no lit or baked variant has): x sky (cosine-weighted, to the
// place's bounds), y open (the share of directions more than 15° up that reach out). P and the geometric normal Ng in
// view space: the volume is read one cell off the surface along Ng, on the side the surface faces (a wall's own cells
// would mix in the other side's). u_fxVisibility: view space -> volume uvw; u_fxVisibilityParams: x 1 = this place
// has a volume (0: everything counts as under open sky; 2 / 3: the debug views fx_visibility_debug / fx_bounce_debug),
// y that offset in metres (one cell), z the v of the bounce's block over the volume's (0: no bounce; then the texture
// is (2 n + 1) cells high, the volume's n, a guard and the bounce's n: half a cell is z - 0.5), w the lamps' share of
// the bounce's light.
// A stage 0 cube map takes the unit: such a draw has no lit or baked variant, and its place counts as open.
#ifndef STAGE0_CUBE
SAMPLER3D(s_fxVisibility, 5);
uniform mat4 u_fxVisibility;
uniform vec4 u_fxVisibilityParams;
// The volume's uvw of P read one cell off along Ng, v kept inside the volume's block.
vec3 ffVisibilityUvw(vec3 P, vec3 Ng)
{
    vec3 uvw = mul(u_fxVisibility, vec4(P + Ng * u_fxVisibilityParams.y, 1.0)).xyz;
    if (u_fxVisibilityParams.z > 0.0)
    {
        float half_cell = u_fxVisibilityParams.z - 0.5;
        uvw.y = clamp(uvw.y, half_cell, u_fxVisibilityParams.z - 3.0 * half_cell);
    }
    return uvw;
}
#endif
vec2 ffVisibility(vec3 P, vec3 Ng)
{
#ifdef STAGE0_CUBE
    return vec2(1.0, 1.0);
#else
    if (u_fxVisibilityParams.x <= 0.0)
        return vec2(1.0, 1.0);
    return texture3D(s_fxVisibility, ffVisibilityUvw(P, Ng)).xy;
#endif
}

// The town walls' relight (src/effects/town_light.hpp, lighting.cpp bind_town_wall): [0] the lightmaps' tone a, b, c and
// w 1 = on for this draw; [1] towards the live sun (view space), w its colour's luminance; [2] towards the sun the game
// baked with; [3], [4] the game's sky per bin of ours (bin centres (i + 0.5) / 8).
uniform vec4 u_fxTownWall[5];
float ffWallSky(float k)
{
    float x = clamp(k * 8.0 - 0.5, 0.0, 7.0);
    float i = min(floor(x), 6.0);
    int at = int(i);
    float lo = at < 4 ? u_fxTownWall[3][at] : u_fxTownWall[4][at - 4];
    float hi = at + 1 < 4 ? u_fxTownWall[3][at + 1] : u_fxTownWall[4][at - 3];
    return max(k, mix(lo, hi, x - i));
}

// One bounce of indirect light at P (src/effects/bounce.hpp, fx/bounce.hpp): the light the lit surfaces around reflect
// onto a surface of world normal Nw, in the units the lights light with (a light's colour times its cosine), read one
// cell off along Ng as ffVisibility. The bounce block holds sqrt(c / 2) and the direction's y, the volume's block B / A
// its x / z: c * max(0, 1 + v.Nw) (the Lambert lobe of the light's L1 moments: a lit floor does not relight itself, a
// shadowed wall takes the street's light). 0 where there is none.
vec3 ffBounce(vec3 P, vec3 Ng, vec3 Nw)
{
#ifdef STAGE0_CUBE
    return vec3_splat(0.0);
#else
    if (u_fxVisibilityParams.x <= 0.0 || u_fxVisibilityParams.z <= 0.0)
        return vec3_splat(0.0);
    vec3 uvw = ffVisibilityUvw(P, Ng);
    vec4 near = texture3D(s_fxVisibility, uvw);
    vec4 light = texture3D(s_fxVisibility, uvw + vec3(0.0, u_fxVisibilityParams.z, 0.0));
    vec3 v = vec3(near.b, light.a, near.a) * 4.0 - 2.0;
    return light.rgb * light.rgb * (2.0 * max(0.0, 1.0 + dot(v, Nw)));
#endif
}

// The lamps of a location, per pixel and with their shadows (src/effects/lamp_shadows.hpp; fx/lamp_shadows.hpp for
// the face convention). All of it in absolute world space.
// u_fxLampView: view space -> world.
// u_fxLamps[0]: x lamp slots (0 = no lamps for this draw), y 1 = GL texture origin (bottom-left), z the lamps' gain
//               on baked colours, w the part of the lamp light the baked colours already hold
// u_fxLamps[1]: x 1 / faces per atlas row, y 1 / atlas rows, z static face size in texels, w dynamic face size
// per slot i at 2 + 3i: xyz position, w radius | rgb colour (with the slot's fade), w 1 = characters stand in its
//               light, + 2 = a light that moves (a carried lantern: no bake holds its light, it only adds) |
//               att0, att1, att2, w the lamp's own size in metres (no point is lit from nearer)
// u_fxLampGrid[0]: xyz the lamp grid's corner, w debug view (1 each slot in its colour, 2 the shadow mask)
// u_fxLampGrid[1]: xyz 1 / the grid's extent, w cells along x
// u_fxLampGrid[2]: xyz the room light's corner, w > 0 = this draw takes its ambient from the room light, times w
// u_fxLampGrid[3]: xyz 1 / the room light's extent, w 1 = 5x5 filter on the static faces
// u_fxLampGrid[4]: x the first slot of the lights that move, y how many such slots (they are not in the grid),
//                  z the share of a lamp's own colour in what it adds to baked surfaces (the rest: the bake's tone)
// u_fxLampGrid[5..7], [8..10]: per such slot, the rows of the turn from the world into the space of its faces
uniform mat4 u_fxLampView;
uniform vec4 u_fxLamps[50];
uniform vec4 u_fxLampGrid[11];
SAMPLER3D(s_fxRoomLight, 8);
SAMPLER3D(s_fxLampGrid, 9);
SAMPLER2DSHADOW(s_fxLampDynamic, 10);
SAMPLER2DSHADOW(s_fxLampStatic, 11);

// 3x3 percentage-closer filter in four hardware-filtered compare taps: the optimised PCF of The Witness, after
// MJP's Shadows sample (github.com/TheRealMJP/Shadows, MIT). uv: atlas coordinates, size: the atlas in texels.
// (The macros' own names carry a prefix: their arguments are expressions of the caller's variables.)
#define FF_LAMP_PCF3(result, map, uv, size, depth) \
    { \
        vec2 pcf_texel = (uv) * (size); \
        vec2 pcf_base = floor(pcf_texel + vec2_splat(0.5)); \
        vec2 pcf_st = pcf_texel + vec2_splat(0.5) - pcf_base; \
        pcf_base = (pcf_base - vec2_splat(0.5)) / (size); \
        vec2 pcf_w0 = vec2_splat(3.0) - 2.0 * pcf_st; \
        vec2 pcf_w1 = vec2_splat(1.0) + 2.0 * pcf_st; \
        vec2 pcf_o0 = ((vec2_splat(2.0) - pcf_st) / pcf_w0 - vec2_splat(1.0)) / (size); \
        vec2 pcf_o1 = (pcf_st / pcf_w1 + vec2_splat(1.0)) / (size); \
        result = (pcf_w0.x * pcf_w0.y * shadow2D(map, vec3(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o0.y, depth)) + \
                  pcf_w1.x * pcf_w0.y * shadow2D(map, vec3(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o0.y, depth)) + \
                  pcf_w0.x * pcf_w1.y * shadow2D(map, vec3(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o1.y, depth)) + \
                  pcf_w1.x * pcf_w1.y * shadow2D(map, vec3(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o1.y, depth))) / 16.0; \
    }

// The same filter over 5x5 texels in nine taps.
#define FF_LAMP_PCF5(result, map, uv, size, depth) \
    { \
        vec2 pcf_texel = (uv) * (size); \
        vec2 pcf_base = floor(pcf_texel + vec2_splat(0.5)); \
        vec2 pcf_st = pcf_texel + vec2_splat(0.5) - pcf_base; \
        pcf_base = (pcf_base - vec2_splat(0.5)) / (size); \
        vec2 pcf_w0 = vec2_splat(4.0) - 3.0 * pcf_st; \
        vec2 pcf_w2 = vec2_splat(1.0) + 3.0 * pcf_st; \
        vec2 pcf_o0 = ((vec2_splat(3.0) - 2.0 * pcf_st) / pcf_w0 - vec2_splat(2.0)) / (size); \
        vec2 pcf_o1 = ((vec2_splat(3.0) + pcf_st) / 7.0) / (size); \
        vec2 pcf_o2 = (pcf_st / pcf_w2 + vec2_splat(2.0)) / (size); \
        result = (pcf_w0.x * pcf_w0.y * shadow2D(map, vec3(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o0.y, depth)) + \
                  7.0 * pcf_w0.y * shadow2D(map, vec3(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o0.y, depth)) + \
                  pcf_w2.x * pcf_w0.y * shadow2D(map, vec3(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o0.y, depth)) + \
                  pcf_w0.x * 7.0 * shadow2D(map, vec3(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o1.y, depth)) + \
                  49.0 * shadow2D(map, vec3(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o1.y, depth)) + \
                  pcf_w2.x * 7.0 * shadow2D(map, vec3(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o1.y, depth)) + \
                  pcf_w0.x * pcf_w2.y * shadow2D(map, vec3(pcf_base.x + pcf_o0.x, pcf_base.y + pcf_o2.y, depth)) + \
                  7.0 * pcf_w2.y * shadow2D(map, vec3(pcf_base.x + pcf_o1.x, pcf_base.y + pcf_o2.y, depth)) + \
                  pcf_w2.x * pcf_w2.y * shadow2D(map, vec3(pcf_base.x + pcf_o2.x, pcf_base.y + pcf_o2.y, depth))) / 144.0; \
    }

// 1 = the lamp of slot `slot` reaches the point at L from it (unit geometric normal Ng, facing the lamp by ngl),
// 0 = the location or a character stands between them. radius: the lamp's; characters: its dynamic faces hold some.
float ffLampVisibility(float slot, vec3 L, float radius, float characters, vec3 Ng, float ngl)
{
    // One texel of a face at this distance, in metres: the receiver steps that far off its surface (further at grazing
    // light), and the compared distance is pulled in by most of a texel.
    float soft = u_fxLampGrid[3].w;
    float texel = 2.1 * length(L) / u_fxLamps[1].z;
    vec3 Lo = L + Ng * (texel * (1.0 + soft + 1.5 * (1.0 - clamp(ngl, 0.0, 1.0))) + 0.004);
    vec3 a = abs(Lo);
    vec3 F;
    vec3 R;
    vec3 U;
    float face;
    if (a.x >= a.y && a.x >= a.z)
    {
        if (Lo.x >= 0.0) { face = 0.0; F = vec3(1.0, 0.0, 0.0); R = vec3(0.0, 0.0, -1.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 1.0; F = vec3(-1.0, 0.0, 0.0); R = vec3(0.0, 0.0, 1.0); U = vec3(0.0, 1.0, 0.0); }
    }
    else if (a.y >= a.z)
    {
        if (Lo.y >= 0.0) { face = 2.0; F = vec3(0.0, 1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, -1.0); }
        else { face = 3.0; F = vec3(0.0, -1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, 1.0); }
    }
    else
    {
        if (Lo.z >= 0.0) { face = 4.0; F = vec3(0.0, 0.0, 1.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 5.0; F = vec3(0.0, 0.0, -1.0); R = vec3(-1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
    }
    float major = max(dot(Lo, F), 1e-4);
    float s = 1.0 / 1.05; // fx::kLampFaceGuard
    vec2 uv = vec2((face + 0.5 + 0.5 * s * dot(Lo, R) / major) * u_fxLamps[1].x,
                   (slot + 0.5 - 0.5 * s * dot(Lo, U) / major) * u_fxLamps[1].y);
    if (u_fxLamps[0].y > 0.5)
        uv.y = 1.0 - uv.y;
    float depth = (length(Lo) - 0.75 * texel) / radius;
    vec2 faces = vec2(1.0 / u_fxLamps[1].x, 1.0 / u_fxLamps[1].y);
    float lit;
    if (soft > 0.5)
    {
        FF_LAMP_PCF5(lit, s_fxLampStatic, uv, faces * u_fxLamps[1].z, depth)
    }
    else
    {
        FF_LAMP_PCF3(lit, s_fxLampStatic, uv, faces * u_fxLamps[1].z, depth)
    }
    if (characters > 0.5)
    {
        // The characters' faces are coarser by the ratio of the face sizes.
        float moving;
        FF_LAMP_PCF3(moving, s_fxLampDynamic, uv, faces * u_fxLamps[1].w, depth - texel * (u_fxLamps[1].z / u_fxLamps[1].w - 1.0) / radius)
        lit = min(lit, moving);
    }
    return lit;
}

vec3 ffLampDebugColor(float slot)
{
    vec3 hue = abs(fract(vec3_splat(slot * 0.381966) + vec3(0.0, 0.3333, 0.6667)) * 6.0 - vec3_splat(3.0)) - vec3_splat(1.0);
    return clamp(hue, 0.0, 1.0);
}

// Adds the lamp with grid number `id` (slot + 1, 0 = none) at Pw: sum.rgb its light with its shadow, sum.a the
// luminance of its light without the shadow. Ng: geometric normal (shadow bias, no light from behind the
// surface), N: shading normal. The game's own D3D attenuation, faded to zero at the lamp's radius.
// specV, spec: the world-space unit vector to the eye and the pixel's highlight (fxGlossSpec / fxMaterialSpec); the
// lamp's highlight with its shadow is added to lampSpec.
void ffLampAdd(float id, vec3 Pw, vec3 Ng, vec3 N, vec3 specV, vec2 spec, inout vec4 sum, inout vec3 lampSpec)
{
    if (id < 0.5)
        return;
    float slot = id - 1.0;
    int base = 2 + 3 * int(slot);
    vec4 lamp = u_fxLamps[base];
    vec3 L = Pw - lamp.xyz;
    float reach = length(L);
    if (reach >= lamp.w)
        return;
    vec3 toLamp = -L / max(reach, 1e-4);
    float ngl = dot(Ng, toLamp);
    float ndl = dot(N, toLamp);
    if (ngl <= 0.0 || ndl <= 0.0)
        return;
    vec4 color = u_fxLamps[base + 1];
    vec4 att = u_fxLamps[base + 2];
    float moving = color.w > 1.5 ? 1.0 : 0.0;
    // A lamp of the place fades to zero at the radius it lights on the baked surfaces. A light that moves stands for
    // one of the game's own point lights and only adds light: it reaches as that light does (fxPointLight: its D3D
    // range, the radius here, with the same fade at its end).
    float window;
    if (moving > 0.5)
        window = clamp((lamp.w - reach) / (FX_LIGHT_RANGE_FADE * lamp.w), 0.0, 1.0);
    else
    {
        window = reach / lamp.w;
        window = 1.0 - window * window * window * window;
        window *= window;
    }
    // The lamp is its flames and their fixture, not a point: nothing is lit from nearer than its own size.
    float d = max(reach, att.w);
    float falloff = window / max(att.x + att.y * d + att.z * d * d, 1e-3);
#ifdef FX_LIGHT_LINEAR
    falloff *= falloff; // as fxPointLight
#endif
    float light = ndl * falloff;
    vec3 rgb = fxLightColor(color.rgb);
    if (u_fxLampGrid[0].w > 0.5)
        rgb = ffLampDebugColor(slot) * dot(rgb, vec3(0.299, 0.587, 0.114));
    // The faces of a ship's lantern are in the ship's space: the direction and the normal turn into it.
    vec3 Lf = L;
    vec3 Nf = Ng;
    if (moving > 0.5)
    {
        int turn = 5 + 3 * int(slot - u_fxLampGrid[4].x + 0.5);
        Lf = vec3(dot(L, u_fxLampGrid[turn].xyz), dot(L, u_fxLampGrid[turn + 1].xyz), dot(L, u_fxLampGrid[turn + 2].xyz));
        Nf = vec3(dot(Ng, u_fxLampGrid[turn].xyz), dot(Ng, u_fxLampGrid[turn + 1].xyz), dot(Ng, u_fxLampGrid[turn + 2].xyz));
    }
    float seen = ffLampVisibility(slot, Lf, lamp.w, color.w - 2.0 * moving, Nf, ngl);
    sum.rgb += rgb * (light * seen);
    sum.a += dot(rgb, vec3(0.299, 0.587, 0.114)) * light;
#ifdef FX_LIGHT_SPECULAR
    if (u_fxGloss.w > 0.0)
    {
        vec3 H = normalize(toLamp + specV);
        lampSpec += rgb * (pow(max(dot(N, H), 0.0), spec.x) * spec.y * clamp(ndl * 4.0, 0.0, 1.0) * falloff * seen);
    }
#endif
}

// The light of the lights that move (a carried lantern, a ship's lanterns) at the pixel, with their shadows. The
// bake holds nothing of them: it is plain added light, as the game's own point lights, not a part of the relit sum:
// `lampMoved` of ffLampLight.

// The lamps that reach Pw, from the lamp grid (up to eight per cell); `lampSpec` their highlights (ffLampAdd).
vec4 ffLampLight(vec3 Pw, vec3 Ng, vec3 N, vec3 specV, vec2 spec, out vec3 lampMoved, out vec3 lampSpec)
{
    vec4 sum = vec4_splat(0.0);
    lampMoved = vec3_splat(0.0);
    lampSpec = vec3_splat(0.0);
    if (u_fxLamps[0].x < 0.5)
        return sum;
    // The lights that move (at most two) are few and anywhere: every pixel asks them, without the grid.
    vec4 moved = vec4_splat(0.0);
    if (u_fxLampGrid[4].y > 0.5)
        ffLampAdd(u_fxLampGrid[4].x + 1.0, Pw, Ng, N, specV, spec, moved, lampSpec);
    if (u_fxLampGrid[4].y > 1.5)
        ffLampAdd(u_fxLampGrid[4].x + 2.0, Pw, Ng, N, specV, spec, moved, lampSpec);
    // The debug views show every light the same way.
    if (u_fxLampGrid[0].w > 0.5)
        sum = moved;
    else
        lampMoved = moved.rgb;
    vec3 cell = (Pw - u_fxLampGrid[0].xyz) * u_fxLampGrid[1].xyz;
    if (min(min(cell.x, cell.y), cell.z) < 0.0 || max(max(cell.x, cell.y), cell.z) > 1.0)
        return sum;
    float half_texel = 0.5 / u_fxLampGrid[1].w;
    float u = 0.5 * clamp(cell.x, half_texel, 1.0 - half_texel);
    vec4 near = floor(texture3D(s_fxLampGrid, vec3(u, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
    ffLampAdd(near.x, Pw, Ng, N, specV, spec, sum, lampSpec);
    ffLampAdd(near.y, Pw, Ng, N, specV, spec, sum, lampSpec);
    ffLampAdd(near.z, Pw, Ng, N, specV, spec, sum, lampSpec);
    ffLampAdd(near.w, Pw, Ng, N, specV, spec, sum, lampSpec);
    if (near.w > 0.5)
    {
        vec4 more = floor(texture3D(s_fxLampGrid, vec3(u + 0.5, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
        ffLampAdd(more.x, Pw, Ng, N, specV, spec, sum, lampSpec);
        ffLampAdd(more.y, Pw, Ng, N, specV, spec, sum, lampSpec);
        ffLampAdd(more.z, Pw, Ng, N, specV, spec, sum, lampSpec);
        ffLampAdd(more.w, Pw, Ng, N, specV, spec, sum, lampSpec);
    }
    return sum;
}

// The light of the place around Pw (fx::RoomLight: its baked colours), in the baked colours' own units; Nw: the
// surface's normal (the light is taken half a metre in front of it).
vec3 ffRoomLight(vec3 Pw, vec3 Nw)
{
    vec3 uvw = (Pw + Nw * 0.5 - u_fxLampGrid[2].xyz) * u_fxLampGrid[3].xyz;
    return texture3D(s_fxRoomLight, clamp(uvw, 0.0, 1.0)).rgb;
}
#define FF_DIFFUSE ffDiffuse
#else
#define FF_DIFFUSE v_color0
#endif

// per stage i: [3i] colour op, arg1, arg2, arg0   [3i+1] alpha op, arg1, arg2, arg0
//              [3i+2] x: result goes to TEMP  y: projected  z: texture bound
uniform vec4 u_ffStage[15];
SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
#ifdef STAGE0_CUBE
SAMPLERCUBE(s_cube0, 5);
#endif
#ifdef STAGE1_CUBE
SAMPLERCUBE(s_cube1, 6);
#endif
#ifdef STAGE2_CUBE
SAMPLERCUBE(s_cube2, 7);
#endif

// diffuse: FF_DIFFUSE; specular: v_color1 (stage inputs, passed in: storm_common.sh).
vec4 ffArg(float arg, vec4 current, vec4 tex, vec4 temp, vec4 diffuse, vec4 specular)
{
    float flags = floor(arg / 16.0);
    float sel = arg - flags * 16.0;
    vec4 r;
    if (sel < 0.5) r = diffuse;
    else if (sel < 1.5) r = current;
    else if (sel < 2.5) r = tex;
    else if (sel < 3.5) r = u_ffFs[0];
    else if (sel < 4.5) r = specular;
    else r = temp;
    float complement = flags - 2.0 * floor(flags / 2.0);
    if (complement > 0.5) r = vec4_splat(1.0) - r;
    if (flags > 1.5) r = r.aaaa;
    return r;
}

vec4 ffCombine(float op, vec4 a1, vec4 a2, vec4 a0, vec4 current, vec4 tex, vec4 diffuse)
{
    if (op < 2.5) return a1;
    if (op < 3.5) return a2;
    if (op < 4.5) return a1 * a2;
    if (op < 5.5) return a1 * a2 * 2.0;
    if (op < 6.5) return a1 * a2 * 4.0;
    if (op < 7.5) return a1 + a2;
    if (op < 8.5) return a1 + a2 - vec4_splat(0.5);
    if (op < 9.5) return (a1 + a2 - vec4_splat(0.5)) * 2.0;
    if (op < 10.5) return a1 - a2;
    if (op < 11.5) return a1 + a2 - a1 * a2;
    if (op < 12.5) return mix(a2, a1, diffuse.a);
    if (op < 13.5) return mix(a2, a1, tex.a);
    if (op < 14.5) return mix(a2, a1, u_ffFs[0].a);
    if (op < 15.5) return a1 + a2 * (1.0 - tex.a);
    if (op < 16.5) return mix(a2, a1, current.a);
    if (op < 17.5) return a1;
    if (op < 18.5) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op < 19.5) return vec4(a1.rgb * a2.rgb + a1.aaa, a1.a);
    if (op < 20.5) return vec4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);
    if (op < 21.5) return vec4((vec3_splat(1.0) - a1.rgb) * a2.rgb + a1.aaa, a1.a);
    if (op < 23.5) return a1;
    if (op < 24.5) return vec4_splat(clamp(4.0 * dot(a1.rgb - vec3_splat(0.5), a2.rgb - vec3_splat(0.5)), 0.0, 1.0));
    if (op < 25.5) return a0 + a1 * a2;
    return mix(a2, a1, a0);
}

#define FF_ARG(arg) ffArg(arg, current, tex, temp, FF_DIFFUSE, v_color1)
#define FF_STAGE(i, SAMPLE) \
    if (running && float(i) < u_ffFs[3].z) \
    { \
        vec4 cs = u_ffStage[3 * i]; \
        vec4 as = u_ffStage[3 * i + 1]; \
        vec4 ms = u_ffStage[3 * i + 2]; \
        vec4 tex = vec4(0.0, 0.0, 0.0, 1.0); \
        if (ms.z > 0.5) { tex = SAMPLE; } \
        FF_KEEP_TEX(i, tex) \
        vec4 c = ffCombine(cs.x, FF_ARG(cs.y), FF_ARG(cs.z), FF_ARG(cs.w), current, tex, FF_DIFFUSE); \
        vec4 a = as.x < 1.5 ? current : ffCombine(as.x, FF_ARG(as.y), FF_ARG(as.z), FF_ARG(as.w), current, tex, FF_DIFFUSE); \
        vec4 r = clamp(vec4(c.rgb, cs.x > 23.5 && cs.x < 24.5 ? c.a : a.a), 0.0, 1.0); \
        if (ms.x > 0.5) temp = r; else current = r; \
    } \
    else { running = false; }

#ifdef FX_FF_PIXEL_LIGHTING
#define FF_KEEP_TEX(i, tex) if (i == 0) tex0 = tex;
#else
#define FF_KEEP_TEX(i, tex)
#endif

#define FF_UV(tc, ms) ((ms).y > 0.5 ? (tc).xy / (tc).w : (tc).xy)

#ifdef FX_LIGHT_PARALLAX
// The parallax variant is only chosen for a relief stage without projected coordinates (effects/lighting.cpp): that
// stage (u_fxFf.y) is read where the eye's ray meets the relief, the other as it is.
#ifndef STAGE0_SAMPLE
#define STAGE0_SAMPLE (u_fxFf.y > 0.5 ? texture2D(s_tex0, FF_UV(v_texcoord0, ms)) : texture2DGrad(s_tex0, ffReliefUv, ffReliefDx, ffReliefDy))
#endif
#ifndef STAGE1_SAMPLE
#define STAGE1_SAMPLE (u_fxFf.y > 0.5 ? texture2DGrad(s_tex1, ffReliefUv, ffReliefDx, ffReliefDy) : texture2D(s_tex1, FF_UV(v_texcoord1, ms)))
#endif
#else
#ifndef STAGE0_SAMPLE
#define STAGE0_SAMPLE texture2D(s_tex0, FF_UV(v_texcoord0, ms))
#endif
#ifndef STAGE1_SAMPLE
#define STAGE1_SAMPLE texture2D(s_tex1, FF_UV(v_texcoord1, ms))
#endif
#endif
#ifndef STAGE2_SAMPLE
#define STAGE2_SAMPLE texture2D(s_tex2, FF_UV(v_texcoord2, ms))
#endif
#define STAGE3_SAMPLE texture2D(s_tex3, FF_UV(v_texcoord3, ms))
#define STAGE4_SAMPLE texture2D(s_tex4, FF_UV(v_texcoord4, ms))

void main()
{
#ifdef FX_FF_PIXEL_LIGHTING
    vec3 P = v_position.xyz;
    vec3 V = normalize(-P);
    vec3 N = normalize(v_normal.xyz);
#ifdef FX_FF_LIT
    if (u_fxFf.x > 0.5)
    {
        // Two-sided thin surface (sails, flags): light the side the viewer sees; the light from behind comes
        // through as translucency. The facing comes from the triangle itself, so the row order of the target
        // does not matter.
        vec3 face = cross(dFdx(P), dFdy(P));
        if (dot(face, V) < 0.0) face = -face;
        if (dot(N, face) < 0.0) N = -N;
    }
#endif
    vec3 Ng = N;
#ifdef FX_LIGHT_NORMALMAP
    // The relief follows the coordinates of its stage (u_fxFf.y); with the parallax that stage's texture is then read
    // where the relief's map is (ffReliefUv, with the derivatives of the unshifted coordinates).
    vec2 ffReliefUv = u_fxFf.y > 0.5 ? v_texcoord1.xy : v_texcoord0.xy;
    vec2 ffReliefDx = dFdx(ffReliefUv);
    vec2 ffReliefDy = dFdy(ffReliefUv);
    vec3 reliefGu;
    vec3 reliefGv;
    fxReliefFrame(Ng, P, ffReliefDx, ffReliefDy, reliefGu, reliefGv);
#ifdef FX_LIGHT_PARALLAX
    vec3 ffPomFound;
    vec2 ffReliefUv0 = ffReliefUv;
    ffReliefUv = fxParallax(ffReliefUv, ffReliefDx, ffReliefDy, reliefGu, reliefGv, Ng, V, length(P), ffPomFound);
    N = fxPerturbNormal(Ng, reliefGu, reliefGv, texture2DGrad(s_fxNormalMap, ffReliefUv, ffReliefDx, ffReliefDy).rg);
#else
    N = fxPerturbNormal(Ng, reliefGu, reliefGv, texture2D(s_fxNormalMap, ffReliefUv).rg);
#endif
#endif
    // The highlight of this pixel: its gloss texel (stage 0's coordinates; the G of the material texture) or the
    // material's. ffGloss -1: no gloss map (ff_wet.sh may read it).
    float ffGloss = -1.0;
    vec2 ffSpec = fxMaterialSpec();
    // The metal of the pixel (0..1): a metal texture's polished texels; their highlight takes the texture's colour and
    // their diffuse light is a little less (subtle: no chrome look). Rust, carriages and grime are less glossy, so no
    // metal.
    float ffMetal = 0.0;
#ifdef FX_LIGHT_SPECULAR
    if (u_fxGloss.x > 0.0)
    {
        // G the gloss; R a gloss map's metal mask (fx::GlossMaterial, u_fxGloss.y > 0) or a relief's height (u_fxGloss.y
        // < 0 on metal: its polished texels by their gloss, which the metal mask floored at 0.5).
        vec2 ffMaterial = texture2D(s_fxMaterial, v_texcoord0.xy).rg;
        ffGloss = ffMaterial.g;
        ffMetal = (u_fxGloss.y > 0.0 ? u_fxGloss.y * ffMaterial.r : -u_fxGloss.y * smoothstep(0.4, 0.55, ffGloss)) * u_fxGloss.x;
        // Metal reflects several times what lacquer or glazing do.
        ffSpec = mix(ffSpec, fxGlossSpec(ffGloss) * vec2(1.0, (1.0 + 2.5 * ffMetal) * u_fxGloss.w), u_fxGloss.x);
    }
#endif
    vec3 ffSpecV = normalize(mul(u_fxLampView, vec4(V, 0.0)).xyz);
    vec3 Pw = mul(u_fxLampView, vec4(P, 1.0)).xyz;
    vec3 Nw = normalize(mul(u_fxLampView, vec4(Ng, 0.0)).xyz);
    // The place's lamps with their shadows (nothing where there are none).
    vec3 ffLampMoved;
    vec3 ffLampSpec;
    vec4 lamps = ffLampLight(Pw, Nw, normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz), ffSpecV, ffSpec, ffLampMoved, ffLampSpec);
    // How open the pixel is to the sky (the visibility volume): x the sky it sees, y whether the sun can reach it.
    // Under a roof the sun and the weather's ambient do not arrive and the room's own occlusion counts in full; in
    // a doorway, by a window or under a hatch each arrives by its share.
    vec2 skySeen = ffVisibility(P, Ng);
#ifndef STAGE0_CUBE
    // Until the place's visibility volume is there (it is baked when the place is entered; ffVisibility says "open"
    // without one), a place that has an AO volume counts as closed: it is one where one walks under a roof.
    if (u_fxVisibilityParams.x <= 0.0 && u_fxAoParams.x > 0.0)
        skySeen = vec2_splat(0.0);
#endif
    float aoVolume = mix(ffAoVolume(P, Ng, Nw), 1.0, skySeen.x);
    // The relief's cavity: an occlusion at the texture's scale, under the light as the volume's is (ambient and bounce
    // in full, half of the direct light).
    float cavity = 1.0;
#ifdef FX_LIGHT_PARALLAX
    cavity = fxReliefCavity(ffReliefUv, ffPomFound);
#endif
    float cavityDirect = mix(1.0, cavity, 0.5);
#ifdef FX_FF_LIT
    // The volume occludes the ambient part (the per-vertex sum carries the global ambient and the lights not lit per
    // pixel); the per-pixel sun and the lamps keep their own visibility.
    float sunShadow = fxSunShadow(P, Ng) * skySeen.y * cavityDirect;
#ifdef FX_LIGHT_PARALLAX
    sunShadow *= fxParallaxShadow(ffReliefUv, ffPomFound, reliefGu, reliefGv, Ng, u_fxLight[0].xyz);
#endif
    vec3 ambient = fxAmbient(N);
    float lampSeen = dot(lamps.rgb, vec3(0.299, 0.587, 0.114));
    if (u_fxLampGrid[2].w > 0.5)
    {
        // Indoors the model stands in the room's own light: the baked colours around it are its ambient (the global
        // ambient is the weather's, of the street). Where lamps light the pixel directly, the part of them the bake
        // holds leaves the ambient, as on the baked surfaces.
        vec3 room = ffRoomLight(Pw, Nw);
        float level = max(dot(room, vec3(0.299, 0.587, 0.114)), 0.03);
        vec3 roomAmbient = fxLightColor(room) * (u_fxLampGrid[2].w * level / (level + u_fxLamps[0].w * u_fxLamps[0].z * lamps.a));
#ifdef FX_LIGHT_HEMISPHERE
        ambient = mix(roomAmbient, ambient, skySeen.x); // the sky's light where the sky is seen
#else
        ambient = roomAmbient; // (without the hemisphere the weather's ambient is in the vertex sum, switched off here)
#endif
    }
    // Outdoors (no room light: indoors it already carries the place's indirect light) the lit surfaces around light the
    // model with their reflected light, in their colour; the AO volume occludes it with the rest of the ambient.
    vec3 Nshade = normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz);
    vec3 bounce = ffBounce(P, Ng, Nshade);
    if (u_fxLampGrid[2].w <= 0.5)
        ambient += bounce;
    // A lamp's light on a model is at most that of a fully lit surface: above half of it the sum is compressed
    // towards 1 (a model at a candle's flame is lit, not burnt out).
    float lampShown = lampSeen <= 0.5 ? lampSeen : 0.5 + (lampSeen - 0.5) / (1.0 + 2.0 * (lampSeen - 0.5));
    vec3 lampLight = lamps.rgb * (lampShown / max(lampSeen, 1e-4)) * cavityDirect;
    vec4 ffDiffuse = vec4(clamp(fxLit(v_color0.rgb * (aoVolume * cavity), v_matDiffuse.rgb,
                                 fxSunDiffuse(N, V) * sunShadow + (fxPointDiffuse(N, P) + ffLampMoved) * cavityDirect +
                                     lampLight,
                                 v_matAmbient.rgb, ambient * (aoVolume * cavity)), 0.0, 1.0), v_color0.a);
#else
    vec4 ffDiffuse = v_color0;
#endif
    vec4 tex0 = vec4_splat(0.0); // stage 0's texel: the baked relight, the tint of a metal highlight
#endif
    vec4 current = FF_DIFFUSE;
    vec4 temp = vec4_splat(0.0);
    bool running = true;
    FF_STAGE(0, STAGE0_SAMPLE)
    FF_STAGE(1, STAGE1_SAMPLE)
    FF_STAGE(2, STAGE2_SAMPLE)
    FF_STAGE(3, STAGE3_SAMPLE)
    FF_STAGE(4, STAGE4_SAMPLE)
#ifdef FX_FF_PIXEL_LIGHTING
    // A metal's highlight takes the colour of its texture; the rest is white (the fixed-function material has no
    // usable specular colour).
    vec3 specTint = vec3_splat(1.0);
#ifdef FX_LIGHT_SPECULAR
    specTint = mix(specTint, tex0.rgb / max(max(tex0.r, max(tex0.g, tex0.b)), 0.04), ffMetal);
    // Metal scatters less of its light diffusely: 15 % less at full metal (subtle: the game's textures hold its shading).
    current.rgb *= 1.0 - 0.15 * ffMetal;
#endif
#endif
#ifdef FX_FF_LIT
    // Highlights and the rim light.
    current.rgb = fxAddSpecular(current.rgb, specTint * (fxSunSpecular(N, V, ffSpec) * sunShadow + fxPointSpecular(N, P, V, ffSpec) +
                                                         ffLampSpec) + fxRim(N, V));
#ifdef FX_LIGHT_TRANSLUCENT
    // Sails and flags with the sun behind them: its light comes through the canvas (fx_lighting.sh). The visibility
    // is the sun's on the sail's sunlit side, so a sail does not shadow itself; a sail behind another one still gets
    // part of it (canvas passes light on, the shadow map cannot tell canvas from wood).
    if (u_fxLight[2].w > 0.0)
        current.rgb = fxAddSpecular(current.rgb, fxSunTransmission(Ng, V, dot(tex0.rgb, vec3(0.299, 0.587, 0.114))) *
                                                     mix(0.4, 1.0, fxSunShadow(P, -Ng)));
#endif
#endif
#ifdef FX_FF_BAKED
#ifdef FX_LIGHT_SPECULAR
    // How lit the bake shows this texel: its stages' result against the texture alone (with a 2x modulate or a
    // light map alike). The sun's highlight shows where that is the light of the open sun, not in the shade.
    float bakedSun = smoothstep(0.55, 0.9, dot(current.rgb, vec3(0.299, 0.587, 0.114)) / (1.0 - 0.15 * ffMetal) /
                                               max(dot(tex0.rgb, vec3(0.299, 0.587, 0.114)), 0.04));
#endif
#ifdef FX_LIGHT_NORMALMAP
    current.rgb *= fxBakedRelief(Ng, N, V);
#ifdef FX_LIGHT_PARALLAX
    // The baked colour holds the sun and the sky: the relief's own shadow takes the sun's part (about half of it).
    current.rgb *= 1.0 - 0.5 * (1.0 - fxParallaxShadow(ffReliefUv, ffPomFound, reliefGu, reliefGv, Ng, u_fxLight[0].xyz));
#endif
#endif
    // The place's lamps relight the baked colour b: b * (b + G * lit) / (b + k * G * unshadowed). Where no lamp
    // reaches, the colour is the bake; the side towards a lamp gets its light, what the lamp cannot see loses the
    // part of that light the bake is taken to hold (k). The AO volume occludes all of it: the room's own look stays.
    float baked = max(dot(v_color0.rgb, vec3(0.299, 0.587, 0.114)), 0.03);
    float gain = u_fxLamps[0].z;
    // The bake holds the game's own indirect light, and the part k = u_fxLamps[0].w of each lamp's: of the bounce only
    // that of the lamp light the relight adds beyond it joins in (its lamps' share w of the bounce's light), and only
    // where a lamp reaches (a room without lamps keeps its bake).
    vec3 Nshade = normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz);
    vec3 bounce = ffBounce(P, Ng, Nshade);
    vec3 lampBounce = bounce * (u_fxVisibilityParams.w * (1.0 - u_fxLamps[0].w) * clamp(lamps.a * 50.0, 0.0, 1.0));
    if (u_fxLamps[0].x < 0.5)
        lampBounce = vec3_splat(0.0);
    // The baked colours already carry the tone the game gave its lamp light on these surfaces: the relight adds
    // and takes light in that tone, with only a share of the lamp's own, far more saturated colour
    // (u_fxLampGrid[4].z; with all of it a candle-lit wall goes orange-red where the game's is warm white). The
    // bounce of that light is added in the same tone.
    vec3 lampAdded = lamps.rgb + lampBounce;
    vec3 lampTone = mix(vec3_splat(dot(lampAdded, vec3(0.299, 0.587, 0.114))), lampAdded, u_fxLampGrid[4].z);
    // The baked colour holds the sky, the bounce and the sun together: three quarters of the cavity.
    aoVolume *= mix(1.0, cavity, 0.75);
#ifdef FX_LIGHT_LINEAR
    current.rgb *= aoVolume * sqrt((vec3_splat(baked * baked) + gain * gain * lampTone) /
                                   (baked * baked + u_fxLamps[0].w * gain * gain * lamps.a));
#else
    current.rgb *= aoVolume * (vec3_splat(baked) + gain * lampTone) / (baked + u_fxLamps[0].w * gain * lamps.a);
#endif
    if (u_fxTownWall[0].w > 0.5)
    {
        // A town's wall (src/effects/town_light.hpp TownWallTone): its baked colour carries the game's sun along the
        // direction the game baked with and the game's sky, which takes no account of alleys and gaps between
        // houses. Relit as our lightmaps are: a + b sun + c sky with our sun (its shadow, the openness to it), our
        // sky and the sun's bounce (relative to the sun's own light) now, over the game's sun and sky then.
        vec2 seen = ffVisibility(P, Ng);
        float sunNow = max(0.0, dot(N, u_fxTownWall[1].xyz)) * fxSunShadow(P, Ng) * seen.y;
        float sunThen = max(0.0, dot(N, u_fxTownWall[2].xyz));
        vec3 bounceNow = bounce / u_fxTownWall[1].w;
        vec3 now = vec3_splat(u_fxTownWall[0].x + u_fxTownWall[0].z * seen.x) + u_fxTownWall[0].y * (vec3_splat(sunNow) + bounceNow);
        float then = u_fxTownWall[0].x + u_fxTownWall[0].y * sunThen + u_fxTownWall[0].z * ffWallSky(seen.x);
        current.rgb *= clamp(now / max(then, 0.02), 0.33, 3.0);
    }
    current.rgb = fxLit(current.rgb, tex0.rgb, (fxPointDiffuse(N, P) + ffLampMoved) * cavityDirect, vec3_splat(0.0),
                        vec3_splat(0.0));
#ifdef FX_LIGHT_SPECULAR
    // Highlights the bake cannot hold: the sun's (outdoors: u_fxLight[1] is black indoors), the lamps' with their
    // shadows (at the lamps' gain on baked colours), the dynamic lights'.
    if (u_fxGloss.w > 0.0)
    {
#ifdef FX_LIGHT_LINEAR
        float lampGain = gain * gain;
#else
        float lampGain = gain;
#endif
        current.rgb = fxAddSpecular(current.rgb, specTint * (fxSunSpecular(N, V, ffSpec) * bakedSun * skySeen.y + ffLampSpec * lampGain +
                                                             fxPointSpecular(N, P, V, ffSpec)));
    }
#endif
#endif
#if defined(FX_FF_PIXEL_LIGHTING) && defined(FX_LIGHT_SPECULAR)
    if (u_fxGloss.z > 0.5)
    {
        // Debug views: the gloss as grey (magenta: no gloss map), or the relief: how far the normal map tilts the
        // normal, 0.5 grey where it does not (no map, or still fading in).
        if (u_fxGloss.z < 1.5)
            current.rgb = ffGloss >= 0.0 ? vec3_splat(ffGloss) : vec3(0.6, 0.0, 0.6);
        else if (u_fxGloss.z < 2.5)
            current.rgb = clamp(vec3_splat(0.5) + 2.5 * (N - Ng), 0.0, 1.0);
        else if (u_fxGloss.z > 3.5)
            current.rgb = mix(vec3_splat(0.15), vec3(0.2, 0.85, 1.0), ffMetal); // the metal mask
        else
        {
#ifdef FX_FF_BAKED
            // Yellow: the open sun the bake shows, times the sun's light; blue: the sun's highlight there.
            float sunLight = bakedSun * clamp(2.0 * dot(u_fxLight[1].rgb, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
            current.rgb = vec3(sunLight, sunLight, clamp(fxSunSpecular(N, V, ffSpec).g * bakedSun, 0.0, 1.0));
#else
            current.rgb = vec3(0.0, 0.0, 0.5); // lit draws: the sun shadow map decides
#endif
        }
    }
#endif
#ifdef FX_FF_PIXEL_LIGHTING
    if (u_fxLamps[0].x > 0.5 && u_fxLampGrid[0].w > 0.5)
    {
        // Debug views: the lamps' light in the slots' colours over a grey picture, or what the shadows remove.
        float grey = dot(current.rgb, vec3(0.299, 0.587, 0.114));
        if (u_fxLampGrid[0].w < 1.5)
            current.rgb = vec3_splat(0.35 * grey) + lamps.rgb * u_fxLamps[0].z;
        else
            current.rgb = vec3_splat(lamps.a > 1e-4 ? dot(lamps.rgb, vec3(0.299, 0.587, 0.114)) / lamps.a : 0.5);
    }
#ifndef STAGE0_CUBE
    if (u_fxVisibilityParams.x > 2.5)
    {
        // Debug view (fx_bounce_debug): the bounce's light (four times) over a dim grey picture.
        current.rgb = vec3_splat(0.15 * dot(current.rgb, vec3(0.299, 0.587, 0.114))) +
                      4.0 * ffBounce(P, Ng, normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz));
    }
    else if (u_fxVisibilityParams.x > 1.5)
    {
        // Debug view (fx_visibility_debug): red the sky, green the openness above 15°, over a grey picture.
        vec2 vis = ffVisibility(P, Ng);
        current.rgb = vec3(vis.x, vis.y, 0.15) * (0.35 + 0.65 * dot(current.rgb, vec3(0.299, 0.587, 0.114)));
    }
#endif
#endif
#ifdef FX_LIGHT_PARALLAX
    if (u_fxParallax[1].y > 0.5)
    {
        // Debug view (fx_parallax_debug): red the uv shift in texels of the height's top level (full red at 8), green
        // the relief's height where the eye's ray met it, blue 0.3 on every parallax draw (other draws keep their colour).
        float shifted = length((ffReliefUv - ffReliefUv0) * exp2(u_fxParallax[0].w));
        current.rgb = vec3(clamp(shifted / 8.0, 0.0, 1.0), ffPomFound.x > 0.0 ? 1.0 - ffPomFound.y : 0.0, 0.3);
    }
#endif
    gl_FragColor = ffFinish(current);
    FF_SLOPE_DEPTH();
}
