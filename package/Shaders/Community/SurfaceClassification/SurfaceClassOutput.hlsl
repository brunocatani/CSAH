#ifndef SURFACE_CLASS_CODE
#define SURFACE_CLASS_CODE 0
#endif

struct PixelOutput
{
    float SurfaceClass : SV_Target6;
};

PixelOutput PSMain()
{
    PixelOutput output;
    output.SurfaceClass = float(SURFACE_CLASS_CODE) / 255.0f;
    return output;
}
