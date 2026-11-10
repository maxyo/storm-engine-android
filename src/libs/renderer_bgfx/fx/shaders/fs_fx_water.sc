$input v_wpos, v_wnormal, v_misc
// Sea surface shading: the legacy Sea2 model (sea_sea2_vertex_shader / pixel shader of
// src/techniques/weather/sea.fx) evaluated per pixel, with the same weather palette, plus detail normals,
// sun road and foam. Sea2, per vertex, in the game's gamma-encoded colours:
//   F = Fresnel(cos, Sea2.Frenel)        T = 2^-(Sea2.Attenuation / view.y)
//   colour = F * Reflection * N.y * SkyColor * environment + (1 - F) * (1 - T) * WaterColor
//   alpha  = (1 - F) * T * Transparency, blended as colour + behind * alpha
// T stands in for the light that comes back from below the surface (the sea bed, the hulls under water).
// Seen from below (SEA's underwater mode) the surface shows the scene above through Snell's window.
// Variants:
//   FX_WATER_FOAM     crest foam (engine FoamV / FoamK / FoamUV)
//   FX_WATER_CLOUDS   the cloud deck appears in the reflection of the analytic sky
//   FX_WATER_ENVCUBE  reflect the engine's environment cube (ships, islands, sky) instead of the analytic sky
//   FX_WATER_DEPTH    scene behind the surface: T from the real water depth, refraction, shore foam, soft edges
#include "fx_atmosphere.sh"

SAMPLER2D(s_fxDetail, 0);
#ifdef FX_WATER_ENVCUBE
SAMPLERCUBE(s_fxEnvCube, 1);
#endif
#ifdef FX_WATER_DEPTH
SAMPLER2D(s_fxBehindColor, 2);
SAMPLER2D(s_fxBehindDepth, 3);
#endif

// [0] xyz: camera position (world), w: time in seconds * BumpSpeed
// [1] rgb: sun light (linear), w: specular power
// [2] rgb: Sea2.WaterColor (gamma), w: Sea2.Reflection
// [3] rgb: Sea2.SkyColor (gamma), w: Sea2.Frenel
// [4] x: detail normal strength, y: 1 / detail fade distance, z: sun road multiplier, w: wave height that counts as a crest
// [5] x: FoamV, y: FoamK, z: FoamUV, w: water depth in metres where shore foam ends
// [6] x: Sea2.Attenuation, y: Sea2.Transparency, z: far plane distance, w: 1 = refraction on
// [7] x: refraction offset, y: 1 / depth in metres that Sea2.Attenuation stands for, zw: unused
// [8] rgb: average colour of the sky dome (linear): the light that falls on foam, w: unused
// [9] rgb: the game's fog colour (gamma), w: Sea2's fog density (0: no fog)
uniform vec4 u_fxWater[10];

// Sea2's fog, in gamma like the legacy sea: 2^-(view depth * density) towards the game's fog colour.
vec3 fxSea2Fog(vec3 encoded)
{
    return mix(u_fxWater[9].rgb, encoded, exp2(-v_misc.y * u_fxWater[9].w));
}

#ifdef FX_WATER_DEPTH
float fxBehindDistance(vec2 uv)
{
    // 24-bit view depth / far, packed by fs_fx_water_capture.
    return dot(texture2D(s_fxBehindDepth, uv).rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) * u_fxWater[6].z;
}
#endif

// Sea2's Fresnel term: unpolarised dielectric reflectance with Sea2.Frenel as the index ratio.
float fxSea2Fresnel(float c, float n)
{
    float g = sqrt(abs(n * n * c * c - n * n + 1.0));
    float rs = (c - n * g) / (c + n * g);
    float rp = (n * c - g) / (n * c + g);
    return min(0.5 * (rs * rs + rp * rp), 1.0);
}

void main()
{
    vec3 toEye = u_fxWater[0].xyz - v_wpos;
    float dist = length(toEye);
    vec3 V = toEye / dist;
    vec3 L = u_fxSky[0].xyz;
    float time = u_fxWater[0].w;

    // Two detail layers scrolling in different directions.
    vec2 uv = v_misc.zw;
    vec2 uvA = uv + time * vec2(0.031, 0.017);
    vec2 uvB = vec2(uv.x * 0.83 - uv.y * 0.56, uv.x * 0.56 + uv.y * 0.83) * 3.7 + time * vec2(-0.023, 0.041);
    vec4 dA = texture2D(s_fxDetail, uvA);
    vec4 dB = texture2D(s_fxDetail, uvB);
    float detailFade = 1.0 / (1.0 + dist * u_fxWater[4].y);
    vec2 slope = (dA.xy + dB.xy - vec2_splat(1.0)) * (u_fxWater[4].x * detailFade);
    vec3 G = normalize(v_wnormal);
    vec3 N = normalize(vec3(G.x + slope.x, G.y, G.z + slope.y));

    if (dot(V, G) < 0.0)
    {
        // Under the surface (SEA's underwater mode): the scene above shows through Snell's window (critical
        // angle of water, cos = 0.66), attenuated along the way up and scaled by Sea2.Transparency as from above;
        // outside the window the surface mirrors the water body (total internal reflection).
        float window = smoothstep(0.62, 0.72, max(dot(-N, V), 0.0));
        float up = window * exp2(-u_fxWater[6].x * dist * u_fxWater[7].y) * u_fxWater[6].y;
        vec3 water = fxSea2Fog(u_fxWater[2].rgb * (1.0 - up));
#ifdef FX_WATER_DEPTH
        vec2 aboveUv = gl_FragCoord.xy * u_viewTexel.xy - N.xz * (u_fxWater[7].x * u_fxWater[6].w);
        gl_FragColor = vec4(water + texture2D(s_fxBehindColor, aboveUv).rgb * up, 1.0);
#else
        gl_FragColor = vec4(water, up);
#endif
        return;
    }

    // Sea2 evaluates Fresnel and the light from below with the surface normal; the detail normals bend the
    // reflection and the sun road.
    float fresnel = fxSea2Fresnel(max(dot(G, V), 0.0), u_fxWater[3].w);

    // Reflection. Rays that would dip below the horizon are folded back up.
    vec3 R = reflect(-V, N);
    R.y = abs(R.y) + 0.02;
    R = normalize(R);
#ifdef FX_WATER_ENVCUBE
    vec3 reflection = textureCube(s_fxEnvCube, R).rgb;
#else
    vec3 sky = fxSkyColor(R) + fxSunHalo(R) * 0.6;
#ifdef FX_WATER_CLOUDS
    sky = fxClouds(R, sky);
#endif
    vec3 reflection = fxEncode(sky);
#endif

    // Light from below: Sea2 takes a layer of fixed depth seen at the view angle; with the scene behind the
    // surface the real depth of the water under the pixel is used instead.
    float viewY = max(V.y, 0.02);
    float transmission = exp2(-u_fxWater[6].x / viewY);
    float shore = 0.0;
    float edge = 1.0;
    vec3 behind = vec3_splat(0.0);
#ifdef FX_WATER_DEPTH
    vec2 screenUv = gl_FragCoord.xy * u_viewTexel.xy;
    float surface = v_misc.y;
    float thickness = max(fxBehindDistance(screenUv) - surface, 0.0);
    // Refract only into geometry that really is behind the surface.
    vec2 refractedUv = screenUv + N.xz * (u_fxWater[7].x * u_fxWater[6].w * clamp(thickness * 0.5, 0.0, 1.0) / (1.0 + surface * 0.02));
    float refractedThickness = fxBehindDistance(refractedUv) - surface;
    vec3 straight = texture2D(s_fxBehindColor, screenUv).rgb;
    if (refractedThickness > 0.0)
    {
        screenUv = refractedUv;
        thickness = refractedThickness;
    }
    float depth = thickness * viewY;
    transmission = exp2(-u_fxWater[6].x * depth * u_fxWater[7].y / viewY);
    behind = texture2D(s_fxBehindColor, screenUv).rgb;
    shore = clamp(1.0 - depth / u_fxWater[5].w, 0.0, 1.0);
    edge = clamp(depth * 12.0, 0.0, 1.0);
#endif

    // Sea2's palette, in the game's gamma-encoded colours as the legacy shader computes it.
    float reflected = fresnel * u_fxWater[2].w * max(G.y, 0.0);
    vec3 base = reflected * u_fxWater[3].rgb * reflection + (1.0 - fresnel) * (1.0 - transmission) * u_fxWater[2].rgb;
    float through = (1.0 - fresnel) * transmission * u_fxWater[6].y;

    // Sun road: narrow highlight from the detail normals, widened by distance.
    vec3 color = fxDecode(base);
    vec3 H = normalize(L + V);
    float ndh = max(dot(N, H), 0.0);
    float specular = pow(ndh, u_fxWater[1].w) * u_fxWater[4].z * (0.25 + 0.75 * fresnel) * step(0.0, L.y);
    color += u_fxWater[1].rgb * specular;

    vec3 skyLight = u_fxWater[8].rgb;
#if defined(FX_WATER_FOAM) || defined(FX_WATER_DEPTH)
    // Foam: engine crest rule (height above FoamV, scaled by FoamK) and, with depth, the shore line.
    float crestFoam = clamp((v_misc.x - u_fxWater[5].x) * u_fxWater[5].y, 0.0, 1.0);
    vec2 foamUv = uv * u_fxWater[5].z + slope * 0.4;
    float pattern = texture2D(s_fxDetail, foamUv + time * vec2(0.011, -0.007)).b;
    float foam = clamp(crestFoam * (0.35 + pattern) + shore * (0.45 + 1.3 * pattern) - 0.25 * (1.0 - pattern), 0.0, 1.0);
    vec3 foamColor = (skyLight * 0.9 + u_fxWater[1].rgb * (0.55 * max(dot(G, L), 0.0))) * (0.75 + 0.25 * pattern);
    color = mix(color, foamColor, foam);
    through *= 1.0 - foam;
#endif

    vec3 encoded = fxSea2Fog(fxEncode(color));
#ifdef FX_WATER_DEPTH
    // The surface composes the snapshot itself and fades out where it meets the ground.
    vec3 composed = encoded + behind * through;
    gl_FragColor = vec4(mix(straight, composed, edge), 1.0);
#else
    // Blended as Sea2: source + destination * alpha.
    gl_FragColor = vec4(encoded, through);
#endif
}
