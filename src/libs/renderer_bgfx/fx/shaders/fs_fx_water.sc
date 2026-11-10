$input v_wpos, v_wnormal, v_misc
// Sea surface shading. Variants:
//   FX_WATER_FOAM     crest foam (engine FoamV / FoamK / FoamUV)
//   FX_WATER_CLOUDS   the cloud deck appears in the reflection
//   FX_WATER_ENVCUBE  reflect the engine's environment cube instead of the analytic sky
//   FX_WATER_DEPTH    scene behind the surface: depth colour, refraction, shore foam, soft edges
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
// [2] rgb: deep water colour (linear), w: reflection strength
// [3] rgb: light scattered inside crests (linear), w: reflectance at normal incidence
// [4] x: detail normal strength, y: 1 / detail fade distance, z: sun road multiplier, w: wave height that counts as a crest
// [5] x: FoamV, y: FoamK, z: FoamUV, w: water depth in metres where shore foam ends
// [6] rgb: environment tint (Sea2.SkyColor, linear), w: far plane distance
// [7] x: refraction offset, y: absorption per metre, z: 1 = colour behind the surface is available, w: unused
// [8] rgb: average colour of the sky dome (linear): the light that falls on the water, w: unused
uniform vec4 u_fxWater[9];

#ifdef FX_WATER_DEPTH
float fxBehindDistance(vec2 uv)
{
    // 24-bit view depth / far, packed by fs_fx_water_capture.
    return dot(texture2D(s_fxBehindDepth, uv).rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) * u_fxWater[6].w;
}
#endif

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

    float ndv = max(dot(N, V), 0.0);
    float f0 = u_fxWater[3].w;
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);
    fresnel = clamp(fresnel * u_fxWater[2].w, 0.0, 1.0);

    // Reflection. Rays that would dip below the horizon are folded back up.
    vec3 R = reflect(-V, N);
    R.y = abs(R.y) + 0.02;
    R = normalize(R);
#ifdef FX_WATER_ENVCUBE
    vec3 reflection = fxDecode(textureCube(s_fxEnvCube, R).rgb);
#else
    vec3 reflection = fxSkyColor(R) + fxSunHalo(R) * 0.6;
#ifdef FX_WATER_CLOUDS
    reflection = fxClouds(R, reflection);
#endif
#endif
    reflection *= u_fxWater[6].rgb;

    // Water body: deep colour lit by the sky, plus sunlight scattered through wave crests towards the viewer.
    float sunUp = clamp(L.y * 4.0, 0.0, 1.0);
    float crest = clamp(v_misc.x / max(u_fxWater[4].w, 0.01), 0.0, 1.0);
    float forward = pow(max(dot(V, normalize(vec3(-L.x, 0.25, -L.z))), 0.0), 3.0);
    vec3 skyLight = u_fxWater[8].rgb;
    // The legacy sea is unlit: keep half of that so nights and storms stay as readable as the game made them.
    vec3 body = u_fxWater[2].rgb * (0.65 + 0.7 * fxLuma(skyLight) + 0.2 * sunUp * fxLuma(u_fxWater[1].rgb));
    body += u_fxWater[3].rgb * u_fxWater[1].rgb * (crest * (0.25 + 0.75 * forward) * (0.4 + 0.6 * (1.0 - ndv)) * sunUp);

    float alpha = 1.0;
    float shore = 0.0;
#ifdef FX_WATER_DEPTH
    vec2 screenUv = gl_FragCoord.xy * u_viewTexel.xy;
    float surface = v_misc.y;
    float thickness = max(fxBehindDistance(screenUv) - surface, 0.0);
    // Refract only into geometry that really is behind the surface.
    vec2 refractedUv = screenUv + N.xz * (u_fxWater[7].x * clamp(thickness * 0.5, 0.0, 1.0) / (1.0 + surface * 0.02));
    float refractedThickness = fxBehindDistance(refractedUv) - surface;
    if (refractedThickness > 0.0)
    {
        screenUv = refractedUv;
        thickness = refractedThickness;
    }
    // Vertical water depth under the pixel; the light path through the water is longer at grazing angles,
    // but capped so that shallows stay visible from a ship's deck.
    float depth = thickness * max(V.y, 0.02);
    float path = depth * min(1.0 / max(V.y, 0.02), 5.0);
    // Red is absorbed first: shallows turn turquoise before they turn deep blue.
    vec3 transmission = exp(-path * u_fxWater[7].y * vec3(2.6, 1.0, 0.75));
    vec3 behind = fxDecode(texture2D(s_fxBehindColor, screenUv).rgb);
    body = mix(body, mix(body * 1.6 + u_fxWater[3].rgb * 0.25, behind, u_fxWater[7].z), transmission);
    shore = clamp(1.0 - depth / u_fxWater[5].w, 0.0, 1.0);
    alpha = clamp(depth * 12.0, 0.0, 1.0);
#endif

    // Sun road: narrow highlight from the detail normals, widened by distance.
    vec3 H = normalize(L + V);
    float ndh = max(dot(N, H), 0.0);
    float specular = pow(ndh, u_fxWater[1].w) * u_fxWater[4].z * (0.25 + 0.75 * fresnel) * step(0.0, L.y);
    vec3 color = mix(body, reflection, fresnel) + u_fxWater[1].rgb * specular;

#if defined(FX_WATER_FOAM) || defined(FX_WATER_DEPTH)
    // Foam: engine crest rule (height above FoamV, scaled by FoamK) and, with depth, the shore line.
    float crestFoam = clamp((v_misc.x - u_fxWater[5].x) * u_fxWater[5].y, 0.0, 1.0);
    vec2 foamUv = uv * u_fxWater[5].z + slope * 0.4;
    float pattern = texture2D(s_fxDetail, foamUv + time * vec2(0.011, -0.007)).b;
    float foam = clamp(crestFoam * (0.35 + pattern) + shore * (0.45 + 1.3 * pattern) - 0.25 * (1.0 - pattern), 0.0, 1.0);
    vec3 foamColor = (skyLight * 0.9 + u_fxWater[1].rgb * (0.55 * max(dot(G, L), 0.0))) * (0.75 + 0.25 * pattern);
    color = mix(color, foamColor, foam);
#endif

    color = fxApplyFog(color, V, dist);
    gl_FragColor = vec4(fxEncode(color), alpha);
}
