$input a_position, a_normal, a_color0, a_texcoord0
$output v_color0
#include <bgfx_shader.sh>
void main() {
    gl_Position = mul(u_modelViewProj, a_position);
    // Test the real stream's ARGB conversion, normal and UV offsets separately.
    v_color0 = vec4(a_color0.r, a_normal.x, a_texcoord0.x, 1.0);
}
