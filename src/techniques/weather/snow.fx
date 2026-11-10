// Flakes of the "snow" entity (snow and dust): textured camera-facing quads, alpha blended,
// depth tested without depth writes.
technique snow_flakes
{
    pass p0
    {
        ZWriteEnable = false;
        Lighting = false;
        CullMode = none;
        AlphaTestEnable = false;
        AlphaBlendEnable = true;
        SrcBlend = srcalpha;
        DestBlend = invsrcalpha;

        ColorOp[0] = modulate;
        ColorArg1[0] = texture;
        ColorArg2[0] = diffuse;

        AlphaOp[0] = modulate;
        AlphaArg1[0] = texture;
        AlphaArg2[0] = diffuse;

        ColorOp[1] = disable;
    }
}
