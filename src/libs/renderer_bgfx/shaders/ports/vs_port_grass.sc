$input a_position
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader grass_main (vs_1_1) of src/techniques/effects/grass.fx, drawing from the static vertex
// buffer of src/libs/location/src/grass.cpp: first the per-blade wind, the bending around characters and the LOD blend
// the game once computed on the CPU for every vertex of every frame (Grass::RenderBlock, as it was until the grass
// moved to the GPU), then grass_main's instructions, one D3D9 instruction per line. Which blades are drawn the CPU
// still decides per block (the indices of Grass::DrawRuns).
//
// Vertex: four 16-bit integers (Grass::GpuVertex, 8 bytes):
//   x, z: the blade's place in the buffer's window of blocks (Grass::UpdateWindow) in 1/32 of a block (block * 32 +
//         the step in the block, as the .grs file keeps it), the corner's x / z offset in the sign bit;
//   y: the blade's base height in 16383 steps between the lowest and the highest blade of the map, times 4, plus the
//      highest LOD that still draws the blade;
//   w: the blade's frame, height, width and angle, 4 bits each (the 0..15 of the .grs file).
// The position comes out as Grass::LoadData computes it (the step times GRASS_STEP plus the block's corner), the
// height within half a step.
//
// c0..c41 as for grass_main, set by Grass::Realize. Per frame (Grass::Realize):
//   c42 wind addition x, z, wind direction x, z times the direction factor
//   c43 wind direction x, z, wind force, the wind factor of the low quality
//   c44 phase 0, 3, 4, cos-phase 1     c45 sin-phase 2, 5, 6, wind power
//   c46 force^10 term, amplitude factor, quality (0..2), number of characters
//   c47 map start x, z, camera x, z    c48 min / max visible distance, min LOD, the lowest blade height
//   c49 the height step, the window's first block x, z on the minimap
//   c50 + 2i, c51 + 2i: character i position, its inertial position (GRASS_CHARACTERS, as in grass.cpp): the
//   characters touching a drawn blade
#define PORT_VSC_COUNT 82 // registers c0..c81 (the GL program uploads exactly these)
#define GRASS_CHARACTERS 16
#include "port_vs.sh"

void main()
{
    // ---- the vertex (Grass::CreateGpuBuffer) ----
    // Convert the integer attribute numerically before unpacking (grass.varying.def). The bytes stay SHORT4 on the CPU.
    vec4 packedPosition = vec4(a_position);
    float cornerX = packedPosition.x < 0.0 ? 1.0 : 0.0;
    float cornerZ = packedPosition.y < 0.0 ? 1.0 : 0.0;
    float xq = packedPosition.x + cornerX * 32768.0;
    float zq = packedPosition.y + cornerZ * 32768.0;
    float wx = floor(xq / 32.0); // the block in the window
    float wz = floor(zq / 32.0);
    float mx = wx + u_vsc[49].y; // the block on the minimap
    float mz = wz + u_vsc[49].z;
    float hq = packedPosition.z + 32768.0;
    float hs = floor(hq / 4.0);
    int maxLod = int(hq - hs * 4.0);
    float packed = packedPosition.w < 0.0 ? packedPosition.w + 65536.0 : packedPosition.w;
    float nib0 = mod(packed, 16.0);
    float nib1 = mod(floor(packed / 16.0), 16.0);
    float nib2 = mod(floor(packed / 256.0), 16.0);
    float nib3 = floor(packed / 4096.0);
    // The blade's base (GRSMapElementEx: the step times GRASS_STEP plus the block's corner)
    vec4 v0 = vec4((xq - wx * 32.0) * (1.0 / 32.0) + (u_vsc[47].x + mx), u_vsc[48].w + hs * u_vsc[49].x,
                   (zq - wz * 32.0) * (1.0 / 32.0) + (u_vsc[47].y + mz), 1.0);
    // GRSMapElementEx::data (frame, h, w, ang bytes of 17 * the 0..15 values) as COLOR0 gave it to grass_main
    vec4 v1 = vec4(nib2, nib1, nib0, nib3) * (17.0 / 255.0);
    // The corner as grass_main's COLOR1
    vec4 v2 = vec4(cornerX, cornerZ, 0.0, 0.0);
    vec4 v3;
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    int a0 = 0;
    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oD0 = vec4_splat(1.0);
    vec4 oD1 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT1 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT3 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT4 = vec4(0.0, 0.0, 0.0, 1.0);
    float oFog = 1.0;

    // ---- the block's LOD (Grass::RenderBlock(camPos, ...), (GRSMiniMapElement &, kLod)) ----
    float scale = u_vsc[38].w;
    float cx = scale * (u_vsc[47].x + (mx + 0.5));
    float cz = scale * (u_vsc[47].y + (mz + 0.5));
    float dist = (cx - u_vsc[47].z) * (cx - u_vsc[47].z) + (cz - u_vsc[47].w) * (cz - u_vsc[47].w);
    float kLod = (sqrt(dist) - u_vsc[48].x) / (u_vsc[48].y - u_vsc[48].x);
    if (kLod < u_vsc[48].z)
        kLod = u_vsc[48].z;
    kLod = kLod * 3.9999;
    int lod = int(kLod); // truncation, as the C++ cast
    int quality = int(u_vsc[46].z + 0.5);
    if (lod < quality)
        lod = quality;
    float kBlend = clamp(1.0 - (kLod - float(lod)), 0.0, 1.0);
    // Grass::DrawRuns draws the blades the block's LOD draws; those the next LOD keeps too are opaque.
    float alpha = maxLod >= lod + 1 ? 1.0 : kBlend;

    // ---- the blade's wind ----
    float x = v0.x, y = v0.y, z = v0.z;
    float winx = sin(x * u_vsc[44].w + z * 0.06 + u_vsc[44].x);
    float winz = cos(x * 0.11 + z * u_vsc[45].x + u_vsc[44].x);
    if (quality <= 1)
    {
        float dx = u_vsc[43].x * x * 0.5 + u_vsc[44].y;
        float dz = u_vsc[43].y * z * 0.5 + u_vsc[44].z;
        float k1 = sin(dx + dz);
        float a1 = 0.001 + u_vsc[45].y * sin((dx + dz) * 0.5);
        float k2 = cos(u_vsc[43].y * x * (a1 + u_vsc[45].z) - u_vsc[43].x * z * a1);
        float base = k1 * k2 * 0.5 + 0.5;
        float kamp = (base > 0.0 ? pow(base, u_vsc[45].w) : 0.0) + u_vsc[46].x + u_vsc[43].z * 0.7;
        if (kamp > 1.0)
            kamp = 1.0;
        kamp *= u_vsc[46].y;
        winx = (0.9 * winx + u_vsc[42].z) * kamp + u_vsc[42].x;
        winz = (0.9 * winz + u_vsc[42].w) * kamp + u_vsc[42].y;
        // The characters bend the grass around them.
        int characters = int(u_vsc[46].w + 0.5);
        for (int i = 0; i < GRASS_CHARACTERS; ++i)
        {
            if (i >= characters)
                break;
            vec3 pos = u_vsc[50 + 2 * i].xyz;
            vec3 last = u_vsc[51 + 2 * i].xyz;
            if (abs(pos.y - y) < 0.7)
            {
                float pldx = ((x - pos.x) + (x - last.x)) * 0.5;
                float pldz = ((z - pos.z) + (z - last.z)) * 0.5;
                float dst = pldx * pldx + pldz * pldz;
                if (dst < 0.8 * 0.8 && dst > 0.0)
                {
                    dst = sqrt(dst);
                    float k = 0.5 / dst;
                    pldx *= k;
                    pldz *= k;
                    dst *= 1.0 / 0.8;
                    dst = 1.0 - dst;
                    dst *= dst;
                    winx += (pldx - winx) * dst;
                    winz += (pldz - winz) * dst;
                }
            }
        }
        float wLen = sqrt(winx * winx + winz * winz);
        if (wLen > 1.2)
        {
            wLen = 1.2 / wLen;
            winx *= wLen;
            winz *= wLen;
        }
        winx *= 0.4;
        winz *= 0.4;
    }
    else
    {
        winx *= u_vsc[43].w;
        winz *= u_vsc[43].w;
    }
    v3 = vec4(winx, winz, alpha, 0.0);

    // ---- grass_main ----
    r0.xy = v3.xy * u_vsc[36].xy;                // mul r0.xy, v3, c36
    r0.w = r0.y + r0.x;                          // add r0.w, r0.yyyy, r0.xxxx
    r0.w = max(-r0.w, u_vsc[40].y);              // max r0.w, -r0.wwww, c40.yyyy
    r2.xw = v1.zw * u_vsc[40].xx;                // mul r2.xw, v1.zyzw, c40.xxxx
    r0.w = min(r0.w, u_vsc[40].z);               // min r0.w, r0.wwww, c40.zzzz
    a0 = portAddr(r2.x);                         // mov a0.x, r2.xxxx
    r0.y = u_vsc[0 + a0].z;                      // mov r0.y, c0[a0.x].zzzz
    r1.w = r0.y * u_vsc[40].w;                   // mul r1.w, r0.yyyy, c40.wwww
    r0.w = r0.w * u_vsc[39].x + r1.w;            // mad r0.w, r0.wwww, c39.xxxx, r1.wwww
    r1.w = r0.w * v2.y;                          // mul r1.w, r0.wwww, v2.yyyy
    r0.xy = v3.xy * v3.xy;                       // mul r0.xy, v3, v3
    r0.w = r0.y + r0.x;                          // add r0.w, r0.yyyy, r0.xxxx
    r0.xy = v1.xy * u_vsc[41].xy + u_vsc[41].zw; // mad r0.xy, v1, c41, c41.zwzw
    r1.w = r1.w * u_vsc[39].y;                   // mul r1.w, r1.wwww, c39.yyyy
    r0.w = -r0.w + r0.y;                         // add r0.w, -r0.wwww, r0.yyyy
    r0.xy = r0.xx * u_vsc[0 + a0].xy;            // mul r0.xy, r0.xxxx, c0[a0.x]
    r0.w = portRsq(r0.w);                        // rsq r0.w, r0.wwww
    r0.w = portRcp(r0.w);                        // rcp r0.w, r0.wwww
    r3.w = v2.x + u_vsc[40].y;                   // add r3.w, v2.xxxx, c40.yyyy
    r3.xy = v2.xy;                               // mov r3.xy, v2
    r0.w = r0.w * r3.y;                          // mul r0.w, r0.wwww, r3.yyyy
    r0.w = v0.y * u_vsc[38].w + r0.w;            // mad r0.w, v0.yyyy, c38.wwww, r0.wwww
    r1.xy = r0.xy * -r3.ww;                      // mul r1.xy, r0, -r3.wwww
    r1.xy = v0.xz * u_vsc[38].ww + r1.xy;        // mad r1.xy, v0.xzzw, c38.wwww, r1.xyyy
    r0 = r0.wwww * u_vsc[33];                    // mul r0, r0.wwww, c33
    r2.xy = v3.xy * r3.yy + r1.xy;               // mad r2.xy, v3, r3.yyyy, r1
    r1.xyz = u_vsc[38].xyz;                      // mov r1.xyz, c38
    oD0.xyz = r1.xyz * r1.www + u_vsc[37].xyz;   // mad oD0.xyz, r1, r1.wwww, c37
    r0 = u_vsc[32] * r2.xxxx + r0;               // mad r0, c32, r2.xxxx, r0
    r1.zw = u_vsc[39].zw;                        // mov r1.zw, c39
    a0 = portAddr(r2.w);                         // mov a0.x, r2.wwww
    oT0.xy = r3.xy * r1.zw + u_vsc[16 + a0].xy;  // mad oT0.xy, r3, r1.zwzw, c16[a0.x]
    r0 = u_vsc[34] * r2.yyyy + r0;               // mad r0, c34, r2.yyyy, r0
    oPos = r0 + u_vsc[35];                       // add oPos, r0, c35
    oD0.w = v3.z;                                // mov oD0.w, v3.zzzz


    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
