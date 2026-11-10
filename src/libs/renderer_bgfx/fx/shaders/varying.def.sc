vec2 v_uv      : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_wpos    : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
vec3 v_wnormal : TEXCOORD2 = vec3(0.0, 1.0, 0.0);
vec4 v_misc    : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_dir     : TEXCOORD4 = vec3(0.0, 0.0, 1.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec3 a_texcoord0 : TEXCOORD0;
