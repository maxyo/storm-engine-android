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
vec4 ffDiffuse;

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
// has a volume (0: everything counts as under open sky; 2: the debug view, fx_visibility_debug), y that offset in
// metres (one cell).
// A stage 0 cube map takes the unit: such a draw has no lit or baked variant, and its place counts as open.
#ifndef STAGE0_CUBE
SAMPLER3D(s_fxVisibility, 5);
uniform mat4 u_fxVisibility;
uniform vec4 u_fxVisibilityParams;
#endif
vec2 ffVisibility(vec3 P, vec3 Ng)
{
#ifdef STAGE0_CUBE
    return vec2(1.0, 1.0);
#else
    if (u_fxVisibilityParams.x <= 0.0)
        return vec2(1.0, 1.0);
    return texture3D(s_fxVisibility, mul(u_fxVisibility, vec4(P + Ng * u_fxVisibilityParams.y, 1.0)).xyz).xy;
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
// u_fxLampGrid[4]: x the first slot of the lights that move, y how many such slots (they are not in the grid)
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
void ffLampAdd(float id, vec3 Pw, vec3 Ng, vec3 N, inout vec4 sum)
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
    sum.rgb += rgb * (light * ffLampVisibility(slot, Lf, lamp.w, color.w - 2.0 * moving, Nf, ngl));
    sum.a += dot(rgb, vec3(0.299, 0.587, 0.114)) * light;
}

// The light of the lights that move (a carried lantern, a ship's lanterns) at the pixel, with their shadows. The
// bake holds nothing of them: it is plain added light, as the game's own point lights, not a part of the relit sum.
vec3 ffLampMoved;

// The lamps that reach Pw, from the lamp grid (up to eight per cell); sets ffLampMoved.
vec4 ffLampLight(vec3 Pw, vec3 Ng, vec3 N)
{
    vec4 sum = vec4_splat(0.0);
    ffLampMoved = vec3_splat(0.0);
    if (u_fxLamps[0].x < 0.5)
        return sum;
    // The lights that move (at most two) are few and anywhere: every pixel asks them, without the grid.
    vec4 moved = vec4_splat(0.0);
    if (u_fxLampGrid[4].y > 0.5)
        ffLampAdd(u_fxLampGrid[4].x + 1.0, Pw, Ng, N, moved);
    if (u_fxLampGrid[4].y > 1.5)
        ffLampAdd(u_fxLampGrid[4].x + 2.0, Pw, Ng, N, moved);
    // The debug views show every light the same way.
    if (u_fxLampGrid[0].w > 0.5)
        sum = moved;
    else
        ffLampMoved = moved.rgb;
    vec3 cell = (Pw - u_fxLampGrid[0].xyz) * u_fxLampGrid[1].xyz;
    if (min(min(cell.x, cell.y), cell.z) < 0.0 || max(max(cell.x, cell.y), cell.z) > 1.0)
        return sum;
    float half_texel = 0.5 / u_fxLampGrid[1].w;
    float u = 0.5 * clamp(cell.x, half_texel, 1.0 - half_texel);
    vec4 near = floor(texture3D(s_fxLampGrid, vec3(u, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
    ffLampAdd(near.x, Pw, Ng, N, sum);
    ffLampAdd(near.y, Pw, Ng, N, sum);
    ffLampAdd(near.z, Pw, Ng, N, sum);
    ffLampAdd(near.w, Pw, Ng, N, sum);
    if (near.w > 0.5)
    {
        vec4 more = floor(texture3D(s_fxLampGrid, vec3(u + 0.5, cell.y, cell.z)) * 255.0 + vec4_splat(0.5));
        ffLampAdd(more.x, Pw, Ng, N, sum);
        ffLampAdd(more.y, Pw, Ng, N, sum);
        ffLampAdd(more.z, Pw, Ng, N, sum);
        ffLampAdd(more.w, Pw, Ng, N, sum);
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

vec4 ffArg(float arg, vec4 current, vec4 tex, vec4 temp)
{
    float flags = floor(arg / 16.0);
    float sel = arg - flags * 16.0;
    vec4 r;
    if (sel < 0.5) r = FF_DIFFUSE;
    else if (sel < 1.5) r = current;
    else if (sel < 2.5) r = tex;
    else if (sel < 3.5) r = u_ffFs[0];
    else if (sel < 4.5) r = v_color1;
    else r = temp;
    float complement = flags - 2.0 * floor(flags / 2.0);
    if (complement > 0.5) r = vec4_splat(1.0) - r;
    if (flags > 1.5) r = r.aaaa;
    return r;
}

vec4 ffCombine(float op, vec4 a1, vec4 a2, vec4 a0, vec4 current, vec4 tex)
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
    if (op < 12.5) return mix(a2, a1, FF_DIFFUSE.a);
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

#define FF_STAGE(i, SAMPLE) \
    if (running && float(i) < u_ffFs[3].z) \
    { \
        vec4 cs = u_ffStage[3 * i]; \
        vec4 as = u_ffStage[3 * i + 1]; \
        vec4 ms = u_ffStage[3 * i + 2]; \
        vec4 tex = vec4(0.0, 0.0, 0.0, 1.0); \
        if (ms.z > 0.5) { tex = SAMPLE; } \
        FF_KEEP_TEX(i, tex) \
        vec4 c = ffCombine(cs.x, ffArg(cs.y, current, tex, temp), ffArg(cs.z, current, tex, temp), \
                           ffArg(cs.w, current, tex, temp), current, tex); \
        vec4 a = as.x < 1.5 ? current : ffCombine(as.x, ffArg(as.y, current, tex, temp), ffArg(as.z, current, tex, temp), \
                           ffArg(as.w, current, tex, temp), current, tex); \
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

#ifndef STAGE0_SAMPLE
#define STAGE0_SAMPLE texture2D(s_tex0, FF_UV(v_texcoord0, ms))
#endif
#ifndef STAGE1_SAMPLE
#define STAGE1_SAMPLE texture2D(s_tex1, FF_UV(v_texcoord1, ms))
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
    N = fxPerturbNormal(Ng, P, u_fxFf.y > 0.5 ? v_texcoord1.xy : v_texcoord0.xy);
#endif
    vec3 Pw = mul(u_fxLampView, vec4(P, 1.0)).xyz;
    vec3 Nw = normalize(mul(u_fxLampView, vec4(Ng, 0.0)).xyz);
    // The place's lamps with their shadows (nothing where there are none).
    vec4 lamps = ffLampLight(Pw, Nw, normalize(mul(u_fxLampView, vec4(N, 0.0)).xyz));
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
#ifdef FX_FF_LIT
    // The volume occludes the ambient part (the per-vertex sum carries the global ambient and the lights not lit per
    // pixel); the per-pixel sun and the lamps keep their own visibility.
    float sunShadow = fxSunShadow(P, Ng) * skySeen.y;
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
    // A lamp's light on a model is at most that of a fully lit surface: above half of it the sum is compressed
    // towards 1 (a model at a candle's flame is lit, not burnt out).
    float lampShown = lampSeen <= 0.5 ? lampSeen : 0.5 + (lampSeen - 0.5) / (1.0 + 2.0 * (lampSeen - 0.5));
    vec3 lampLight = lamps.rgb * (lampShown / max(lampSeen, 1e-4));
    ffDiffuse = vec4(clamp(fxLit(v_color0.rgb * aoVolume, v_matDiffuse.rgb,
                                 fxSunDiffuse(N, V) * sunShadow + fxPointDiffuse(N, P) + lampLight + ffLampMoved,
                                 v_matAmbient.rgb, ambient * aoVolume), 0.0, 1.0), v_color0.a);
#else
    ffDiffuse = v_color0;
#endif
    vec4 tex0 = vec4_splat(0.0);
#endif
    vec4 current = FF_DIFFUSE;
    vec4 temp = vec4_splat(0.0);
    bool running = true;
    FF_STAGE(0, STAGE0_SAMPLE)
    FF_STAGE(1, STAGE1_SAMPLE)
    FF_STAGE(2, STAGE2_SAMPLE)
    FF_STAGE(3, STAGE3_SAMPLE)
    FF_STAGE(4, STAGE4_SAMPLE)
#ifdef FX_FF_LIT
    // Highlights (white: the fixed-function material has no usable specular colour) and the rim light.
    current.rgb = fxAddSpecular(current.rgb, fxSunSpecular(N, V) * sunShadow + fxPointSpecular(N, P, V) + fxRim(N, V));
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
#ifdef FX_LIGHT_NORMALMAP
    current.rgb *= fxBakedRelief(Ng, N, V);
#endif
    // The place's lamps relight the baked colour b: b * (b + G * lit) / (b + k * G * unshadowed). Where no lamp
    // reaches, the colour is the bake; the side towards a lamp gets its light, what the lamp cannot see loses the
    // part of that light the bake is taken to hold (k). The AO volume occludes all of it: the room's own look stays.
    float baked = max(dot(v_color0.rgb, vec3(0.299, 0.587, 0.114)), 0.03);
    float gain = u_fxLamps[0].z;
#ifdef FX_LIGHT_LINEAR
    current.rgb *= aoVolume * sqrt((vec3_splat(baked * baked) + gain * gain * lamps.rgb) /
                                   (baked * baked + u_fxLamps[0].w * gain * gain * lamps.a));
#else
    current.rgb *= aoVolume * (vec3_splat(baked) + gain * lamps.rgb) / (baked + u_fxLamps[0].w * gain * lamps.a);
#endif
    current.rgb = fxLit(current.rgb, tex0.rgb, fxPointDiffuse(N, P) + ffLampMoved, vec3_splat(0.0), vec3_splat(0.0));
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
    if (u_fxVisibilityParams.x > 1.5)
    {
        // Debug view (fx_visibility_debug): red the sky, green the openness above 15°, over a grey picture.
        vec2 vis = ffVisibility(P, Ng);
        current.rgb = vec3(vis.x, vis.y, 0.15) * (0.35 + 0.65 * dot(current.rgb, vec3(0.299, 0.587, 0.114)));
    }
#endif
#endif
    gl_FragColor = ffFinish(current);
    FF_SLOPE_DEPTH();
}
