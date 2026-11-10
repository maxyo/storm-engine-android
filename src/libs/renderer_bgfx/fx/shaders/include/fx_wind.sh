// Wind sway for vertex shaders (FX_WIND): foliage, grass, sails, flags.
//     worldPos += fxWindOffset(worldPos, weight);
// `weight` is how free the vertex is to move, 0 at the anchored part (trunk, ground, yard) to 1 at the tip;
// the emulator takes it from what the mesh offers (grass: height in the blade, foliage and sails: a vertex
// colour channel or the height above the object's origin).
#ifndef FX_WIND_SH
#define FX_WIND_SH

// [0] xyz: wind direction (unit, world space), w: time in seconds
// [1] x: sway amplitude in metres at weight 1, y: sway frequency (rad/s), z: gust wave number (rad/m), w: flutter amplitude
uniform vec4 u_fxWind[2];

vec3 fxWindOffset(vec3 worldPos, float weight)
{
    // A slow gust front travelling with the wind bends everything; a faster, smaller flutter keeps leaves alive.
    float along = dot(worldPos, u_fxWind[0].xyz);
    float gust = sin(u_fxWind[0].w * u_fxWind[1].y - along * u_fxWind[1].z);
    float flutter = sin(u_fxWind[0].w * u_fxWind[1].y * 3.7 + dot(worldPos, vec3(1.7, 2.3, 1.1)));
    vec3 side = vec3(-u_fxWind[0].z, 0.0, u_fxWind[0].x);
    float bend = (0.6 + 0.4 * gust) * u_fxWind[1].x;
    return (u_fxWind[0].xyz * bend + side * (flutter * u_fxWind[1].w)) * (weight * weight);
}
#endif
