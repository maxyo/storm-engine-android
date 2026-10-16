$input v_color0, v_texcoord0, v_texcoord1
#include <bgfx_shader.sh>
SAMPLER2D(s_sky0, 0);
SAMPLER2D(s_sky1, 1);
uniform vec4 u_sky; // factor alpha, material: single/transition/alpha-mask
void main() {
    vec4 first = texture2D(s_sky0, v_texcoord0);
    vec3 color = first.rgb * v_color0.rgb;
    float alpha = 1.0;
    if (u_sky.y > 0.5) {
        vec4 second = texture2D(s_sky1, v_texcoord1);
        // D3DTOP_BLENDFACTORALPHA: texture1 is not modulated by diffuse.
        color = mix(color, second.rgb, u_sky.x);
        if (u_sky.y > 1.5) alpha = mix(first.a, second.a, u_sky.x);
    }
    gl_FragColor = vec4(color, alpha);
}
