$input v_color0
#include <bgfx_shader.sh>
void main() {
    if (v_color0.a <= 0.0) discard;
    gl_FragColor = v_color0;
}
