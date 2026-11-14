// Rain wetness of the outdoor surfaces (src/effects/lighting.hpp "wet surfaces", fx/wetness.hpp). Included by
// ff_fragment.sh in the FX_FF_LIT / FX_FF_BAKED programs. The physically based wet surface of Lagarde, "Water drop 3b -
// Physically based wet surfaces" (2013): water fills the pores of a rough (porous) surface, which darkens it, and a
// film of it covers the surface, which raises its gloss; a smooth surface (or metal) only takes the film. In the
// hollows of the relief (the material texture's height, R of stage 7) water gathers into puddles: flat, a mirror,
// over a darkened floor. The gloss that results drives the same highlights as a gloss map's (fxGlossSpec: the sun's,
// the lamps' with their shadows, the dynamic lights'; FX_LIGHT_SPECULAR) and the overcast sky's reflection (here).
//   main(): ffWetPrepare after the relief and the gloss of the pixel, ffWetNormal and the wet gloss before the lamps
//           take them, its w on the stages' colour, ffWetFinish on the lit colour.
//
// u_fxWet[0]: x the place's wetness 0..1 (0 = the draw stays dry), y 1 = puddles in the relief's hollows
// u_fxWet[1]: rgb the sky the wet surfaces reflect (the weather's fog colour, darker at night)
uniform vec4 u_fxWet[2];

// The gloss a surface without a gloss map is taken to have dry: stone, plaster, wood, earth (fx/gloss_map.hpp).
#define FF_WET_DRY_GLOSS 0.1
// A puddle's water: nearly a mirror; fxGlossSpec holds its lobe at what the game's meshes can show.
#define FF_WET_WATER_GLOSS 0.95
// The darkest a soaked porous surface gets (linear albedo factor; Lagarde: 0.2 for the most porous, milder here: the
// game's textures carry their own shading).
#define FF_WET_POROUS_DARK 0.35

// wet: the place's wetness at the pixel from the rain, its facing and its openness. Nw: world geometric normal; open:
// how much of the sky above 15° the pixel sees (ffVisibility(P, Ng).y: under a roof, an awning or an arcade the rain
// does not arrive). gloss: the dry gloss (< 0: no gloss map), metal 0..1; height: the relief's height at the pixel
// (1 the top, 0 the deepest; 1 where the draw has none).
// The wet surface of the pixel: x its wetness (0: dry, nothing of this file changes the pixel), y the puddle's water
// 0..1, z its gloss (Lagarde's, 0..1 as a gloss map's), w its diffuse colour's factor (gamma).
vec4 ffWetPrepare(vec3 Nw, float open, float gloss, float metal, float height)
{
    if (u_fxWet[0].x <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);
    // The rain falls on what faces up; walls take a little of it, what faces down none.
    float up = Nw.y;
    float wet = u_fxWet[0].x * (0.3 * smoothstep(-0.4, 0.0, up) + 0.7 * smoothstep(0.0, 0.8, up)) * smoothstep(0.15, 0.6, open);
    if (wet <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);
    // Puddles: on flat ground, the water rises in the relief's hollows over the second half of the wetting, at most to
    // 40 % of the relief's depth.
    float puddle = 0.0;
    if (u_fxWet[0].y > 0.5)
    {
        float level = 0.4 * clamp(wet * 2.0 - 1.0, 0.0, 1.0);
        puddle = smoothstep(0.9, 0.97, up) * clamp((level - height) * 20.0, 0.0, 1.0);
    }
    float dry = gloss >= 0.0 ? gloss : FF_WET_DRY_GLOSS;
    // Lagarde: the porosity from the dry gloss (matte = porous); its pores filled, the albedo darkens; the film's gloss.
    float porosity = clamp((0.5 - dry) / 0.4, 0.0, 1.0);
    float factor = mix(1.0, FF_WET_POROUS_DARK, (1.0 - metal) * porosity);
    float film = mix(1.0, dry, mix(1.0, factor, 0.5 * wet));
    float dark = mix(1.0, factor, wet);
    // A puddle covers its floor: water's gloss over the soaked colour. The picture is gamma-encoded (gamma 2, as
    // fxLightColor).
    return vec4(wet, puddle, mix(film, FF_WET_WATER_GLOSS, puddle), sqrt(mix(dark, factor, puddle)));
}

// The relief under the water: the film fills half of it, a puddle all of it. N: the shading normal, Ng: the
// geometric one (view space); wet: ffWetPrepare's.
vec3 ffWetNormal(vec3 N, vec3 Ng, vec4 wet)
{
    if (wet.x <= 0.0)
        return N;
    return normalize(mix(N, Ng, max(0.5 * wet.x, wet.y)));
}

// The highlight of the wet surface (the sun's, the lamps', the dynamic lights'; fxGlossSpec's power and strength): the
// water's lobe from the wet gloss, its strength the water's Fresnel (Schlick, F0 0.02) at the view (a highlight is seen
// where H is near N, so V.H is about N.V) times the normalised Blinn-Phong peak (power + 8) / 8, over the dry surface's
// by the wetness. At a grazing view the water mirrors several times what it does from above: the streak of a lamp on a
// wet street. dry: the dry surface's highlight; N, V: view space; wet: ffWetPrepare's.
vec2 ffWetSpec(vec2 dry, vec3 N, vec3 V, vec4 wet)
{
    float power = exp2(3.0 + 4.5 * wet.z);
    float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
    return mix(dry, vec2(power, fresnel * (power + 8.0) * 0.125), wet.x);
}

// The overcast sky the wet surface reflects (Schlick's Fresnel of water, F0 0.02, with Lagarde's roughness term: a
// rough film reflects less at grazing views), brighter overhead, added to the lit colour. N, V: view space; Rw_y: the
// reflected direction's world height; wet: ffWetPrepare's.
vec3 ffWetFinish(vec3 color, vec3 N, vec3 V, float Rw_y, vec4 wet)
{
    if (wet.x <= 0.0)
        return color;
    float nv = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + (max(wet.z, 0.02) - 0.02) * pow(1.0 - nv, 5.0);
    vec3 sky = fxLightColor(u_fxWet[1].rgb * (1.2 + 0.6 * clamp(Rw_y, 0.0, 1.0)));
    return fxAddSpecular(color, sky * (fresnel * wet.x));
}
