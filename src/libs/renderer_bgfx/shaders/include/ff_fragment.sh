// Fixed-function texture-stage combiner. Included by fs_ff*.sc; STAGEn_SAMPLE
// macros select the sampler type of each stage (2D unless overridden).
//
// Graphics effects variants (src/effects/lighting.cpp picks them; none is used by the «Оригинал» preset):
//   FX_FF_LIT       per-pixel lighting of a lit draw (vs_ff_lit): the diffuse colour the stages read is
//                   re-lit per pixel with fx_lighting.sh (sun, hemisphere, point / spot lights, rim, translucency,
//                   specular with FX_LIGHT_SPECULAR); features the settings leave off arrive with zero strength.
//   FX_FF_BAKED     geometry with baked vertex colours (lighting off, vs_ff_lit): only dynamic point lights are
//                   added, as texture * light.
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

// Lamp shadows of interiors (src/effects/lamp_shadows.hpp, fx/lamp_shadows.hpp for the face convention).
// u_fxLampView: view space -> world. u_fxLamps[0]: x lamp count, y 1 = GL texture origin (bottom-left), z 1 / rows of
// the atlases, w the draw's ambient luminance; per lamp i at 1 + 4i: xyz position, w far plane | rgb colour, w range |
// att0, att1, att2, w slot | x baked share constant, y static texel, z dynamic texel.
uniform mat4 u_fxLampView;
uniform vec4 u_fxLamps[17];
SAMPLER2DSHADOW(s_fxLampDynamic, 10);
SAMPLER2DSHADOW(s_fxLampStatic, 11);

// 1 = the lamp reaches Pw, 0 = something stands between them (the location or a character).
float ffLampVisibility(int i, vec3 Pw)
{
    vec4 lamp = u_fxLamps[1 + 4 * i];
    vec3 L = Pw - lamp.xyz;
    vec3 a = abs(L);
    vec3 F;
    vec3 R;
    vec3 U;
    float face;
    if (a.x >= a.y && a.x >= a.z)
    {
        if (L.x >= 0.0) { face = 0.0; F = vec3(1.0, 0.0, 0.0); R = vec3(0.0, 0.0, -1.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 1.0; F = vec3(-1.0, 0.0, 0.0); R = vec3(0.0, 0.0, 1.0); U = vec3(0.0, 1.0, 0.0); }
    }
    else if (a.y >= a.z)
    {
        if (L.y >= 0.0) { face = 2.0; F = vec3(0.0, 1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, -1.0); }
        else { face = 3.0; F = vec3(0.0, -1.0, 0.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 0.0, 1.0); }
    }
    else
    {
        if (L.z >= 0.0) { face = 4.0; F = vec3(0.0, 0.0, 1.0); R = vec3(1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
        else { face = 5.0; F = vec3(0.0, 0.0, -1.0); R = vec3(-1.0, 0.0, 0.0); U = vec3(0.0, 1.0, 0.0); }
    }
    float major = max(dot(L, F), 0.31); // inside the near plane: lit
    float s = 1.0 / 1.05; // fx::kLampFaceGuard
    float u = dot(L, R) / major * s;
    float v = dot(L, U) / major * s;
    float tile = u_fxLamps[3 + 4 * i].w * 6.0 + face;
    float row = floor(tile / 6.0);
    float column = tile - row * 6.0;
    float x = (column + u * 0.5 + 0.5) / 6.0;
    float y = (row + 0.5 - v * 0.5) * u_fxLamps[0].z;
    if (u_fxLamps[0].y > 0.5)
        y = 1.0 - y;
    float far = lamp.w;
    float depth = far / (far - 0.3) * (1.0 - 0.3 / major) - 0.0015; // fx::kLampShadowNear, a small bias
    float t = u_fxLamps[4 + 4 * i].y;
    float lit = 0.25 * (shadow2D(s_fxLampStatic, vec3(x - t, y - t, depth)) + shadow2D(s_fxLampStatic, vec3(x + t, y - t, depth)) +
                        shadow2D(s_fxLampStatic, vec3(x - t, y + t, depth)) + shadow2D(s_fxLampStatic, vec3(x + t, y + t, depth)));
    return min(lit, shadow2D(s_fxLampDynamic, vec3(x, y, depth)));
}

// The lamp's diffuse light at Pw (the D3D formulas the game's lights use), without the shadow.
vec3 ffLampLight(int i, vec3 Pw, vec3 Nw)
{
    vec3 d = u_fxLamps[1 + 4 * i].xyz - Pw;
    float distance = length(d);
    vec4 color = u_fxLamps[2 + 4 * i];
    vec4 att = u_fxLamps[3 + 4 * i];
    if (distance > color.w)
        return vec3_splat(0.0);
    float ndl = max(dot(Nw, d / max(distance, 1e-4)), 0.0);
    return color.rgb * ndl / max(att.x + att.y * distance + att.z * distance * distance, 1e-3);
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

#ifdef FX_FF_BAKED
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
    // Two texels of a 512² face at 4 m along the normal: no self-shadowing at grazing light.
    vec3 Pw = mul(u_fxLampView, vec4(P + Ng * 0.06, 1.0)).xyz;
    vec3 Nw = normalize(mul(u_fxLampView, vec4(Ng, 0.0)).xyz);
    float aoVolume = ffAoVolume(P, Ng, Nw);
#ifdef FX_FF_LIT
    // The volume occludes the ambient part (the per-vertex sum carries the global ambient and the lights not lit per
    // pixel); the per-pixel sun and lamps keep their own visibility.
    float sunShadow = fxSunShadow(P, Ng);
    ffDiffuse = vec4(clamp(fxLit(v_color0.rgb * aoVolume, v_matDiffuse.rgb, fxSunDiffuse(N, V) * sunShadow + fxPointDiffuse(N, P),
                                 v_matAmbient.rgb, fxAmbient(N) * aoVolume), 0.0, 1.0), v_color0.a);
    // Lamp shadows: the light of a hidden lamp leaves (wherever the sum carried it: per vertex or per pixel).
    vec3 lampLost = vec3_splat(0.0);
    for (int i = 0; i < 4; ++i)
        if (float(i) < u_fxLamps[0].x)
            lampLost += ffLampLight(i, Pw, Nw) * (1.0 - ffLampVisibility(i, Pw));
    ffDiffuse.rgb = max(ffDiffuse.rgb - v_matDiffuse.rgb * lampLost, ffDiffuse.rgb * 0.2);
#else
    ffDiffuse = v_color0;
    vec4 tex0 = vec4_splat(0.0);
#endif
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
#endif
#ifdef FX_FF_BAKED
#ifdef FX_LIGHT_NORMALMAP
    current.rgb *= fxBakedRelief(Ng, N, V);
#endif
    // The baked colours of interiors are mostly the ambient of the room (local-testing/shadows/DESIGN.md §2.1).
    current.rgb *= aoVolume;
    // Lamp shadows: a hidden lamp takes its share of the baked light, A / (A + k * ambient), along.
    float lampKeep = 1.0;
    for (int i = 0; i < 4; ++i)
        if (float(i) < u_fxLamps[0].x)
        {
            float A = dot(ffLampLight(i, Pw, Nw), vec3(0.299, 0.587, 0.114));
            float share = A / (A + u_fxLamps[4 + 4 * i].x * max(u_fxLamps[0].w, 0.02));
            lampKeep -= share * (1.0 - ffLampVisibility(i, Pw));
        }
    current.rgb *= max(lampKeep, 0.2);
    current.rgb = fxLit(current.rgb, tex0.rgb, fxPointDiffuse(N, P), vec3_splat(0.0), vec3_splat(0.0));
#endif
    gl_FragColor = ffFinish(current);
    FF_SLOPE_DEPTH();
}
