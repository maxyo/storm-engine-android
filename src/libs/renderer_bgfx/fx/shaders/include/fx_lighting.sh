// Per-pixel lighting upgrade for the fixed-function emulator.
//
// Compile-time feature flags (the emulator adds them to its variant key; with
// none defined this header adds nothing and the legacy per-vertex path is the
// whole lighting model). fx::LightingDefines(settings) returns the set to use.
//   FX_LIGHT_PER_PIXEL    sun diffuse evaluated per pixel
//   FX_LIGHT_HEMISPHERE   sky / ground ambient instead of the flat global ambient
//   FX_LIGHT_SPECULAR     Blinn-Phong highlights
//   FX_LIGHT_TRANSLUCENT  light passing through thin two-sided surfaces (sails, flags)
//   FX_LIGHT_POINT        the enabled D3D point / spot lights evaluated per pixel;
//                         FX_LIGHT_POINT_MAX = 4 or 8 lights per draw call
//   FX_LIGHT_SHADOW       sun shadow map; FX_LIGHT_SHADOW_SOFT = four taps instead of one
//   FX_LIGHT_RIM          rim light on silhouettes seen against the sun (characters, ships)
//   FX_LIGHT_LINEAR       lights are summed in linear light: overlapping lights no longer burn out and
//                         the terminator is soft. Combine with fxLit() / fxAddSpecular().
//   FX_LIGHT_NORMALMAP    relief from a normal map derived from the game's height ("bump") texture
//                         of texture stage 1; no mesh tangents needed
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
//   color.rgb = fxAddSpecular(color.rgb, matSpecular * (fxSunSpecular(N, V) * fxSunShadow(P, N)
//                                                       + fxPointSpecular(N, P, V)) + fxRim(N, V))
// Without FX_LIGHT_LINEAR everything is in the gamma space of the legacy pipeline and uses the D3D9
// formulas, so a surface keeps its fixed-function brightness; only the interpolation changes (per pixel
// instead of per vertex). With FX_LIGHT_LINEAR light colours (and point-light falloff, which the game tuned
// in gamma space) are squared before they are summed and the result is square-rooted (gamma 2.0): a surface
// lit by one light at full strength keeps its brightness and a torch its reach, while the cosine term and
// overlapping lights add up the way light does. Render targets stay gamma encoded; the
// «Оригинал» preset never defines this flag.
//
// FX_LIGHT_NORMALMAP (only for draw calls whose stage 1 texture has a derived normal map, bound with
// fx::Lighting::BindNormalMap): replace the interpolated normal before any of the calls above,
//   N = fxPerturbNormal(N, P, stage 1 texture coordinates);
// The stage itself stays as it is (the 2x multiply detail layer of the original look). Cost per pixel:
// one RG8 fetch, four screen-space derivatives, two cross products and two normalisations.
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

#ifdef FX_LIGHT_PER_PIXEL
// N: unit view-space normal facing the viewer side, V: unit vector to the eye.
vec3 fxSunDiffuse(vec3 N, vec3 V)
{
    float ndl = dot(N, u_fxLight[0].xyz);
    float wrap = u_fxLight[3].w;
    float lit = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
#ifdef FX_LIGHT_TRANSLUCENT
    // Light entering the far side glows through, strongest when looking towards the sun.
    float through = clamp(-ndl, 0.0, 1.0) * (0.55 + 0.45 * clamp(-dot(V, u_fxLight[0].xyz), 0.0, 1.0));
    lit += through * u_fxLight[2].w;
#endif
    return fxLightColor(u_fxLight[1].rgb) * lit;
}

vec3 fxAmbient(vec3 N)
{
#ifdef FX_LIGHT_HEMISPHERE
    float up = 0.5 + 0.5 * dot(N, u_fxLight[2].xyz);
    return fxLightColor(mix(u_fxLight[4].rgb, u_fxLight[3].rgb, up));
#else
    return vec3_splat(0.0);
#endif
}

vec3 fxSunSpecular(vec3 N, vec3 V)
{
#ifdef FX_LIGHT_SPECULAR
    vec3 H = normalize(u_fxLight[0].xyz + V);
    float ndl = clamp(dot(N, u_fxLight[0].xyz) * 4.0, 0.0, 1.0);
    return fxLightColor(u_fxLight[1].rgb) * (pow(max(dot(N, H), 0.0), u_fxLight[0].w) * u_fxLight[1].w * ndl);
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

#ifdef FX_LIGHT_NORMALMAP
// Sampler stage 14 (fx::kNormalMapStage) is reserved for the derived normal map (RG8: x, y of the tangent-space normal).
SAMPLER2D(s_fxNormalMap, 14);

// N: unit interpolated normal, P: position (both view space), uv: the height texture's coordinates.
// The tangent frame comes from screen-space derivatives of P and uv (a cotangent frame), so meshes need no
// tangents and mirrored or rotated texture mappings are handled by construction.
vec3 fxPerturbNormal(vec3 N, vec3 P, vec2 uv)
{
    vec3 dpx = dFdx(P);
    vec3 dpy = dFdy(P);
    vec2 dux = dFdx(uv);
    vec2 duy = dFdy(uv);
    vec3 px = cross(dpy, N);
    vec3 py = cross(N, dpx);
    vec3 T = px * dux.x + py * duy.x; // direction of growing u on the surface
    vec3 B = px * dux.y + py * duy.y; // direction of growing v
    float scale = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    vec2 n = (texture2D(s_fxNormalMap, uv).rg * 2.0 - vec2_splat(1.0)) * u_fxLight[5].x;
    float nz = sqrt(max(1.0 - dot(n, n), 0.0));
    return normalize(T * (scale * n.x) + B * (scale * n.y) + N * nz);
}

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
    return clamp(1.0 + 1.6 * (dot(N, L) - 0.5), 0.55, 1.5);
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

vec3 fxPointSpecular(vec3 N, vec3 P, vec3 V)
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
        sum += fxLightColor(u_fxPoint[4 * i + 1].rgb) * (pow(max(dot(N, H), 0.0), u_fxLight[0].w) * u_fxLight[1].w * ndl * light.w);
    }
#endif
    return sum;
}
#endif // FX_LIGHT_POINT

#ifdef FX_LIGHT_SHADOW
// Sampler stage 13 (fx::kShadowMapStage) is reserved for the shadow map in every shader that defines FX_LIGHT_SHADOW.
SAMPLER2DSHADOW(s_fxShadowMap, 13);
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
    return 1.0 - (1.0 - lit) * u_fxShadow.y * inside;
}
#else
#define fxSunShadow(P, N) 1.0
#endif
#endif
