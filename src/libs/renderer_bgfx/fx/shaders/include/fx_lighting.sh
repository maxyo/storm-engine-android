// Per-pixel lighting upgrade for the fixed-function emulator.
//
// Compile-time feature flags (the emulator adds them to its variant key; with
// none defined this header adds nothing and the legacy per-vertex path is the
// whole lighting model). fx::LightingDefines(settings) returns the set to use.
//   FX_LIGHT_PER_PIXEL    sun diffuse evaluated per pixel
//   FX_LIGHT_HEMISPHERE   sky / ground ambient instead of the flat global ambient
//   FX_LIGHT_SPECULAR     Blinn-Phong highlights
//   FX_LIGHT_TRANSLUCENT  sunlight passing through thin two-sided surfaces (sails, flags): fxSunTransmission
//   FX_LIGHT_POINT        the enabled D3D point / spot lights evaluated per pixel;
//                         FX_LIGHT_POINT_MAX = 4 or 8 lights per draw call
//   FX_LIGHT_SHADOW       sun shadow map; FX_LIGHT_SHADOW_SOFT = four taps instead of one
//   FX_LIGHT_RIM          rim light on silhouettes seen against the sun (characters, ships)
//   FX_LIGHT_LINEAR       lights are summed in linear light: overlapping lights no longer burn out and
//                         the terminator is soft. Combine with fxLit() / fxAddSpecular().
//   FX_LIGHT_NORMALMAP    relief from a normal map derived from the game's height ("bump") texture of texture
//                         stage 1 or the colour texture of stage 0 (fx/normal_map.hpp); no mesh tangents needed
//   FX_LIGHT_PARALLAX     with FX_LIGHT_NORMALMAP: parallax occlusion mapping from the relief's height (stage 0 maps)
//
// Contract with the emulator's vertex shader when FX_LIGHT_PER_PIXEL is on:
//   * pass the view-space normal and position to the fragment shader;
//   * leave the sun (the directional light fx::Lighting was given) out of the
//     per-vertex sum, with FX_LIGHT_HEMISPHERE also the global ambient, and
//     with FX_LIGHT_POINT every point and spot light. Emissive, material alpha
//     and the lights' own ambient terms stay per vertex in v_color0.
// The fragment shader then computes, before the texture stages:
//   v_color0.rgb = fxLit(v_color0.rgb, matDiffuse, fxSunDiffuse(N, V) * fxSunShadow(P, N) + fxPointDiffuse(N, P),
//                        matAmbient, fxAmbient(N))
// and after them:
//   color.rgb = fxAddSpecular(color.rgb, matSpecular * (fxSunSpecular(N, V, spec) * fxSunShadow(P, N)
//                                                       + fxPointSpecular(N, P, V, spec)) + fxRim(N, V))
// where spec = fxMaterialSpec() (the material's highlight) or fxGlossSpec(gloss) (a texel of a gloss map,
// fx/gloss_map.hpp).
// and with FX_LIGHT_TRANSLUCENT, for two-sided surfaces (the sun's visibility taken on the surface's sunlit side):
//   color.rgb = fxAddSpecular(color.rgb, fxSunTransmission(N, V, luminance of the colour texture) * fxSunShadow(P, -N))
// Without FX_LIGHT_LINEAR everything is in the gamma space of the legacy pipeline and uses the D3D9
// formulas, so a surface keeps its fixed-function brightness; only the interpolation changes (per pixel
// instead of per vertex). With FX_LIGHT_LINEAR light colours (and point-light falloff, which the game tuned
// in gamma space) are squared before they are summed and the result is square-rooted (gamma 2.0): a surface
// lit by one light at full strength keeps its brightness and a torch its reach, while the cosine term and
// overlapping lights add up the way light does. Render targets stay gamma encoded; the
// «Оригинал» preset never defines this flag.
//
// FX_LIGHT_NORMALMAP (only for draw calls whose stage 1 or stage 0 texture has a derived normal map, bound with
// fx::Lighting::BindNormalMap): replace the interpolated normal before any of the calls above,
//   fxReliefFrame(N, P, dFdx(uv), dFdy(uv), gu, gv);  N = fxPerturbNormal(N, gu, gv, the map's texel at uv);
// with uv that stage's texture coordinates. The stages themselves stay as they are (colour, and the 2x multiply detail
// layer of the original look). Cost per pixel: one RG8 fetch, six screen-space derivatives, two cross products and two
// normalisations.
// FX_LIGHT_PARALLAX (with FX_LIGHT_NORMALMAP, a stage 0 map whose class has a height: its material at fx::Lighting::BindMaterial):
// before that, uv = fxParallax(uv, ...): the map and stage 0's colour are read where the eye's ray meets the relief
// (with the derivatives of the unshifted uv: no seams where the shift jumps).
// For baked geometry, which has no run-time sun term, multiply the baked colour instead:
//   current.rgb *= fxBakedRelief(Ngeometry, N, V);
//
// Geometry drawn with D3DRS_LIGHTING off and baked vertex colours (static
// location models, see src/libs/lighter) already contains the location's
// lights. For it use FX_LIGHT_POINT alone, select lights with
// include_baked = false (flashes and carried lights only) and add
//   current.rgb = fxLit(current.rgb, texture.rgb, fxPointDiffuse(N, P), vec3_splat(0.0), vec3_splat(0.0))
// so nothing is lit twice and the baked night look is kept.
#ifndef FX_LIGHTING_SH
#define FX_LIGHTING_SH

// [0] xyz: unit vector to the sun in VIEW space, w: specular power
// [1] rgb: sun diffuse colour (D3DLIGHT9.Diffuse), w: specular strength
// [2] xyz: world up in VIEW space, w: translucency 0..1
// [3] rgb: ambient from the sky, w: wrap 0..1 (softens the terminator)
// [4] rgb: ambient from the ground / sea
// [5] x: normal map strength for this material (0 = flat), y: rim light strength
uniform vec4 u_fxLight[6];

// Light colour as the sums below expect it.
vec3 fxLightColor(vec3 c)
{
#ifdef FX_LIGHT_LINEAR
    return c * c;
#else
    return c;
#endif
}

// Lit colour from the per-vertex part and the per-pixel diffuse and ambient sums.
vec3 fxLit(vec3 vertexLit, vec3 matDiffuse, vec3 diffuse, vec3 matAmbient, vec3 ambient)
{
#ifdef FX_LIGHT_LINEAR
    return sqrt(vertexLit * vertexLit + matDiffuse * matDiffuse * diffuse + matAmbient * matAmbient * ambient);
#else
    return vertexLit + matDiffuse * diffuse + matAmbient * ambient;
#endif
}

vec3 fxAddSpecular(vec3 color, vec3 specular)
{
#ifdef FX_LIGHT_LINEAR
    return sqrt(color * color + specular);
#else
    return color + specular;
#endif
}

// A highlight's Blinn-Phong power (x) and strength (y). The material's (fx::LightingMaterial):
vec2 fxMaterialSpec()
{
    return vec2(u_fxLight[0].w, u_fxLight[1].w);
}

// A texel of a gloss map, gloss 0 (matte) .. 1 (a mirror): the lobe narrows from power 8 to 181 (the game's coarse
// meshes and their interpolated normals cannot hold a sharper one: it would flicker or vanish) and keeps its
// energy (normalised Blinn-Phong: the peak grows as (power + 8) / 8) while the reflected share is half of gloss^2.5.
// Dry wood, stone and cloth (gloss 0.04..0.2) get next to nothing (no plastic sheen); painted hulls a faint sheen;
// lacquer, leather and wet stone (~0.5) clear highlights; metal and glass (0.65..0.9) bright, small ones.
vec2 fxGlossSpec(float gloss)
{
    float power = exp2(3.0 + 4.5 * gloss);
    return vec2(power, 0.0625 * gloss * gloss * sqrt(gloss) * (power + 8.0));
}

// The sun's highlight (the baked variants use it too: their bake holds the sun's light, not its highlight).
// N: unit view-space normal, V: unit vector to the eye, spec: fxMaterialSpec() or fxGlossSpec().
vec3 fxSunSpecular(vec3 N, vec3 V, vec2 spec)
{
#ifdef FX_LIGHT_SPECULAR
    vec3 H = normalize(u_fxLight[0].xyz + V);
    float ndl = clamp(dot(N, u_fxLight[0].xyz) * 4.0, 0.0, 1.0);
    return fxLightColor(u_fxLight[1].rgb) * (pow(max(dot(N, H), 0.0), spec.x) * spec.y * ndl);
#else
    return vec3_splat(0.0);
#endif
}

#ifdef FX_LIGHT_PER_PIXEL
// N: unit view-space normal facing the viewer side, V: unit vector to the eye.
vec3 fxSunDiffuse(vec3 N, vec3 V)
{
    float ndl = dot(N, u_fxLight[0].xyz);
    float wrap = u_fxLight[3].w;
    float lit = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
    return fxLightColor(u_fxLight[1].rgb) * lit;
}

#ifdef FX_LIGHT_TRANSLUCENT
// Sunlight passing through a thin two-sided surface (sails, flags) seen from its shadowed side: N faces the viewer,
// so the sun stands behind the surface where N.L < 0. Canvas scatters it mostly forwards (strongest when looking
// towards the sun) and lets the warm part through. weave: the luminance of the surface's own colour texture (its
// threads, seams and patches show); added after the texture stages, so it is not tinted twice by the texture.
// Strength u_fxLight[2].w (LightingMaterial::translucency; 0 for every other material).
vec3 fxSunTransmission(vec3 N, vec3 V, float weave)
{
    // back: the sun's light on the far side, saturating (a curved sail under a high sun still glows); toward: the
    // forward-scattering lobe around the sun.
    float back = clamp(-dot(N, u_fxLight[0].xyz), 0.0, 1.0);
    float toward = clamp(-dot(V, u_fxLight[0].xyz), 0.0, 1.0);
    float toward2 = toward * toward;
    float through = (1.2 * back / (back + 0.2)) * (0.3 + 1.7 * toward2 * toward2);
    return fxLightColor(u_fxLight[1].rgb * vec3(1.0, 0.85, 0.6)) * (through * weave * u_fxLight[2].w);
}
#endif

vec3 fxAmbient(vec3 N)
{
#ifdef FX_LIGHT_HEMISPHERE
    float up = 0.5 + 0.5 * dot(N, u_fxLight[2].xyz);
    return fxLightColor(mix(u_fxLight[4].rgb, u_fxLight[3].rgb, up));
#else
    return vec3_splat(0.0);
#endif
}

// Light wrapping around a silhouette when the viewer looks towards the sun.
vec3 fxRim(vec3 N, vec3 V)
{
#ifdef FX_LIGHT_RIM
    float edge = 1.0 - clamp(dot(N, V), 0.0, 1.0);
    float against = clamp(0.35 - 0.65 * dot(V, u_fxLight[0].xyz), 0.0, 1.0);
    return fxLightColor(u_fxLight[1].rgb) * (edge * edge * edge * against * u_fxLight[5].y);
#else
    return vec3_splat(0.0);
#endif
}
#endif // FX_LIGHT_PER_PIXEL

#if defined(FX_LIGHT_PARALLAX) || defined(FX_LIGHT_SPECULAR)
// Sampler stage 7 (fx::kMaterialStage): the material texture, EAC RG11 with mips: R the relief's height (1 = the top,
// fx/height_map.hpp: the parallax and the cavity), G the gloss (fx/gloss_map.hpp: the highlights).
SAMPLER2D(s_fxMaterial, 7);
#endif

#ifdef FX_LIGHT_NORMALMAP
// Sampler stage 14 (fx::kNormalMapStage) is reserved for the derived normal map (RG8: x, y of the tangent-space normal).
SAMPLER2D(s_fxNormalMap, 14);

// The relief's frame at a pixel: gu, gv are the surface gradients of the map's coordinates u and v (view space, per
// metre: the direction in which u grows, as fast as it grows), from screen-space derivatives of the position P and of
// uv (dux = dFdx(uv), duy = dFdy(uv)) around the unit normal N: a cotangent frame (Schüler, "Followup: Normal Mapping
// Without Precomputed Tangents"), so meshes need no tangents and mirrored or rotated mappings are handled by
// construction. The determinant keeps its sign: a picture drawn with its rows the other way round has the same frame.
void fxReliefFrame(vec3 N, vec3 P, vec2 dux, vec2 duy, out vec3 gu, out vec3 gv)
{
    vec3 dpx = dFdx(P);
    vec3 dpy = dFdy(P);
    vec3 px = cross(dpy, N);
    vec3 py = cross(N, dpx);
    float det = dot(dpx, px);
    float inv = abs(det) > 1e-30 ? 1.0 / det : 0.0;
    gu = (px * dux.x + py * duy.x) * inv;
    gv = (px * dux.y + py * duy.y) * inv;
}

// N: unit interpolated normal, gu, gv: its relief frame (fxReliefFrame), rg: the normal map's texel.
vec3 fxPerturbNormal(vec3 N, vec3 gu, vec3 gv, vec2 rg)
{
    float scale = inversesqrt(max(max(dot(gu, gu), dot(gv, gv)), 1e-20));
    vec2 n = (rg * 2.0 - vec2_splat(1.0)) * u_fxLight[5].x;
    float nz = sqrt(max(1.0 - dot(n, n), 0.0));
    return normalize(gu * (scale * n.x) + gv * (scale * n.y) + N * nz);
}

#ifdef FX_LIGHT_PARALLAX
// [0] x, y: the depth of the height's 0..1 span in u and v (its depth in texels over the map's width and height, times
//         the class's depth, the strength and the relief's fade-in); z: the most steps of the ray (1 = one-tap offset
//         mapping); w: log2 of the map's larger side (the level the steps read).
// [1] x: the self-shadow towards the sun, 0..1 of the sun's light it may take (0 = none: offset mapping, interiors);
//     y: 1 = the debug view (ff_fragment.sh, fx_parallax_debug); z: the cavity's strength (fxReliefCavity, 0 = none).
// With z set and no parallax (x, y and z of [0] zero) the variant draws only the cavity.
uniform vec4 u_fxParallax[2];

// Parallax occlusion mapping: where the ray from the eye along -V meets the relief below the surface at uv (its
// derivatives dux, duy; frame gu, gv; geometric unit normal Ng facing the viewer; distance: the pixel's from the eye in
// metres). The relief's top is the surface: the ray goes down into the texture's pits, never above it. The step count
// grows towards grazing views (8 looking straight at the surface, up to z), the last two steps are interpolated
// linearly; the effect fades out between 10 and 15 m, where it is not drawn at all. At grazing views the shift is held
// to 2.5 times the depth: further, it runs off the texture's region at the edge of a polygon (a dark band under a step).
// One tap (z = 1): offset mapping with offset limiting (no division by the cosine).
// `found`, for fxParallaxShadow: x the full depth in metres (0: no parallax drawn), y where the eye's ray met the relief
// (0 the top .. 1 the deepest), z the level the steps read.
vec2 fxParallax(vec2 uv, vec2 dux, vec2 duy, vec3 gu, vec3 gv, vec3 Ng, vec3 V, float distance, out vec3 found)
{
    float lod = max(0.5 * log2(max(dot(dux, dux), dot(duy, duy))) + u_fxParallax[0].w, 0.0);
    found = vec3(0.0, 0.0, lod);
    float fade = 1.0 - smoothstep(10.0, 15.0, distance);
    if (fade <= 0.0 || u_fxParallax[0].z < 0.5)
        return uv;
    // The full depth in metres: the depth in texels times a texel's size along u and along v, averaged.
    float lu = length(gu);
    float lv = length(gv);
    float depth = 0.5 * (u_fxParallax[0].x / max(lu, 1e-6) + u_fxParallax[0].y / max(lv, 1e-6)) * fade;
    float nv = clamp(dot(Ng, V), 0.0, 1.0);
    // Along the ray, going one metre down into the surface moves the point 1 / nv metres along it, its uv by gu, gv.
    vec2 along = vec2(dot(gu, V), dot(gv, V)) * depth;
    found.x = depth;
    if (u_fxParallax[0].z < 1.5)
    {
        float h = texture2DLod(s_fxMaterial, uv, lod).r;
        found.y = 1.0 - h;
        return uv - along * (1.0 - h);
    }
    vec2 shift = along / max(nv, 0.4);
    float steps = mix(u_fxParallax[0].z, 8.0, nv);
    float layer = 1.0 / steps;
    vec2 step = shift * layer;
    vec2 at = uv;
    float below = 0.0;                                           // the ray's depth (0 at the top, 1 the deepest)
    float ground = 1.0 - texture2DLod(s_fxMaterial, at, lod).r; // the relief's depth under it
    float prev_below = 0.0;
    float prev_ground = ground;
    for (int i = 0; i < 32; ++i)
    {
        if (below >= ground || float(i) >= steps)
            break;
        prev_below = below;
        prev_ground = ground;
        at -= step;
        below += layer;
        ground = 1.0 - texture2DLod(s_fxMaterial, at, lod).r;
    }
    // Between the last step above the relief and the first below it: where the two lines cross.
    float after = ground - below;
    float before = prev_ground - prev_below;
    float t = clamp(after / min(after - before, -1e-5), 0.0, 1.0);
    found.y = mix(below, prev_below, t);
    return at + step * t;
}

// The relief's own shadow towards the light L (unit, view space) at the point fxParallax found (uv: its result, found:
// what it found): a short
// ray from it up to the top along L, six taps; what of the relief stands above the ray darkens softly, the more the
// higher it stands and the nearer it is. 1 = lit, down to 1 - u_fxParallax[1].x. The ray's run along the surface is held
// to three times the depth (a low sun). Nothing where no parallax was drawn or the surface faces away from L.
float fxParallaxShadow(vec2 uv, vec3 found, vec3 gu, vec3 gv, vec3 Ng, vec3 L)
{
    float nl = dot(Ng, L);
    if (u_fxParallax[1].x <= 0.0 || found.x <= 0.0 || nl <= 0.0 || found.y <= 0.0)
        return 1.0;
    // Going one unit of depth up towards L moves the point by this much uv.
    vec2 toward = vec2(dot(gu, L), dot(gv, L)) * (found.x / max(nl, 0.33));
    float occlusion = 0.0;
    for (int i = 1; i <= 6; ++i)
    {
        float k = float(i) / 6.0;
        float ray = found.y * (1.0 - k); // the ray's depth there
        float ground = 1.0 - texture2DLod(s_fxMaterial, uv + toward * (found.y * k), found.z).r;
        occlusion = max(occlusion, (ray - ground) * (1.0 - 0.6 * k) * 6.0);
    }
    return 1.0 - u_fxParallax[1].x * clamp(occlusion, 0.0, 1.0);
}
// The relief's cavity (micro-occlusion) at uv: how far the height there lies below its surroundings, the same height
// two and a half levels coarser (`found`: fxParallax's, its z the level; its y the depth of the ray's hit when the
// parallax ran, else the height is read). 1 = open, down to 1 - u_fxParallax[1].z in joints, gaps and recesses. One
// tap of the height (two without the parallax).
float fxReliefCavity(vec2 uv, vec3 found)
{
    if (u_fxParallax[1].z <= 0.0)
        return 1.0;
    float h = found.x > 0.0 ? 1.0 - found.y : texture2DLod(s_fxMaterial, uv, found.z).r;
    float around = texture2DLod(s_fxMaterial, uv, found.z + 2.5).r;
    return 1.0 - u_fxParallax[1].z * clamp((around - h) * 4.0, 0.0, 1.0);
}
#endif // FX_LIGHT_PARALLAX

// Baked geometry: how much brighter or darker the relief makes a surface that was baked with normal Ng; V: unit
// vector to the eye. The baked colours already hold the sun's strength, so only its direction matters here: a light
// 60 degrees from the surface normal on the sun's side (the viewer's side when the sun stands straight above the
// surface), otherwise a high sun would flatten the relief of the ground. Averages to 1 over a texture, so the baked
// brightness is kept.
float fxBakedRelief(vec3 Ng, vec3 N, vec3 V)
{
    vec3 t = u_fxLight[0].xyz - Ng * dot(u_fxLight[0].xyz, Ng);
    if (dot(t, t) < 1e-4) t = V - Ng * dot(V, Ng);
    vec3 L = Ng * 0.5 + t * (0.866 * inversesqrt(max(dot(t, t), 1e-12)));
    // N leans as far as Settings::normal_map_strength (u_fxLight[5].x) lets it, so that knob scales this too.
    return clamp(1.0 + 2.4 * (dot(N, L) - 0.5), 0.6, 1.4);
}
#endif // FX_LIGHT_NORMALMAP

#ifdef FX_LIGHT_POINT
#ifndef FX_LIGHT_POINT_MAX
#define FX_LIGHT_POINT_MAX 4
#endif
// Fraction of the range over which the D3D range cut-off is faded (a hard edge would show per pixel).
#define FX_LIGHT_RANGE_FADE 0.15

// Per light i (fx::PackPointLights), view space:
//   [4i]   xyz: position, w: range
//   [4i+1] rgb: diffuse colour, w: attenuation0
//   [4i+2] x: attenuation1, y: attenuation2, z, w: spot cone as saturate(rho * z + w)
//   [4i+3] xyz: spot direction, w: spot falloff exponent
uniform vec4 u_fxPoint[32];
uniform vec4 u_fxPointCount; // x: number of lights

// Light i at P: xyz = unit vector to the light, w = attenuation * spot factor (D3D9 fixed-function formulas).
vec4 fxPointLight(int i, vec3 P)
{
    vec4 position = u_fxPoint[4 * i];
    vec4 params = u_fxPoint[4 * i + 2];
    vec4 spot = u_fxPoint[4 * i + 3];
    vec3 toLight = position.xyz - P;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-5);
    float attenuation = 1.0 / max(u_fxPoint[4 * i + 1].w + params.x * dist + params.y * dist * dist, 1e-6);
    attenuation *= clamp((position.w - dist) / (FX_LIGHT_RANGE_FADE * position.w), 0.0, 1.0);
    float cone = clamp(dot(spot.xyz, -L) * params.z + params.w, 0.0, 1.0);
    float falloff = attenuation * pow(cone, spot.w);
#ifdef FX_LIGHT_LINEAR
    // The game's attenuation constants were tuned by eye in gamma space: square the falloff with the colour,
    // so a single light reaches as far as it did and only the cosine term and overlaps follow linear light.
    falloff *= falloff;
#endif
    return vec4(L, falloff);
}

// Sum of the point / spot lights' diffuse terms at P with unit normal N (to be multiplied by the material diffuse).
vec3 fxPointDiffuse(vec3 N, vec3 P)
{
    vec3 sum = vec3_splat(0.0);
    for (int i = 0; i < FX_LIGHT_POINT_MAX; ++i)
    {
        if (float(i) >= u_fxPointCount.x)
            break;
        vec4 light = fxPointLight(i, P);
        sum += fxLightColor(u_fxPoint[4 * i + 1].rgb) * (max(dot(N, light.xyz), 0.0) * light.w);
    }
    return sum;
}

vec3 fxPointSpecular(vec3 N, vec3 P, vec3 V, vec2 spec)
{
    vec3 sum = vec3_splat(0.0);
#ifdef FX_LIGHT_SPECULAR
    for (int i = 0; i < FX_LIGHT_POINT_MAX; ++i)
    {
        if (float(i) >= u_fxPointCount.x)
            break;
        vec4 light = fxPointLight(i, P);
        float ndl = clamp(dot(N, light.xyz) * 4.0, 0.0, 1.0);
        vec3 H = normalize(light.xyz + V);
        sum += fxLightColor(u_fxPoint[4 * i + 1].rgb) * (pow(max(dot(N, H), 0.0), spec.x) * spec.y * ndl * light.w);
    }
#endif
    return sum;
}
#endif // FX_LIGHT_POINT

#ifdef FX_LIGHT_SHADOW
// Sampler stage 15 (fx::kShadowMapStage) is reserved for the shadow map in every shader that defines FX_LIGHT_SHADOW.
SAMPLER2DSHADOW(s_fxShadowMap, 15);
uniform mat4 u_fxShadowMtx; // view space -> shadow map uv and depth
uniform vec4 u_fxShadow;    // x: one texel in uv, y: shadow strength, z: normal offset in view units, w: depth bias

// 1 = fully lit by the sun, 1 - strength = fully shadowed. P and N in view space.
float fxSunShadow(vec3 P, vec3 N)
{
    vec4 c = mul(u_fxShadowMtx, vec4(P + N * u_fxShadow.z, 1.0));
    vec3 coord = vec3(c.xy, c.z - u_fxShadow.w);
#ifdef FX_LIGHT_SHADOW_SOFT
    // Four hardware-filtered taps on a rotated square: a 4x4 texel soft edge.
    vec2 o = u_fxShadow.xx;
    float lit = shadow2D(s_fxShadowMap, coord + vec3(-1.1 * o.x, -0.4 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(0.4 * o.x, -1.1 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(1.1 * o.x, 0.4 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(-0.4 * o.x, 1.1 * o.y, 0.0));
    lit *= 0.25;
#else
    float lit = shadow2D(s_fxShadowMap, coord);
#endif
    // Outside the map (and beyond its far plane) everything is lit; fade out over the last tenth.
    vec2 edge = abs(coord.xy - vec2_splat(0.5)) * 2.0;
    float inside = clamp((1.0 - max(max(edge.x, edge.y), coord.z)) * 10.0, 0.0, 1.0);
    lit = 1.0 - (1.0 - lit) * inside;
    return 1.0 - (1.0 - lit) * u_fxShadow.y;
}
#else
#define fxSunShadow(P, N) 1.0
#endif
#endif
