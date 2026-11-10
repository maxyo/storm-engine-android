$input v_color0, v_texcoord0 // same varyings as vs_ui_quad (bgfx matches the hashes)
// One separable 7-tap Gaussian pass, limited to the filter region.
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
uniform vec4 u_ui_blur_weights; // weights for offsets 0..3
uniform vec4 u_ui_texel_offset; // xy
uniform vec4 u_ui_region;       // xy: min texcoord, zw: max texcoord
void main()
{
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    for (int i = 0; i < 7; i++)
    {
        vec2 uv = v_texcoord0 - float(i - 3) * u_ui_texel_offset.xy;
        vec2 in_region = step(u_ui_region.xy, uv) * step(uv, u_ui_region.zw);
        int k = i < 3 ? 3 - i : i - 3;
        float weight = k == 0 ? u_ui_blur_weights.x : (k == 1 ? u_ui_blur_weights.y : (k == 2 ? u_ui_blur_weights.z : u_ui_blur_weights.w));
        color += texture2D(s_ui_tex, uv) * in_region.x * in_region.y * weight;
    }
    gl_FragColor = color;
}
