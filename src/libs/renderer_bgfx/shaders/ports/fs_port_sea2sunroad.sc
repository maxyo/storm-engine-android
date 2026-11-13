$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea2sunroad_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx.
// Stage 0: animated bump volume texture, stage 3: sun road cube map.
#include "port_ps.sh"
#include "sea_waves.sh"
SAMPLER3D(s_vol0, 8);
SAMPLERCUBE(s_cube3, 11);

void main()
{
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture3D(s_vol0, v_texcoord0.xyz);                           // tex t0
    vec3 n = PS_BX2(t0.rgb);                                                //   t0_bx2
    float m1 = dot(v_texcoord1.xyz, n);                                     // texm3x3pad t1, t0_bx2
    float m2 = dot(v_texcoord2.xyz, n);                                     // texm3x3pad t2, t0_bx2
    float m3 = dot(v_texcoord3.xyz, n);                                     // texm3x3vspec t3, t0_bx2
    vec3 eye = vec3(v_texcoord1.w, v_texcoord2.w, v_texcoord3.w);           //   eye vector from the w components
    // The ripples glint in the sun road too (sea_waves.sh; the wakes only in the Sea2 pass).
    float foam, rough;
    vec3 debug;
    vec3 N = seaWavesNormal(vec3(m1, m2, m3), v_texcoord4, length(fwidth(v_texcoord4.zw)), false, foam, rough, debug);
    vec4 t3 = textureCube(s_cube3, portReflect(N, eye));
    r0 = t3;                                                                // mov r0, t3
    if (u_seaWaves[2].x > 0.5)
        r0 = vec4_splat(0.0);

    PORT_PS_OUTPUT(r0);
}
