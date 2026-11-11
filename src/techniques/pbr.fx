// Caribbean Legend PBR materials (renderer_bgfx/pbr): render state only. GEOM_SERVICE_R draws an object with a
// sidecar material with one of these blocks instead of its legacy technique; the shim then replaces the
// fixed-function program with the PBR program, so no texture stage or lighting state matters here.

technique PbrOpaque
{
    pass p0
    {
        AlphaTestEnable = false;
        AlphaBlendEnable = false;
        ZWriteEnable = true;
    }
}

// Sidecar "alpha": cut-out foliage, nets, fences (alpha of the albedo texture).
technique PbrAlphaTest
{
    pass p0
    {
        AlphaTestEnable = true;
        AlphaFunc = greater;
        AlphaRef = 0x80;
        AlphaBlendEnable = false;
        ZWriteEnable = true;
    }
}

// Legacy blend techniques (fading characters and items): alpha = albedo alpha x texture factor alpha.
technique PbrBlend
{
    pass p0
    {
        AlphaTestEnable = false;
        AlphaBlendEnable = true;
        SrcBlend = srcalpha;
        DestBlend = invsrcalpha;
    }
}
