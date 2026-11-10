// Fixed-function vertex pipeline emulation, driven entirely by uniforms. Included by vs_ff.sc and, with
// FX_FF_LIT, by vs_ff_lit.sc: the variant for per-pixel lighting (src/effects/lighting.cpp), which also passes
// the view-space normal and position and the material colours to the fragment shader. Which lights it leaves
// to the fragment shader is decided on the CPU (their diffuse and specular colours arrive as zero here).
// FF_UNLIT (vs_ff_unlit.sc, draws without D3DRS_LIGHTING) leaves the lights and the material out of the
// program, so bgfx's GL backend does not re-upload them (53 vec4) with every uniform change of an unlit draw.
// FF_SKIN (vs_ff*_skin.sc) skins GEOS::AVERTEX0 vertices (one blend weight, two bone indices) with the bone
// palette in s_bones exactly like the former CPU kernel (src/libs/model/src/skinning.hpp), before world*view.
#include "storm_common.sh"

// World * view comes through bgfx's per-draw transform (u_model[0], bgfx::setTransform): changing it
// does not re-upload the uniforms below, which the GL backend does for any other uniform change.
uniform mat4 u_ffProj; // projection (or screen matrix for XYZRHW) with the D3D->backend clip fix-up
// [0] x: rhw  y: lighting  z: has diffuse  w: has specular
// [1] x: normalize normals  y: light count  z: range fog  w: has normal
// [2] x: diffuse source  y: ambient source  z: emissive source  w: specular source (0 material, 1 color0, 2 color1)
// [3] x: specular enable  y: material power  z: fog passthrough (0 = eye depth)  w: point size
uniform vec4 u_ffVs[4];
uniform vec4 u_ffClipPlane;   // view-space plane, all zero when disabled
#ifndef FF_UNLIT
uniform vec4 u_ffMaterial[4]; // diffuse, ambient, specular, emissive
uniform vec4 u_ffAmbient;
uniform vec4 u_ffLightPos[8];      // view space xyz, w: type (1 point, 2 spot, 3 directional)
uniform vec4 u_ffLightDir[8];      // view space xyz (normalized), w: range
uniform vec4 u_ffLightDiffuse[8];
uniform vec4 u_ffLightAmbient[8];
uniform vec4 u_ffLightSpecular[8];
uniform vec4 u_ffLightAtt[8];      // att0, att1, att2, unused
#endif
// x: texcoord index  y: texgen (0 vertex, 1 normal, 2 position, 3 reflection)  z: transform count (0 off)  w: projected
uniform vec4 u_ffTexCfg[5];
uniform mat4 u_ffTexMtx[5];

vec4 stageCoord(int i, vec4 t0, vec4 t1, vec4 t2, vec4 t3, vec3 P, vec3 N)
{
    vec4 cfg = u_ffTexCfg[i];
    vec4 src;
    if (cfg.y < 0.5)
    {
        if (cfg.x < 0.5) src = t0;
        else if (cfg.x < 1.5) src = t1;
        else if (cfg.x < 2.5) src = t2;
        else src = t3;
    }
    else if (cfg.y < 1.5) src = vec4(N, 1.0);
    else if (cfg.y < 2.5) src = vec4(P, 1.0);
    else src = vec4(reflect(normalize(P), N), 1.0);
    if (cfg.z > 0.5)
    {
        src = mul(u_ffTexMtx[i], src);
        if (cfg.w > 0.5 && cfg.z < 3.5) src.w = cfg.z < 2.5 ? src.y : src.z;
    }
    return src;
}

#ifdef FF_SKIN
// The palette holds 3 texels per bone (row = bone): texel k = (m[k], m[4+k], m[8+k], m[12+k]) of the bone
// matrix, texel 0 negated (the CPU kernel negates elements 0, 4, 8 and 12 after blending); see
// src/libs/model/include/skinning_palette.hpp.
SAMPLER2D(s_bones, 13);

vec4 blendedRow(int row, int bone0, int bone1, float weight)
{
    return texelFetch(s_bones, ivec2(row, bone0), 0) * weight + texelFetch(s_bones, ivec2(row, bone1), 0) * (1.0 - weight);
}

// Position and normal alike, the normal with the translation included (as the CPU kernel did).
vec3 skinPoint(vec4 r0, vec4 r1, vec4 r2, vec3 v)
{
    return vec3((r0.x * v.x + r0.y * v.y) + (r0.z * v.z + r0.w),
                (r1.x * v.x + r1.y * v.y) + (r1.z * v.z + r1.w),
                (r2.x * v.x + r2.y * v.y) + (r2.z * v.z + r2.w));
}
#endif

void main()
{
#ifdef FF_SKIN
    vec4 r0 = blendedRow(0, a_indices.x, a_indices.y, a_weight.x);
    vec4 r1 = blendedRow(1, a_indices.x, a_indices.y, a_weight.x);
    vec4 r2 = blendedRow(2, a_indices.x, a_indices.y, a_weight.x);
    vec3 modelPos = skinPoint(r0, r1, r2, a_position.xyz);
    vec3 modelNormal = skinPoint(r0, r1, r2, a_normal);
#else
    vec3 modelPos = a_position.xyz;
    vec3 modelNormal = a_normal;
#endif
    vec4 diffuse = u_ffVs[0].z > 0.5 ? D3DCOLOR_TO_RGBA(a_color0) : vec4_splat(1.0);
    vec4 specular = u_ffVs[0].w > 0.5 ? D3DCOLOR_TO_RGBA(a_color1) : vec4_splat(0.0);
    vec3 P = vec3_splat(0.0);
    vec3 N = vec3(0.0, 0.0, 1.0);
    if (u_ffVs[0].x > 0.5)
    {
        float w = abs(a_position.w) > 1e-12 ? 1.0 / a_position.w : 1.0;
        vec4 clip = mul(u_ffProj, vec4(a_position.xyz, 1.0));
        gl_Position = clip * w;
        v_fog = vec4(0.0, 1.0, 0.0, 0.0);
#ifdef FX_FF_LIT
        v_normal = vec4(0.0, 0.0, -1.0, 0.0);
        v_position = vec4_splat(0.0);
        v_matDiffuse = vec4_splat(0.0);
        v_matAmbient = vec4_splat(0.0);
#endif
    }
    else
    {
        vec4 viewPos = mul(u_model[0], vec4(modelPos, 1.0));
        P = viewPos.xyz;
        gl_Position = mul(u_ffProj, viewPos);
        if (u_ffVs[1].w > 0.5)
        {
            N = mul(u_model[0], vec4(modelNormal, 0.0)).xyz;
            if (u_ffVs[1].x > 0.5) N = normalize(N);
        }
        float fogDepth = u_ffVs[1].z > 0.5 ? length(P) : abs(P.z);
        if (u_ffVs[3].z > 0.5) fogDepth = specular.a; // fog enabled without a mode: the factor comes with the vertex
        float clipDist = dot(u_ffClipPlane.xyz, P) + u_ffClipPlane.w;
        if (dot(u_ffClipPlane, u_ffClipPlane) == 0.0) clipDist = 1.0;
        v_fog = vec4(fogDepth, clipDist, 0.0, 0.0);
#ifdef FX_FF_LIT
        v_normal = vec4(N, 0.0);
        v_position = vec4(P, 1.0);
        v_matDiffuse = vec4_splat(0.0);
        v_matAmbient = vec4_splat(0.0);
#endif
#ifndef FF_UNLIT
        if (u_ffVs[0].y > 0.5)
        {
            vec4 matDiffuse = u_ffVs[2].x < 0.5 ? u_ffMaterial[0] : (u_ffVs[2].x < 1.5 ? diffuse : specular);
            vec4 matAmbient = u_ffVs[2].y < 0.5 ? u_ffMaterial[1] : (u_ffVs[2].y < 1.5 ? diffuse : specular);
            vec4 matEmissive = u_ffVs[2].z < 0.5 ? u_ffMaterial[3] : (u_ffVs[2].z < 1.5 ? diffuse : specular);
            vec4 matSpecular = u_ffVs[2].w < 0.5 ? u_ffMaterial[2] : (u_ffVs[2].w < 1.5 ? diffuse : specular);
            vec3 accDiffuse = vec3_splat(0.0);
            vec3 accAmbient = u_ffAmbient.rgb;
            vec3 accSpecular = vec3_splat(0.0);
            int count = int(u_ffVs[1].y + 0.5);
            for (int i = 0; i < count; ++i)
            {
                vec3 L;
                float att = 1.0;
                if (u_ffLightPos[i].w > 2.5)
                {
                    L = -u_ffLightDir[i].xyz;
                }
                else
                {
                    vec3 d = u_ffLightPos[i].xyz - P;
                    float dist = length(d);
                    L = d / max(dist, 1e-6);
                    att = dist > u_ffLightDir[i].w ? 0.0
                        : 1.0 / max(u_ffLightAtt[i].x + u_ffLightAtt[i].y * dist + u_ffLightAtt[i].z * dist * dist, 1e-6);
                }
                float ndl = max(dot(N, L), 0.0);
                accAmbient += att * u_ffLightAmbient[i].rgb;
                accDiffuse += att * ndl * u_ffLightDiffuse[i].rgb;
                if (u_ffVs[3].x > 0.5 && ndl > 0.0)
                {
                    vec3 H = normalize(L + vec3(0.0, 0.0, -1.0));
                    accSpecular += att * pow(max(dot(N, H), 0.0), max(u_ffVs[3].y, 1e-4)) * u_ffLightSpecular[i].rgb;
                }
            }
            vec3 lit = matEmissive.rgb + matAmbient.rgb * accAmbient + matDiffuse.rgb * accDiffuse;
            diffuse = vec4(clamp(lit, 0.0, 1.0), clamp(matDiffuse.a, 0.0, 1.0));
#ifdef FX_FF_LIT
            v_matDiffuse = matDiffuse;
            v_matAmbient = matAmbient;
#endif
            specular = vec4(clamp(matSpecular.rgb * accSpecular, 0.0, 1.0), specular.a);
        }
#endif
    }
    v_color0 = diffuse;
    v_color1 = specular;
    v_texcoord0 = stageCoord(0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, P, N);
    v_texcoord1 = stageCoord(1, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, P, N);
    v_texcoord2 = stageCoord(2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, P, N);
    v_texcoord3 = stageCoord(3, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, P, N);
    v_texcoord4 = stageCoord(4, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, P, N);
    gl_PointSize = u_ffVs[3].w;
}
