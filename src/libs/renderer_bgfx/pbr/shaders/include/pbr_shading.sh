// Forward PBR shading of Caribbean Legend materials (one fragment body, variants by define):
//   PBR_NORMAL_MAP  tangent-space normal map on unit 3
//   PBR_EMISSION    emission texture on unit 4
//   PBR_LAYER       three layers (albedo, rma, normal) splatted over the base by an RGB mask (unit 7)
//
// Algorithms (published, own implementation):
//   * Cook-Torrance: GGX D, Smith-Schlick visibility, Schlick Fresnel with the spherical-Gaussian exponent,
//     Lambert diffuse; split-sum image-based light with a GGX-prefiltered cube (mip = perceptual roughness *
//     max mip) and the environment BRDF table (Karis 2013, "Real Shading in Unreal Engine 4").
//   * Diffuse image-based light: SH9 irradiance (Ramamoorthi & Hanrahan 2001), coefficients already / pi.
//   * Specular occlusion from AO (Lagarde & de Rousiers 2014, "Moving Frostbite to PBR").
//   * Tangent frame from screen derivatives of position and UV (no vertex tangents in .gm), solved for dP/du,
//     dP/dv as in Schüler 2006 / Mikkelsen 2010, independent of screen orientation and handedness.
//   * Tone map: the game's own camera grade (Resource/INI/aces.json: exposure, contrast, saturation, lift / gamma /
//     gain) and the ACES fit; the result is display-referred and gamma-encoded like every legacy draw (fx LDR
//     convention).
// Light units: a light colour of 1 makes a white Lambert surface facing it show its albedo (pi folded in).
#include "storm_common.sh"
#include "ff_finish.sh"

#define PBR_PI 3.14159265
// Normal maps: +Y points up in the image (OpenGL convention, CL's Blender pipeline); 0 for DirectX maps.
#define PBR_NORMAL_Y_UP 1

uniform vec4 u_pbrSh[9];            // irradiance / pi, world space
uniform vec4 u_pbrSun;              // xyz: view space, towards the sun; w: 1 on
uniform vec4 u_pbrSunColor;         // rgb: colour x intensity
uniform vec4 u_pbrAmbient;          // rgb: ambient colour x intensity (scales both image-based terms); w: max mip of the radiance cube
uniform vec4 u_pbrFrame;            // x: exposure  y: emission of the hour  z: image-based light on  w: point light count
uniform vec4 u_pbrViewToWorld[3];   // rows of the view -> world rotation
uniform vec4 u_pbrMaterial;         // x: roughness override (< 0: texture)  y: metallic override  z: emission strength  w: AO map on uv1
uniform vec4 u_pbrMaterial2;        // x: normal strength  y: alpha factor (texture factor of blended techniques)
uniform vec4 u_pbrTiling;           // base, r, g, b (layer material; base also scales the plain material's uv: 1)
uniform vec4 u_pbrPointPos[4];      // view space xyz, w: range
uniform vec4 u_pbrPointColor[4];    // rgb
uniform vec4 u_pbrPointAtt[4];      // D3D attenuation a0, a1, a2
uniform vec4 u_pbrGrade;            // rgb: colour filter x 2^exposure_ev; w: contrast
uniform vec4 u_pbrGrade2;           // x: saturation
uniform vec4 u_pbrLift;
uniform vec4 u_pbrGamma;
uniform vec4 u_pbrGain;

SAMPLER2D(s_tex0, 0);       // albedo (.gm stage 0, uv0)
SAMPLER2D(s_tex1, 1);       // ambient occlusion (.gm stage 1, uv1)
SAMPLER2D(s_pbrRma, 2);
SAMPLER2D(s_pbrNormal, 3);
SAMPLERCUBE(s_pbrRadiance, 5);
SAMPLER2D(s_pbrBrdf, 6);
#ifdef PBR_EMISSION
SAMPLER2D(s_pbrEmission, 4);
#endif
#ifdef PBR_LAYER
SAMPLER2D(s_pbrLayerAlbedoR, 4);
SAMPLER2D(s_pbrMask, 7);
SAMPLER2D(s_pbrLayerAlbedoG, 8);
SAMPLER2D(s_pbrLayerAlbedoB, 9);
SAMPLER2D(s_pbrLayerRmaR, 10);
SAMPLER2D(s_pbrLayerRmaG, 11);
SAMPLER2D(s_pbrLayerRmaB, 12);
SAMPLER2D(s_pbrLayerNormalR, 13);
SAMPLER2D(s_pbrLayerNormalG, 14);
SAMPLER2D(s_pbrLayerNormalB, 15);
#endif

vec3 pbrToLinear(vec3 c) { return pow(max(c, vec3_splat(0.0)), vec3_splat(2.2)); }

// Caribbean Legend camera grade (Resource/INI/aces.json) and ACES tone map, scene-referred -> display-referred:
// exposure x colour filter, contrast in log space around mid grey, saturation around Rec. 709 luma, then the ACES
// RRT + ODT fit (Stephen Hill, BakingLab, MIT), then lift / gamma / gain on the display value.
vec3 pbrToneMap(vec3 color)
{
    color *= u_pbrGrade.rgb;
    color = 0.18 * pow(max(color, vec3_splat(1e-6)) / 0.18, vec3_splat(u_pbrGrade.w));
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(vec3_splat(luma) + (color - vec3_splat(luma)) * u_pbrGrade2.x, vec3_splat(0.0));
    vec3 v = vec3(dot(vec3(0.59719, 0.35458, 0.04823), color),
                  dot(vec3(0.07600, 0.90834, 0.01566), color),
                  dot(vec3(0.02840, 0.13383, 0.83777), color));
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081);
    color = vec3(dot(vec3(1.60475, -0.53108, -0.07367), v),
                 dot(vec3(-0.10208, 1.10813, -0.00605), v),
                 dot(vec3(-0.00327, -0.07276, 1.07602), v));
    color = clamp(color, 0.0, 1.0);
    color = u_pbrGain.rgb * (color + u_pbrLift.rgb * (1.0 - color));
    return pow(max(color, vec3_splat(0.0)), 1.0 / max(u_pbrGamma.rgb, vec3_splat(1e-3)));
}

vec3 pbrIrradiance(vec3 n)
{
    vec3 r = u_pbrSh[0].rgb * 0.282095;
    r += u_pbrSh[1].rgb * (0.488603 * n.y);
    r += u_pbrSh[2].rgb * (0.488603 * n.z);
    r += u_pbrSh[3].rgb * (0.488603 * n.x);
    r += u_pbrSh[4].rgb * (1.092548 * n.x * n.y);
    r += u_pbrSh[5].rgb * (1.092548 * n.y * n.z);
    r += u_pbrSh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    r += u_pbrSh[7].rgb * (1.092548 * n.x * n.z);
    r += u_pbrSh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(r, vec3_splat(0.0));
}

vec3 pbrToWorld(vec3 v)
{
    return vec3(dot(u_pbrViewToWorld[0].xyz, v), dot(u_pbrViewToWorld[1].xyz, v), dot(u_pbrViewToWorld[2].xyz, v));
}

// Direct light of colour `light` from direction l (normalised), Karis 2013.
vec3 pbrDirect(vec3 n, vec3 v, vec3 l, vec3 light, vec3 diffuse, vec3 f0, float roughness)
{
    float ndl = clamp(dot(n, l), 0.0, 1.0);
    if (ndl <= 0.0) return vec3_splat(0.0);
    vec3 h = normalize(v + l);
    float ndv = max(dot(n, v), 1e-4);
    float ndh = clamp(dot(n, h), 0.0, 1.0);
    float vdh = clamp(dot(v, h), 0.0, 1.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float dd = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (PBR_PI * dd * dd);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float vis = 0.25 / ((ndl * (1.0 - k) + k) * (ndv * (1.0 - k) + k));
    vec3 F = f0 + (1.0 - f0) * exp2((-5.55473 * vdh - 6.98316) * vdh);
    // pi folded into the light colour: diffuse = albedo * ndl * light
    return (diffuse * (1.0 - F) + PBR_PI * D * vis * F) * light * ndl;
}

#ifdef PBR_NORMAL_MAP
// Tangent frame from screen derivatives: T = dP/du, B = dP/dv, orthogonalised against n.
vec3 pbrPerturb(vec3 n, vec3 p, vec2 uv, vec3 ts)
{
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    vec3 t = (dp1 * duv2.y - dp2 * duv1.y) * sign(det);
    vec3 b = (dp2 * duv1.x - dp1 * duv2.x) * sign(det);
    t -= n * dot(n, t);
    b -= n * dot(n, b);
    float lt = dot(t, t), lb = dot(b, b);
    if (lt < 1e-20 || lb < 1e-20) return n; // degenerate uv mapping: the map cannot be oriented here
    t *= inversesqrt(lt);
    b *= inversesqrt(lb);
#if PBR_NORMAL_Y_UP
    b = -b; // image up = -v
#endif
    return normalize(t * ts.x + b * ts.y + n * ts.z);
}

vec3 pbrDecodeNormal(vec4 texel) { return texel.xyz * 2.0 - 1.0; }
#endif

void main()
{
    vec2 uv0 = v_texcoord0.xy;
    vec2 uv1 = v_texcoord0.zw;
    vec4 base = texture2D(s_tex0, uv0 * u_pbrTiling.x);
    float alpha = base.a * u_pbrMaterial2.y;
    vec3 albedo = pbrToLinear(base.rgb);
    vec3 rma = texture2D(s_pbrRma, uv0 * u_pbrTiling.x).rgb;
#ifdef PBR_NORMAL_MAP
    vec3 ts = pbrDecodeNormal(texture2D(s_pbrNormal, uv0 * u_pbrTiling.x));
#endif
#ifdef PBR_LAYER
    vec3 w = texture2D(s_pbrMask, uv0).rgb;
    float wBase = max(0.0, 1.0 - (w.r + w.g + w.b));
    float total = wBase + w.r + w.g + w.b;
    w /= total;
    wBase /= total;
    vec2 uvR = uv0 * u_pbrTiling.y, uvG = uv0 * u_pbrTiling.z, uvB = uv0 * u_pbrTiling.w;
    albedo = albedo * wBase
           + pbrToLinear(texture2D(s_pbrLayerAlbedoR, uvR).rgb) * w.r
           + pbrToLinear(texture2D(s_pbrLayerAlbedoG, uvG).rgb) * w.g
           + pbrToLinear(texture2D(s_pbrLayerAlbedoB, uvB).rgb) * w.b;
    rma = rma * wBase + texture2D(s_pbrLayerRmaR, uvR).rgb * w.r + texture2D(s_pbrLayerRmaG, uvG).rgb * w.g
        + texture2D(s_pbrLayerRmaB, uvB).rgb * w.b;
    vec2 xy = ts.xy * wBase + pbrDecodeNormal(texture2D(s_pbrLayerNormalR, uvR)).xy * w.r
            + pbrDecodeNormal(texture2D(s_pbrLayerNormalG, uvG)).xy * w.g
            + pbrDecodeNormal(texture2D(s_pbrLayerNormalB, uvB)).xy * w.b;
    xy *= u_pbrMaterial2.x;
    ts = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
#endif
    float roughness = u_pbrMaterial.x >= 0.0 ? u_pbrMaterial.x : rma.r;
    float metallic = u_pbrMaterial.y >= 0.0 ? u_pbrMaterial.y : rma.g;
    roughness = clamp(roughness, 0.03, 1.0);
    float ao = rma.b;
    if (u_pbrMaterial.w > 0.5) ao *= texture2D(s_tex1, uv1).r;

    vec3 n = normalize(v_normal);
    vec3 v = normalize(-v_position);
    if (dot(n, v) < 0.0) n = -n; // two-sided geometry seen from the back
#ifdef PBR_NORMAL_MAP
    n = pbrPerturb(n, v_position, uv0 * u_pbrTiling.x, ts);
#endif
    vec3 diffuse = albedo * (1.0 - metallic);
    vec3 f0 = mix(vec3_splat(0.04), albedo, metallic);

    vec3 color = vec3_splat(0.0);
    if (u_pbrSun.w > 0.5)
        color += pbrDirect(n, v, u_pbrSun.xyz, u_pbrSunColor.rgb, diffuse, f0, roughness);
    int count = int(u_pbrFrame.w + 0.5);
    for (int i = 0; i < 4; ++i)
    {
        if (i >= count) break;
        vec3 d = u_pbrPointPos[i].xyz - v_position;
        float dist = length(d);
        float range = u_pbrPointPos[i].w;
        float att = 1.0 / max(u_pbrPointAtt[i].x + u_pbrPointAtt[i].y * dist + u_pbrPointAtt[i].z * dist * dist, 1e-4);
        att *= clamp((range - dist) / (0.15 * range), 0.0, 1.0); // fade over the last 15 % of the range (fx)
        if (att > 0.0)
            color += pbrDirect(n, v, d / max(dist, 1e-4), u_pbrPointColor[i].rgb * att, diffuse, f0, roughness);
    }
    if (u_pbrFrame.z > 0.5)
    {
        float ndv = max(dot(n, v), 1e-4);
        vec3 nw = pbrToWorld(n);
        vec3 rw = pbrToWorld(reflect(-v, n));
        vec2 brdf = texture2D(s_pbrBrdf, vec2(ndv, roughness)).rg;
        vec3 prefiltered = textureCubeLod(s_pbrRadiance, rw, roughness * u_pbrAmbient.w).rgb;
        float specAo = clamp(pow(ndv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
        color += u_pbrAmbient.rgb * (diffuse * pbrIrradiance(nw) * ao + prefiltered * (f0 * brdf.x + brdf.y) * specAo);
    }
#ifdef PBR_EMISSION
    color += pbrToLinear(texture2D(s_pbrEmission, uv0).rgb) * (u_pbrMaterial.z * u_pbrFrame.y);
#endif
    vec3 display = pbrToneMap(color * u_pbrFrame.x);
    gl_FragColor = ffFinish(vec4(pow(clamp(display, 0.0, 1.0), vec3_splat(1.0 / 2.2)), alpha));
}
