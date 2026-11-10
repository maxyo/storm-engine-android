// Alpha to coverage for alpha-tested geometry (grass, foliage, rigging) on a multisampled target.
// Instead of `if (alpha < ref) discard;` write
//     gl_FragColor.a = fxCoverageAlpha(alpha, ref, uv, textureSize);
// and draw with BGFX_STATE_BLEND_ALPHA_TO_COVERAGE (no blending, depth writes stay on).
// Close up, the alpha edge is sharpened to about one pixel, so the coverage mask turns it into a smooth,
// correctly sorted edge. In the distance, where the mip chain has averaged thin shapes into partial alpha,
// that alpha becomes the coverage of whatever the test would have dropped: ropes and blades thin out
// instead of vanishing, while shapes that still pass the test stay solid.
#ifndef FX_COVERAGE_SH
#define FX_COVERAGE_SH

// uv: the texture coordinates alpha was sampled with; textureSize: size of the texture's top mip in texels.
float fxCoverageAlpha(float alpha, float ref, vec2 uv, vec2 textureSize)
{
    float edge = clamp((alpha - ref) / max(fwidth(alpha), 1e-4) + 0.5, 0.0, 1.0);
    vec2 dx = dFdx(uv * textureSize);
    vec2 dy = dFdy(uv * textureSize);
    // Texels per pixel, squared: above 1 the texture is minified.
    float minified = clamp(0.5 * log2(max(dot(dx, dx), dot(dy, dy))), 0.0, 1.0);
    return max(edge, alpha * minified);
}
#endif
