$input a_position, a_normal, a_texcoord0, a_texcoord1
$output v_texcoord0, v_fog, v_color1, v_normal, v_position
// PBR vertex stage: the fixed-function emulator's transform, fog depth and clip plane (same uniforms, set by the
// shim's apply_ff_uniforms), plus the view-space position and normal for per-pixel shading.
// v_texcoord0 = (uv0, uv1).
#include "storm_common.sh"

uniform mat4 u_ffProj;
uniform vec4 u_ffVs[4];
uniform vec4 u_ffClipPlane;

void main()
{
    vec4 viewPos = mul(u_model[0], vec4(a_position.xyz, 1.0));
    gl_Position = mul(u_ffProj, viewPos);
    v_position = viewPos.xyz;
    v_normal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
    v_texcoord0 = vec4(a_texcoord0.xy, a_texcoord1.xy);
    float fogDepth = u_ffVs[1].z > 0.5 ? length(viewPos.xyz) : abs(viewPos.z);
    float clipDist = dot(u_ffClipPlane.xyz, viewPos.xyz) + u_ffClipPlane.w;
    if (dot(u_ffClipPlane, u_ffClipPlane) == 0.0) clipDist = 1.0;
    v_fog = vec4(fogDepth, clipDist, 0.0, 0.0);
    v_color1 = vec4_splat(0.0);
}
