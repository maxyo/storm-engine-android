// Shared conventions for every storm shader (see ../README in renderer_bgfx).
//   v_color0/v_color1  diffuse/specular, already in RGBA order
//   v_texcoordN        stage N coordinates; projected stages divide by .w
//   v_fog.x            fog input: eye depth (FF vertex path) or a 0..1 factor (vertex-shader oFog,
//                      or the specular alpha when fixed-function fog has no mode)
//   v_fog.z            vertex-shader ports only: clip-space W (eye depth for the game's W-friendly
//                      projections, which D3D9 uses for table fog), used by table (pixel) fog
//   v_fog.y            user clip-plane distance (fragments with < 0 are discarded)
// Stage inputs and outputs (v_*, a_*, i_data*, gl_Position, gl_FragCoord ...) are named in main() only; a helper
// function gets what it needs as parameters, or is a macro expanded in main(). The SPIR-V build (Vulkan) compiles
// the shaders as HLSL, where they are main()'s parameters and do not exist anywhere else; a global that is written
// is not possible there either (HLSL globals are uniforms).
#ifndef STORM_COMMON_SH
#define STORM_COMMON_SH
#include <bgfx_shader.sh>
// D3DCOLOR vertex attributes arrive as B,G,R,A bytes.
#define D3DCOLOR_TO_RGBA(c) (c).zyxw
#endif
