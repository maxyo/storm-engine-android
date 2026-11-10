$input v_uv
// Fast approximate anti-aliasing. Own implementation of the algorithm
// published by Timothy Lottes ("FXAA", NVIDIA, 2009-2011); no code is taken
// from the FXAA 3.11 source. Expects luma in the alpha channel.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxTexel; // xy: one texel in uv

#define FXAA_EDGE_THRESHOLD 0.125
#define FXAA_EDGE_THRESHOLD_MIN 0.0312
#define FXAA_SUBPIXEL 0.75

float lumaAt(vec2 uv)
{
    return texture2DLod(s_fxScene, uv, 0.0).a;
}

void main()
{
    vec2 t = u_fxTexel.xy;
    vec4 centre = texture2DLod(s_fxScene, v_uv, 0.0);
    float lM = centre.a;
    float lN = lumaAt(v_uv + vec2(0.0, -t.y));
    float lS = lumaAt(v_uv + vec2(0.0, t.y));
    float lW = lumaAt(v_uv + vec2(-t.x, 0.0));
    float lE = lumaAt(v_uv + vec2(t.x, 0.0));
    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
    float range = lMax - lMin;
    if (range < max(FXAA_EDGE_THRESHOLD_MIN, lMax * FXAA_EDGE_THRESHOLD))
    {
        gl_FragColor = vec4(centre.rgb, 1.0);
        return;
    }
    float lNW = lumaAt(v_uv + vec2(-t.x, -t.y));
    float lNE = lumaAt(v_uv + vec2(t.x, -t.y));
    float lSW = lumaAt(v_uv + vec2(-t.x, t.y));
    float lSE = lumaAt(v_uv + vec2(t.x, t.y));

    // Edge orientation from second differences along rows and columns.
    float edgeH = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    float edgeV = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    bool horizontal = edgeH >= edgeV;

    // The two neighbours across the edge; step towards the steeper side.
    float l1 = horizontal ? lN : lW;
    float l2 = horizontal ? lS : lE;
    float g1 = abs(l1 - lM);
    float g2 = abs(l2 - lM);
    bool firstSteeper = g1 >= g2;
    float stepLength = horizontal ? t.y : t.x;
    if (firstSteeper)
        stepLength = -stepLength;
    float gradient = 0.25 * max(g1, g2);
    float lEdge = 0.5 * ((firstSteeper ? l1 : l2) + lM);

    // Walk along the edge in both directions until the luma leaves the edge.
    vec2 uvEdge = v_uv;
    vec2 along;
    if (horizontal)
    {
        uvEdge.y += 0.5 * stepLength;
        along = vec2(t.x, 0.0);
    }
    else
    {
        uvEdge.x += 0.5 * stepLength;
        along = vec2(0.0, t.y);
    }
    vec2 uvNeg = uvEdge - along;
    vec2 uvPos = uvEdge + along;
    float endNeg = lumaAt(uvNeg) - lEdge;
    float endPos = lumaAt(uvPos) - lEdge;
    bool doneNeg = abs(endNeg) >= gradient;
    bool donePos = abs(endPos) >= gradient;
    float stride = 1.5;
    for (int i = 0; i < 5; ++i)
    {
        if (!doneNeg)
        {
            uvNeg -= along * stride;
            endNeg = lumaAt(uvNeg) - lEdge;
            doneNeg = abs(endNeg) >= gradient;
        }
        if (!donePos)
        {
            uvPos += along * stride;
            endPos = lumaAt(uvPos) - lEdge;
            donePos = abs(endPos) >= gradient;
        }
        stride *= 1.6;
    }
    float distNeg = horizontal ? v_uv.x - uvNeg.x : v_uv.y - uvNeg.y;
    float distPos = horizontal ? uvPos.x - v_uv.x : uvPos.y - v_uv.y;
    bool nearNeg = distNeg < distPos;
    float distNear = min(distNeg, distPos);
    // Shift only when the nearer end of the edge is on the other side of the edge luma.
    bool centreBelow = lM < lEdge;
    bool goodSpan = ((nearNeg ? endNeg : endPos) < 0.0) != centreBelow;
    float edgeOffset = goodSpan ? 0.5 - distNear / (distNeg + distPos) : 0.0;

    // Sub-pixel aliasing: how far the centre is from its 3x3 low-pass.
    float average = (2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) * (1.0 / 12.0);
    float sub = clamp(abs(average - lM) / range, 0.0, 1.0);
    sub = sub * sub * (3.0 - 2.0 * sub);
    float offset = max(edgeOffset, sub * sub * FXAA_SUBPIXEL);

    vec2 uv = v_uv;
    if (horizontal)
        uv.y += offset * stepLength;
    else
        uv.x += offset * stepLength;
    gl_FragColor = vec4(texture2DLod(s_fxScene, uv, 0.0).rgb, 1.0);
}
