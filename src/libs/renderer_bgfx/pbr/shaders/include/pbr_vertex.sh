// PBR vertex stage: the fixed-function emulator's transform, fog depth and clip plane (same uniforms, set by the
// shim's apply_ff_uniforms), plus the view-space position and normal for per-pixel shading. v_texcoord0 = (uv0, uv1).
// PBR_SKIN: GEOS::AVERTEX0 vertices skinned with the bone palette first (ff_skin.sh, as vs_ff_skin).
#include "storm_common.sh"
#ifdef PBR_SKIN
#include "ff_skin.sh"
#endif

uniform mat4 u_ffProj;
uniform vec4 u_ffVs[4];
uniform vec4 u_ffClipPlane;

void main()
{
#ifdef PBR_SKIN
    vec4 r0 = blendedRow(0, a_indices.x, a_indices.y, a_weight.x);
    vec4 r1 = blendedRow(1, a_indices.x, a_indices.y, a_weight.x);
    vec4 r2 = blendedRow(2, a_indices.x, a_indices.y, a_weight.x);
    vec3 modelPos = skinPoint(r0, r1, r2, a_position.xyz);
    vec3 modelNormal = vec3(dot(r0.xyz, a_normal), dot(r1.xyz, a_normal), dot(r2.xyz, a_normal)); // a direction
#else
    vec3 modelPos = a_position.xyz;
    vec3 modelNormal = a_normal;
#endif
    vec4 viewPos = mul(u_model[0], vec4(modelPos, 1.0));
    gl_Position = mul(u_ffProj, viewPos);
    v_position = viewPos.xyz;
    v_normal = mul(u_model[0], vec4(modelNormal, 0.0)).xyz;
    v_texcoord0 = vec4(a_texcoord0.xy, a_texcoord1.xy);
    float fogDepth = u_ffVs[1].z > 0.5 ? length(viewPos.xyz) : abs(viewPos.z);
    float clipDist = dot(u_ffClipPlane.xyz, viewPos.xyz) + u_ffClipPlane.w;
    if (dot(u_ffClipPlane, u_ffClipPlane) == 0.0) clipDist = 1.0;
    v_fog = vec4(fogDepth, clipDist, 0.0, 0.0);
    v_color1 = vec4_splat(0.0);
}
