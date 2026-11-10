// Fixed-function texture-stage combiner. Included by fs_ff*.sc; STAGEn_SAMPLE
// macros select the sampler type of each stage (2D unless overridden).
#include "ff_finish.sh"

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
    if (sel < 0.5) r = v_color0;
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
    if (op < 12.5) return mix(a2, a1, v_color0.a);
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
        vec4 c = ffCombine(cs.x, ffArg(cs.y, current, tex, temp), ffArg(cs.z, current, tex, temp), \
                           ffArg(cs.w, current, tex, temp), current, tex); \
        vec4 a = as.x < 1.5 ? current : ffCombine(as.x, ffArg(as.y, current, tex, temp), ffArg(as.z, current, tex, temp), \
                           ffArg(as.w, current, tex, temp), current, tex); \
        vec4 r = clamp(vec4(c.rgb, cs.x > 23.5 && cs.x < 24.5 ? c.a : a.a), 0.0, 1.0); \
        if (ms.x > 0.5) temp = r; else current = r; \
    } \
    else { running = false; }

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
    vec4 current = v_color0;
    vec4 temp = vec4_splat(0.0);
    bool running = true;
    FF_STAGE(0, STAGE0_SAMPLE)
    FF_STAGE(1, STAGE1_SAMPLE)
    FF_STAGE(2, STAGE2_SAMPLE)
    FF_STAGE(3, STAGE3_SAMPLE)
    FF_STAGE(4, STAGE4_SAMPLE)
    gl_FragColor = ffFinish(current);
}
