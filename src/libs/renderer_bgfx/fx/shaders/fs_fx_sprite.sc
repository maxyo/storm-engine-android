$input v_wpos, v_misc
// Shapes: 0 glare (soft disc with a long tail), 1 flare ghost (disc with a brighter rim), 2 streak
// (horizontal line), 3 contact shadow blob (multiplies the target: output is 1 - darkness).
#include "fx_common.sh"

void main()
{
    vec2 p = v_misc.xy;
    float r = length(p);
    float shape = v_misc.z;
    float a;
    if (shape < 0.5)
    {
        float core = clamp(1.0 - r, 0.0, 1.0);
        a = core * core * core * 0.6 + 0.4 * core * core * core * core * core * core * core * core;
    }
    else if (shape < 1.5)
    {
        a = smoothstep(1.0, 0.8, r) * (0.35 + 0.65 * smoothstep(0.45, 0.95, r));
    }
    else if (shape < 2.5)
    {
        float along = clamp(1.0 - abs(p.x), 0.0, 1.0);
        a = along * along * exp(-p.y * p.y * 40.0);
    }
    else
    {
        float inside = clamp(1.0 - r, 0.0, 1.0);
        gl_FragColor = vec4(vec3_splat(1.0) - v_wpos * (inside * inside * (3.0 - 2.0 * inside)), 1.0);
        return;
    }
    gl_FragColor = vec4(v_wpos * a, 1.0);
}
