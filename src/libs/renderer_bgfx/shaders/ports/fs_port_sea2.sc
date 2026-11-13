$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea2_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx.
// Stage 0: animated bump volume texture, stage 1: the game's foam texture (weather\\sea\\pena\\pena.tga; read only by
// the waves from objects), stage 3: environment cube map.
//
// On top of the original (effects/sea.hpp sets u_seaFx; all off reproduces the D3D9 shader except the first point):
// - the reflected ray never points below the horizon: the cube's -Y face is never rendered above water, and the
//   original sampled its black there (dark specks near the horizon); a ray reflected downwards meets the sea again,
//   whose colour at the horizon the side faces hold;
// - band limit: the bump detail fades towards the flat surface as one pixel covers more texels of the bump
//   texture (far and grazing water), so the reflection does not shimmer at the horizon;
// - soft edges: the water fades out where it is shallower than the edge depth in front of the scene behind it
//   (s_seaDepth, the scene's view distance per pixel packed into RGB8 by fs_fx_sea_depth). Where that distance is
//   nearer than the water itself, the pixel belongs to an object in front of the sea (its edge: a rope, a sail, a
//   hull, partly covering a multisampled pixel) and the water is not faded: fading it there showed what lies under
//   the sea as bright fringes along every edge over the water;
// - specular: a sun highlight from the bumped normal over the game's own sun road (optional);
// - waves from objects (sea_waves.sh): ripples around swimmers, boats and hulls and the ships' Kelvin wakes tilt the
//   normal; the reflection follows it, and so does the Fresnel split between the mirrored sky (v0) and the water's
//   own colour and transparency (v1, alpha), recomputed per pixel with the vertex shader's formula for the tilted and
//   the untilted normal: where nothing stirs the water the original look stays exactly. Foam on the ripples' crests
//   and along the wakes' tracks is the game's foam texture, thresholded by the foam amount (optional).
#include "port_ps.sh"
#include "sea_waves.sh"
SAMPLER3D(s_vol0, 8);
SAMPLER2D(s_tex1, 1);
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
    float foam, rough;
    vec3 debug;
    // The foam texture in world metres (the sea's vertex origin moves with the camera; u_seaWaves[3] holds its uv).
    vec2 foam_uv = v_texcoord4.zw * u_seaWaves[3].z + u_seaWaves[3].xy;
    // Along the wakes, the divergent crests rise and fall with the texture's coarse streaks (about 0.5..1.5).
    float breakup = u_seaWaves[1].z > 0.0 ? 0.4 + 2.2 * dot(texture2D(s_tex1, foam_uv * 0.21).rgb, vec3(0.3, 0.5, 0.2))
                                         : 1.0;
    vec3 n0 = normalize(vec3(m1, m2, m3));
    vec3 N = seaWavesNormal(n0, v_texcoord4, length(fwidth(v_texcoord4.zw)), true, breakup, foam, rough, debug);
    vec3 R = portReflect(N, eye);
    // The waves' tilt changes how much sky the water mirrors: the vertex shader's Fresnel split, per pixel, for the
    // tilted normal over that for the sea's own.
    vec3 e = normalize(eye);
    float f0 = seaFresnel(dot(e, n0), u_seaWaves[2].z);
    float f1 = seaFresnel(dot(e, N), u_seaWaves[2].z);
    float water = (1.0 - f1) / max(1.0 - f0, 1e-3);
    v0.rgb *= f1 / max(f0, 1e-3);
    v0.a *= water;
    v1.rgb *= water;
    R.y = max(R.y, 0.0);
    vec4 t3 = textureCube(s_cube3, R);
    r0.rgb = PS11(v0.rgb * t3.rgb + v1.rgb);                                // mad r0.xyz, v0, t3, v1
    r0.a = v0.a;                                                            // mov r0.w, v0.wwww
    float highlight = pow(max(dot(normalize(R), u_seaFx[1].xyz), 0.0), u_seaFx[1].w);
    r0.rgb += u_seaFx[0].z * highlight * u_seaFx[2].rgb;
    // The far wake, finer than a pixel: a little more of the sky's light where its waves were.
    r0.rgb *= 1.0 + 0.25 * min(rough, 1.0);
    if (foam > 0.0)
    {
        // The foam texture's bright streaks (its luminance: 0.15 dark water .. 0.75 foam), more of them the more foam:
        // thin lace where a crest just breaks, a white sheet in the churned water behind a hull.
        vec3 lace = texture2D(s_tex1, foam_uv).rgb;
        float streak = clamp((dot(lace, vec3(0.3, 0.5, 0.2)) - 0.15) / 0.6, 0.0, 1.0);
        float cover = smoothstep(-0.1, 0.5, streak - (1.0 - 1.15 * foam)) * min(foam * 2.5, 0.95);
        // Lit like the water: the sky straight up, over the sea's own colour; the streaks' own shading on it.
        vec3 sky = textureCube(s_cube3, vec3(0.0, 1.0, 0.0)).rgb;
        vec3 white = min(vec3_splat(dot(sky, vec3(0.3, 0.5, 0.2)) * 1.1) + sky * 0.15 + v1.rgb, vec3_splat(1.0));
        white *= 0.8 + 0.25 * streak;
        r0.rgb = mix(r0.rgb, white, cover);
        r0.a *= 1.0 - cover;
    }

    if (u_seaWaves[2].x > 1.5)
        r0 = vec4((N.x - n0.x) * 2.0 + 0.5, 0.5, (N.z - n0.z) * 2.0 + 0.5, 0.0); // the tilt the waves add
    else if (u_seaWaves[2].x > 0.5)
        r0 = vec4(min(debug, vec3_splat(1.0)), 0.0);
    gl_FragColor = ffFinish(clamp(r0, 0.0, 1.0));
    if (u_seaFx[0].y > 0.5)
    {
        // Blended as Sea2: source + destination * alpha; fading to the scene keeps destination * 1.
        vec3 packed = texture2D(s_seaDepth, gl_FragCoord.xy * u_viewTexel.xy).rgb;
        float behind = dot(packed, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) * u_seaFx[0].w;
        float gap = behind - v_fog.z;
        float edge = gap < 0.0 ? 1.0 : clamp(gap * u_seaFx[2].w, 0.0, 1.0);
        gl_FragColor.rgb *= edge;
        gl_FragColor.a = 1.0 - edge * (1.0 - gl_FragColor.a);
    }
}
