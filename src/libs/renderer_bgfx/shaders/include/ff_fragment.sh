// Fixed-function texture-stage combiner. Included by fs_ff*.sc; STAGEn_SAMPLE
// macros select the sampler type of each stage (2D unless overridden).
//
// Graphics effects variants (src/effects/lighting.cpp picks them; none is used by the «Оригинал» preset):
//   FX_FF_LIT       per-pixel lighting of a lit draw (vs_ff_lit): the diffuse colour the stages read is
//                   re-lit per pixel with fx_lighting.sh (sun, hemisphere, point / spot lights, rim, translucency,
//                   specular with FX_LIGHT_SPECULAR); features the settings leave off arrive with zero strength.
//   FX_FF_BAKED     geometry with baked vertex colours (lighting off, vs_ff_lit): only dynamic point lights are
//                   added, as texture * light.
//   FX_FF_COVERAGE  alpha to coverage instead of the alpha-test discard (fx_coverage.sh); in the FX_FF_LIT /
//                   FX_FF_BAKED variants it is switched per draw by u_fxCoverage.z.
#include "ff_finish.sh"

#if defined(FX_FF_LIT) || defined(FX_FF_BAKED)
#define FX_FF_PIXEL_LIGHTING 1
#include "fx_lighting.sh"
// x: two-sided surface whose back side shows the light passing through (normal flipped towards the viewer)
uniform vec4 u_fxFf;
vec4 ffDiffuse;
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
    ffDiffuse = vec4(clamp(fxLit(v_color0.rgb, v_matDiffuse.rgb, fxSunDiffuse(N, V) + fxPointDiffuse(N, P),
                                 v_matAmbient.rgb, fxAmbient(N)), 0.0, 1.0), v_color0.a);
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
    current.rgb = fxAddSpecular(current.rgb, fxSunSpecular(N, V) + fxPointSpecular(N, P, V) + fxRim(N, V));
#endif
#ifdef FX_FF_BAKED
    current.rgb = fxLit(current.rgb, tex0.rgb, fxPointDiffuse(N, P), vec3_splat(0.0), vec3_splat(0.0));
#endif
    gl_FragColor = ffFinish(current);
#if STORM_SLOPE_BIAS
    // D3D9: depth + SlopeScaleDepthBias * max(|dz/dx|, |dz/dy|); the constant D3DRS_DEPTHBIAS part is
    // already in the projection. Window depth is [0, 1] like D3D's.
    float slope = max(abs(dFdx(gl_FragCoord.z)), abs(dFdy(gl_FragCoord.z)));
    gl_FragDepth = clamp(gl_FragCoord.z + u_ffDepthBias.x * slope, 0.0, 1.0);
#endif
}
