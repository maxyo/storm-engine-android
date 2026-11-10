vec4 v_color0    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec4 v_color1    : COLOR1    = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_texcoord0 : TEXCOORD0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_texcoord1 : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_texcoord2 : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_texcoord3 : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_texcoord4 : TEXCOORD4 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_fog       : TEXCOORD7 = vec4(0.0, 0.0, 0.0, 0.0);
// Per-pixel lighting variants only (vs_ff_lit / fs_ff_lit*, fs_ff_baked*): view-space normal and position,
// material diffuse and ambient colour of the vertex.
vec4 v_normal     : NORMAL    = vec4(0.0, 0.0, -1.0, 0.0);
vec4 v_position   : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_matDiffuse : COLOR2    = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_matAmbient : COLOR3    = vec4(0.0, 0.0, 0.0, 0.0);

vec4 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_color0    : COLOR0;
vec4 a_color1    : COLOR1;
vec4 a_texcoord0 : TEXCOORD0;
vec4 a_texcoord1 : TEXCOORD1;
vec4 a_texcoord2 : TEXCOORD2;
vec4 a_texcoord3 : TEXCOORD3;
vec4 a_texcoord4 : TEXCOORD4;
vec4 a_texcoord5 : TEXCOORD5;
vec4 a_texcoord6 : TEXCOORD6;
vec4 a_texcoord7 : TEXCOORD7;
vec4 a_weight    : BLENDWEIGHT;
vec4 a_indices   : BLENDINDICES;
vec3 a_tangent   : TANGENT;
vec3 a_bitangent : BITANGENT;
