// Output stage shared by the fixed-function combiner and the ps_1_x ports:
// user clip plane, alpha test, specular add and fog, exactly as D3D9 applies
// them after the pixel pipeline. Call gl_FragColor = ffFinish(colour).
#ifndef FF_FINISH_SH
#define FF_FINISH_SH
#include "storm_common.sh"
#include "fx_aerial.sh"

// [0] texture factor  [1] fog colour rgb, fog mode (0 off, 1 exp, 2 exp2, 3 linear, 4 factor passthrough,
//     5 exp with the effects' aerial perspective: fx_aerial.sh fxAerialFog, height above the eye in v_fog.w)
//     (mode 5: fog start, fog end hold the haze, see Lighting::aerial_fog)
// [2] fog start, fog end, fog density, alpha ref  [3] alpha func (0 = off, else D3DCMP), specular add, stage count, fog depth is in v_fog.z
uniform vec4 u_ffFs[4];
uniform vec4 u_ffDepthBias; // x: D3DRS_SLOPESCALEDEPTHBIAS (STORM_SLOPE_BIAS shaders only)

#ifdef FX_FF_COVERAGE
#include "fx_coverage.sh"
// Alpha to coverage (FX_FF_COVERAGE variants of fs_ff): xy: size of the stage-0 texture's top mip, z: on for
// this draw (the lit variants carry it for every draw; the plain coverage variant is only used with it on).
uniform vec4 u_fxCoverage;
#endif

vec4 ffFinish(vec4 color)
{
    // FF_OPAQUE: the twin of a shader for draws without a clip plane and without an alpha test (the shim picks it,
    // draw.cpp opaque_twin): a shader that can discard loses early depth and hidden-surface removal on tile GPUs.
#ifndef FF_OPAQUE
    if (v_fog.y < 0.0) discard;
    float func = u_ffFs[3].x;
#ifdef FX_FF_COVERAGE
    // Only for GREATER / GREATEREQUAL tests against the stage-0 texture's alpha (the CPU checks it).
    if (func > 0.5 && u_fxCoverage.z > 0.5)
    {
        color.a = fxCoverageAlpha(color.a, u_ffFs[2].w, v_texcoord0.xy, u_fxCoverage.xy);
        func = 0.0;
    }
#endif
    if (func > 0.5)
    {
        float a = color.a;
        float ref = u_ffFs[2].w;
        bool pass = true;
        if (func < 1.5) pass = false;
        else if (func < 2.5) pass = a < ref;
        else if (func < 3.5) pass = a == ref;
        else if (func < 4.5) pass = a <= ref;
        else if (func < 5.5) pass = a > ref;
        else if (func < 6.5) pass = a != ref;
        else if (func < 7.5) pass = a >= ref;
        if (!pass) discard;
    }
#endif
    if (u_ffFs[3].y > 0.5) color.rgb += v_color1.rgb;
    float mode = u_ffFs[1].w;
    if (mode > 4.5)
        color.rgb = fxAerialFog(color.rgb, u_ffFs[1].rgb, u_ffFs[2].z, v_fog.x, v_fog.w, u_ffFs[2].xy);
    else if (mode > 0.5)
    {
        float f;
        float d = u_ffFs[3].w > 0.5 ? v_fog.z : v_fog.x;
        if (mode < 1.5) f = exp(-u_ffFs[2].z * d);
        else if (mode < 2.5) f = exp(-(u_ffFs[2].z * d) * (u_ffFs[2].z * d));
        else if (mode < 3.5) f = (u_ffFs[2].y - d) / max(u_ffFs[2].y - u_ffFs[2].x, 1e-6);
        else f = d;
        color.rgb = mix(u_ffFs[1].rgb, color.rgb, clamp(f, 0.0, 1.0));
    }
    return color;
}

// D3DRS_SLOPESCALEDEPTHBIAS (bgfx has no polygon offset): depth + bias * max(|dz/dx|, |dz/dy|), written by the
// shaders compiled with STORM_SLOPE_BIAS (it disables early depth, so only the draws that set the bias use them).
// The constant D3DRS_DEPTHBIAS part is already in the projection. Window depth is [0, 1] like D3D's.
#if STORM_SLOPE_BIAS
#define FF_SLOPE_DEPTH() gl_FragDepth = clamp(gl_FragCoord.z + u_ffDepthBias.x * max(abs(dFdx(gl_FragCoord.z)), abs(dFdy(gl_FragCoord.z))), 0.0, 1.0)
#else
#define FF_SLOPE_DEPTH()
#endif

#endif
