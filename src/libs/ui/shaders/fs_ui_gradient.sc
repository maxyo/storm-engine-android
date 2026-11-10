$input v_color0, v_texcoord0
// linear/radial/conic gradients, plain and repeating (RmlUi decorators).
#include <bgfx_shader.sh>
#define MAX_NUM_STOPS 16
#define PI 3.14159265
uniform vec4 u_ui_gradient;                      // x: function (0..5, see backend_bgfx.cpp), y: number of stops
uniform vec4 u_ui_gradient_pv;                   // xy: p, zw: v
uniform vec4 u_ui_stop_colors[MAX_NUM_STOPS];
uniform vec4 u_ui_stop_positions[MAX_NUM_STOPS]; // x only

vec4 mix_stop_colors(float t, int num_stops)
{
    vec4 color = u_ui_stop_colors[0];
    for (int i = 1; i < MAX_NUM_STOPS; i++)
    {
        if (i >= num_stops)
            break;
        color = mix(color, u_ui_stop_colors[i], smoothstep(u_ui_stop_positions[i - 1].x, u_ui_stop_positions[i].x, t));
    }
    return color;
}

void main()
{
    int func = int(u_ui_gradient.x + 0.5);
    int num_stops = int(u_ui_gradient.y + 0.5);
    vec2 p = u_ui_gradient_pv.xy;
    vec2 v = u_ui_gradient_pv.zw;
    float t = 0.0;
    if (func == 0 || func == 3)
    {
        t = dot(v, v_texcoord0 - p) / dot(v, v);
    }
    else if (func == 1 || func == 4)
    {
        t = length(v * (v_texcoord0 - p));
    }
    else
    {
        vec2 d = v_texcoord0 - p;
        vec2 r = vec2(v.x * d.x + v.y * d.y, -v.y * d.x + v.x * d.y);
        t = 0.5 + atan2(-r.x, r.y) / (2.0 * PI);
    }
    if (func >= 3)
    {
        float t0 = u_ui_stop_positions[0].x;
        float t1 = u_ui_stop_positions[num_stops - 1].x;
        t = t0 + mod(t - t0, t1 - t0);
    }
    gl_FragColor = v_color0 * mix_stop_colors(t, num_stops);
}
