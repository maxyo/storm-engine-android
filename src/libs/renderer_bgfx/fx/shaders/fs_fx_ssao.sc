$input v_uv
// Ambient occlusion from the depth buffer at half resolution: eight taps on a
// spiral, normals reconstructed from depth. Output: r = occlusion (1 = open),
// g, b = view distance / far in 16 bits for the depth-aware upsample in
// fs_fx_composite.sc.
#include "fx_common.sh"

SAMPLER2D(s_fxSceneDepth, 4);
// [0] x: near * far, y: far, z: far - near, w: radius in metres
// [1] xy: tan(half fov) per axis (1 / projection scale), z: intensity, w: 1 / far
// [2] xy: one depth texel in uv
uniform vec4 u_fxAo[3];

// View-space position (z = distance along the view axis) of the depth texel that contains uv. Sampling
// exactly at texel centres matters: this pass runs at half resolution, where pixel centres fall on the
// corners of depth texels and a point sample would pick neighbours at random.
vec3 viewAt(vec2 uv)
{
    // + 0.25: a half-resolution pixel centre is exactly on a texel corner; pick one of the four for certain.
    vec2 centre = (floor(uv / u_fxAo[2].xy + vec2_splat(0.25)) + vec2_splat(0.5)) * u_fxAo[2].xy;
    float z = u_fxAo[0].x / (u_fxAo[0].y - texture2DLod(s_fxSceneDepth, centre, 0.0).r * u_fxAo[0].z);
    return vec3((centre * 2.0 - vec2_splat(1.0)) * u_fxAo[1].xy * z, z);
}

void main()
{
    vec3 P = viewAt(v_uv);
    float z = P.z;
    float packed = clamp(z * u_fxAo[1].w, 0.0, 0.9999);
    vec2 enc = fract(packed * vec2(1.0, 255.0));
    enc.x -= enc.y * (1.0 / 255.0);
    if (packed > 0.98)
    {
        gl_FragColor = vec4(1.0, enc, 1.0); // sky
        return;
    }
    // Normal from the nearer of the two neighbours on each axis, so it does not bend across depth edges.
    vec2 t = u_fxAo[2].xy;
    vec3 right = viewAt(v_uv + vec2(t.x, 0.0)) - P;
    vec3 left = P - viewAt(v_uv - vec2(t.x, 0.0));
    vec3 up = viewAt(v_uv + vec2(0.0, t.y)) - P;
    vec3 down = P - viewAt(v_uv - vec2(0.0, t.y));
    vec3 dx = abs(right.z) < abs(left.z) ? right : left;
    vec3 dy = abs(up.z) < abs(down.z) ? up : down;
    vec3 N = normalize(cross(dy, dx));
    if (N.z > 0.0)
        N = -N; // towards the camera (z grows into the screen)

    // Radius in uv, limited so distant and very close geometry both get a sensible footprint.
    float radius = u_fxAo[0].w;
    vec2 radiusUv = clamp(radius / (z * u_fxAo[1].xy) * 0.5, t * 2.0, vec2_splat(0.08));
    // The spiral is rotated in a 2x2 pattern; the upsample averages exactly such a block, which removes the
    // pattern and leaves 32 effective directions.
    vec2 cell = floor(mod(gl_FragCoord.xy, 2.0));
    float angle = (cell.x * 2.0 + cell.y) * 0.19635;
    float occlusion = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        float fi = float(i);
        float a = angle + fi * 0.7854;
        vec2 uv = v_uv + vec2(cos(a), sin(a)) * radiusUv * ((fi + 1.0) / 8.0);
        // Taps outside the screen know nothing: they count as open.
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            continue;
        vec3 v = viewAt(uv) - P;
        float d2 = dot(v, v);
        // Occluders above the tangent plane, weighted down with distance beyond the radius.
        occlusion += max(dot(N, v) * inversesqrt(d2 + 1e-6) - 0.08, 0.0) / (1.0 + d2 / (radius * radius));
    }
    float ao = clamp(1.0 - occlusion * (u_fxAo[1].z / 8.0) * 4.5, 0.0, 1.0);
    // Far away a half-resolution texel covers metres of alpha-tested foliage: the depth is noise there and so would
    // the occlusion be (a coarse mosaic over distant hills). It fades out between 30 and 90 m.
    ao = mix(ao, 1.0, smoothstep(30.0, 90.0, z));
    gl_FragColor = vec4(ao, enc, 1.0);
}
