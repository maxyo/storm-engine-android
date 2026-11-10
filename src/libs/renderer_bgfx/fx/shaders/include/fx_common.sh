// Conventions shared by every fx shader.
//   * Render targets hold gamma-encoded LDR colour, exactly like the legacy
//     fixed-function pipeline they are mixed with. The package uses gamma 2.0
//     (square / square root): cheap and exactly invertible.
//   * World space is the engine's: y up, metres.
#ifndef FX_COMMON_SH
#define FX_COMMON_SH
#include <bgfx_shader.sh>

vec3 fxDecode(vec3 c)
{
    return c * c;
}

vec3 fxEncode(vec3 c)
{
    return sqrt(clamp(c, 0.0, 1.0));
}

float fxLuma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Screen-space noise in 0..1 without a texture (interleaved gradient noise), for dithering.
float fxNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}
#endif
