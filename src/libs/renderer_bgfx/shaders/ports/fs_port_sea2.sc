$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea2_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx.
// Stage 0: animated bump volume texture, stage 3: environment cube map.
//
// On top of the original (effects/sea.hpp sets u_seaFx; all off reproduces the D3D9 shader except the first point):
// - the reflected ray never points below the horizon: the cube's -Y face is never rendered above water, and the
//   original sampled its black there (dark specks near the horizon); a ray reflected downwards meets the sea again,
//   whose colour at the horizon the side faces hold;
// - band limit: the bump detail fades towards the flat surface as one pixel covers more texels of the bump
//   texture (far and grazing water), so the reflection does not shimmer at the horizon;
// - soft edges: the water fades out where it is shallower than kEdgeDepth in front of the scene behind it
//   (s_seaDepth, the scene's view distance packed into RGB8 by fs_fx_sea_depth);
// - specular: a sun highlight from the bumped normal over the game's own sun road (optional).
#include "port_ps.sh"
SAMPLER3D(s_vol0, 8);
SAMPLERCUBE(s_cube3, 11);
SAMPLER2D(s_seaDepth, 14);
// [0] x: band limit, y: soft edges, z: specular (each 0 or 1), w: far plane of the packed scene distance
// [1] xyz: towards the sun (world), w: specular power   [2] rgb: sun colour * strength, w: 1 / edge depth (metres)
uniform vec4 u_seaFx[3];

void main()
{
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture3D(s_vol0, v_texcoord0.xyz);                           // tex t0
    vec3 n = PS_BX2(t0.rgb);                                                //   t0_bx2
    // Texels of the bump texture's top level under one pixel: 1 close up, many at the horizon.
    vec2 footprint = fwidth(v_texcoord0.xy) * 128.0;
    float detail = 1.0 - u_seaFx[0].x * clamp(log2(max(max(footprint.x, footprint.y), 1.0)) * 0.3, 0.0, 0.85);
    n = mix(vec3(0.0, 1.0, 0.0), n, detail);
    float m1 = dot(v_texcoord1.xyz, n);                                     // texm3x3pad t1, t0_bx2
    float m2 = dot(v_texcoord2.xyz, n);                                     // texm3x3pad t2, t0_bx2
    float m3 = dot(v_texcoord3.xyz, n);                                     // texm3x3vspec t3, t0_bx2
    vec3 eye = vec3(v_texcoord1.w, v_texcoord2.w, v_texcoord3.w);           //   eye vector from the w components
    vec3 R = portReflect(vec3(m1, m2, m3), eye);
    R.y = max(R.y, 0.0);
    vec4 t3 = textureCube(s_cube3, R);
    r0.rgb = PS11(v0.rgb * t3.rgb + v1.rgb);                                // mad r0.xyz, v0, t3, v1
    r0.a = v0.a;                                                            // mov r0.w, v0.wwww
    float highlight = pow(max(dot(normalize(R), u_seaFx[1].xyz), 0.0), u_seaFx[1].w);
    r0.rgb += u_seaFx[0].z * highlight * u_seaFx[2].rgb;

    gl_FragColor = ffFinish(clamp(r0, 0.0, 1.0));
    if (u_seaFx[0].y > 0.5)
    {
        // Blended as Sea2: source + destination * alpha; fading to the scene keeps destination * 1.
        vec3 packed = texture2D(s_seaDepth, gl_FragCoord.xy * u_viewTexel.xy).rgb;
        float behind = dot(packed, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) * u_seaFx[0].w;
        float edge = clamp((behind - v_fog.z) * u_seaFx[2].w, 0.0, 1.0);
        gl_FragColor.rgb *= edge;
        gl_FragColor.a = 1.0 - edge * (1.0 - gl_FragColor.a);
    }
}
