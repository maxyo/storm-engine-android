$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea2_pixel_shader (ps_1_1) of src/techniques/weather/sea.fx.
// Stage 0: animated bump volume texture, stage 3: environment cube map.
#include "port_ps.sh"
SAMPLER3D(s_vol0, 8);
SAMPLERCUBE(s_cube3, 11);

void main()
{
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture3D(s_vol0, v_texcoord0.xyz);                           // tex t0
    vec3 n = PS_BX2(t0.rgb);                                                //   t0_bx2
    float m1 = dot(v_texcoord1.xyz, n);                                     // texm3x3pad t1, t0_bx2
    float m2 = dot(v_texcoord2.xyz, n);                                     // texm3x3pad t2, t0_bx2
    float m3 = dot(v_texcoord3.xyz, n);                                     // texm3x3vspec t3, t0_bx2
    vec3 eye = vec3(v_texcoord1.w, v_texcoord2.w, v_texcoord3.w);           //   eye vector from the w components
    vec4 t3 = textureCube(s_cube3, portReflect(vec3(m1, m2, m3), eye));
    r0.rgb = PS11(v0.rgb * t3.rgb + v1.rgb);                                // mad r0.xyz, v0, t3, v1
    r0.a = v0.a;                                                            // mov r0.w, v0.wwww

    PORT_PS_OUTPUT(r0);
}
