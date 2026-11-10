$input a_position, a_normal, a_texcoord0
$output v_wpos, v_wnormal, v_misc
// Sea surface. Variants:
//   FX_WATER_MESH      vertices displaced by the engine (SEA::SeaVertex: position, normal, uv)
//   FX_WATER_FIELD     camera-centred grid displaced from the engine's two height-field layers
//   FX_WATER_GERSTNER  the same grid displaced by a sum of Gerstner waves
// v_misc: x = height above y 0, y = view depth, zw = detail texture coordinates.
#include "fx_common.sh"

uniform vec4 u_fxWaterVs; // MESH: xz world offset in xy. Grid: xy level origin (world x, z), z cell size, w unused

#ifdef FX_WATER_FIELD
SAMPLER2D(s_fxLayer0, 8);
SAMPLER2D(s_fxLayer1, 9);
// [0], [1] per layer: x scale (texels per metre), y amplitude, zw scroll (metres)
// [2] x: 3 * PosShift, y: BumpScale
// [3] xy: mip level per layer for this grid level, zw: for the next coarser level
uniform vec4 u_fxWave[4];

void fxDisplace(vec2 p, float coarse, out vec3 pos, out vec3 nrm)
{
    vec4 l0 = u_fxWave[0];
    vec4 l1 = u_fxWave[1];
    vec2 lod = mix(u_fxWave[3].xy, u_fxWave[3].zw, coarse);
    // Linear filtering at (texel + 0.5) / 128 reproduces the engine's bilinear sample of texels i, i + 1.
    vec4 s0 = texture2DLod(s_fxLayer0, ((p + l0.zw) * l0.x + vec2_splat(0.5)) * (1.0 / 128.0), lod.x);
    vec4 s1 = texture2DLod(s_fxLayer1, ((p + l1.zw) * l1.x + vec2_splat(0.5)) * (1.0 / 128.0), lod.y);
    float ny = sqrt(max(1.0 - dot(s0.yz, s0.yz), 0.0)) + sqrt(max(1.0 - dot(s1.yz, s1.yz), 0.0));
    vec2 nxz = l0.x * l0.y * s0.yz + l1.x * l1.y * s1.yz;
    nrm = normalize(vec3(nxz.x, ny, nxz.y));
    pos = vec3(p.x - nrm.x * u_fxWave[2].x, l0.y * s0.x + l1.y * s1.x, p.y - nrm.z * u_fxWave[2].x);
}
#define FX_BUMP_SCALE u_fxWave[2].y
#endif

#ifdef FX_WATER_GERSTNER
// Per wave i: [2i] xy travel direction, z wave number, w amplitude
//             [2i+1] x horizontal amplitude, y phase - omega * time
// [12] x: mean level, y: BumpScale
uniform vec4 u_fxWave[13];

void fxDisplace(vec2 p, float coarse, out vec3 pos, out vec3 nrm)
{
    pos = vec3(p.x, u_fxWave[12].x, p.y);
    vec3 dx = vec3(1.0, 0.0, 0.0); // dP/dx as (x, y, z)
    vec3 dz = vec3(0.0, 0.0, 1.0); // dP/dz
    for (int i = 0; i < 6; ++i)
    {
        vec4 a = u_fxWave[2 * i];
        vec4 b = u_fxWave[2 * i + 1];
        float theta = a.z * dot(a.xy, p) + b.y;
        float s = sin(theta);
        float c = cos(theta);
        pos.xz += b.x * c * a.xy;
        pos.y += a.w * s;
        float kh = a.z * b.x * s;
        float ka = a.z * a.w * c;
        dx += vec3(-kh * a.x * a.x, ka * a.x, -kh * a.x * a.y);
        dz += vec3(-kh * a.x * a.y, ka * a.y, -kh * a.y * a.y);
    }
    nrm = normalize(cross(dz, dx));
}
#define FX_BUMP_SCALE u_fxWave[12].y
#endif

void main()
{
#ifdef FX_WATER_MESH
    vec3 pos = a_position + vec3(u_fxWaterVs.x, 0.0, u_fxWaterVs.y);
    vec3 nrm = a_normal;
    vec2 detailUv = a_texcoord0.xy;
#else
    // a_position: xy grid cell coordinates, z: 0 inside, 1 on the outer edge,
    // 2 / 3 on the outer edge between two vertices of the next coarser level (along x / z).
    // Edge vertices are evaluated exactly as the coarser level evaluates its own, so the levels meet.
    vec2 p = u_fxWaterVs.xy + a_position.xy * u_fxWaterVs.z;
    float edge = a_position.z;
    vec3 pos;
    vec3 nrm;
    if (edge > 1.5)
    {
        // Average the coarser level's two vertices so the levels meet without cracks.
        vec2 d = edge > 2.5 ? vec2(0.0, u_fxWaterVs.z) : vec2(u_fxWaterVs.z, 0.0);
        vec3 posA;
        vec3 nrmA;
        fxDisplace(p - d, 1.0, posA, nrmA);
        fxDisplace(p + d, 1.0, pos, nrm);
        pos = 0.5 * (pos + posA);
        nrm = normalize(nrm + nrmA);
    }
    else
    {
        fxDisplace(p, edge > 0.5 ? 1.0 : 0.0, pos, nrm);
    }
    // Same detail coordinates as SEA::SSE_WaveXZBlock.
    vec2 detailUv = (p - nrm.xz * 5.3) * FX_BUMP_SCALE;
#endif
    gl_Position = mul(u_viewProj, vec4(pos, 1.0));
    v_wpos = pos;
    v_wnormal = nrm;
    v_misc = vec4(pos.y, gl_Position.w, detailUv);
}
