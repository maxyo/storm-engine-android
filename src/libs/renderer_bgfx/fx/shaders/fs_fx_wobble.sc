$input v_uv
// The game's underwater screen wobble (Render.SeaEffect, the D3D9 engine's post-process grid): the picture is
// sampled through sine waves along x and y, 8 periods across the screen, squeezed so no edge is sampled outside.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
// [0] x: phase (radians), y: 1 when uv has its origin at the bottom (the waves run from the top of the picture)
// [1] xy: S / (S + 2 d) per axis, zw: d / (S + 2 d) per axis (S picture size, d wave amplitude, pixels)
uniform vec4 u_fxWobble[2];

void main()
{
    vec2 uv = v_uv;
    if (u_fxWobble[0].y > 0.5)
        uv.y = 1.0 - uv.y;
    vec2 waves = sin(vec2_splat(u_fxWobble[0].x) + uv * (16.0 * 3.14159265));
    vec2 sampled = uv * u_fxWobble[1].xy + u_fxWobble[1].zw * (vec2_splat(1.0) + waves);
    if (u_fxWobble[0].y > 0.5)
        sampled.y = 1.0 - sampled.y;
    gl_FragColor = vec4(texture2D(s_fxScene, sampled).rgb, 1.0);
}
