// Aerial perspective for the fixed-function emulator (FX_AERIAL), replacing the plain distance fog:
// distant geometry loses saturation and takes the colour of the horizon, and a layer of haze lies over the
// sea and in valleys. Density and colour come from the game's fog (fx::Sky::BindAerial).
//     color.rgb = fxAerial(color.rgb, P);      // P: view-space position; colours in the pipeline's gamma space
#ifndef FX_AERIAL_SH
#define FX_AERIAL_SH

// [0] rgb: haze colour (gamma space), w: fog density per metre (the game's)
// [1] x: extra density of the height haze at the camera's height, y: 1 / haze layer thickness, z: desaturation, w: maximum opacity
// [2] xyz: world up in view space
uniform vec4 u_fxAerial[3];

vec3 fxAerial(vec3 color, vec3 P)
{
    float dist = length(P);
    // Height haze thins out exponentially with height: integrate it along the ray from the eye.
    float rise = dot(P, u_fxAerial[2].xyz) * u_fxAerial[1].y;
    float layer = abs(rise) > 1e-3 ? (1.0 - exp(-rise)) / rise : 1.0;
    float haze = 1.0 - exp(-dist * (u_fxAerial[0].w + u_fxAerial[1].x * layer));
    haze = min(haze, u_fxAerial[1].w);
    float grey = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(color, vec3_splat(grey), u_fxAerial[1].z * haze);
    return mix(color, u_fxAerial[0].rgb, haze);
}

// The fixed-function emulator's form (fog mode 5 of the renderer's ff_finish.sh): the draw's own exp fog (colour,
// density per metre, fog distance), so the game's fog overrides (islands, grass) stay and lit, baked and unlit draws
// fade alike. The haze layer only redistributes it with height: a ray at the eye's height keeps exactly the game's
// fog, rays climbing out of the layer (mountains, masts) get less, rays going down a little more; distant colours
// lose some saturation. `rise`: height of the point above the eye. `haze` (ComputeAerialFogHaze): x: haze density
// relative to the fog density at the eye's height, y: 1 / haze layer thickness. No opacity cap: the game's fog hides
// what lies beyond it.
#define FX_AERIAL_FOG_DESATURATION 0.3 // fx::kAerialFogDesaturation
vec3 fxAerialFog(vec3 color, vec3 fogColor, float density, float dist, float rise, vec2 haze)
{
    float r = rise * haze.y;
    float layer = abs(r) > 1e-3 ? (1.0 - exp(-r)) / r : 1.0;
    float amount = 1.0 - exp(-dist * density * (1.0 + haze.x * layer) / (1.0 + haze.x));
    float grey = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(color, vec3_splat(grey), FX_AERIAL_FOG_DESATURATION * amount);
    return mix(color, fogColor, amount);
}
#endif
