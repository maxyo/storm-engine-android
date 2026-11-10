$input v_uv
// Snapshot of the opaque scene for WaterQuality::High: colour copy plus the
// view distance packed into RGB8 (the depth attachment itself cannot be
// sampled while the sea is depth-tested against it).
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
SAMPLER2D(s_fxSceneDepth, 1);
uniform vec4 u_fxCapture; // x: near * far, y: far, z: far - near, w: 1 / far

void main()
{
    float d = texture2D(s_fxSceneDepth, v_uv).r;
    float z = u_fxCapture.x / (u_fxCapture.y - d * u_fxCapture.z);
    float packed = clamp(z * u_fxCapture.w, 0.0, 0.999999);
    vec3 enc = fract(packed * vec3(1.0, 255.0, 65025.0));
    enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
    gl_FragData[0] = vec4(texture2D(s_fxScene, v_uv).rgb, 1.0);
    gl_FragData[1] = vec4(enc, 1.0);
}
