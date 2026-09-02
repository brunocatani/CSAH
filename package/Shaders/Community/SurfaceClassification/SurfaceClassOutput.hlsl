#ifndef SURFACE_CLASS_CODE
#define SURFACE_CLASS_CODE 0
#endif

struct PixelOutput
{
    float SurfaceClass : SV_Target6;
    float4 PbrMaterial : SV_Target7;
};

PixelOutput PSMain()
{
    PixelOutput output;
    output.SurfaceClass = float(SURFACE_CLASS_CODE) / 255.0f;
    // Every classified material must overwrite the authored-PBR target.
    // A matched authored material uses a separate exact shader variant;
    // ordinary draws write zero so nearer geometry cannot inherit data from
    // a previously rendered surface.
    output.PbrMaterial = 0.0f;
    return output;
}
