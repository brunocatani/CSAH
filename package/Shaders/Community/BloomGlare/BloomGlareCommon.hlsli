cbuffer BloomGlareCB : register(b13)
{
    // x=bloom ready, y=glare ready, zw unused.
    float4 CompositeParams;
    // x=threshold, y=radius, z=lower-mip multiplier,
    // w=current-mip multiplier.
    float4 BloomParams;
    // x=threshold, y=intensity, z=padding ratio, w=FFT resolution.
    float4 GlareCore;
    // x=eye width, y=screen height, z=eye index, w=aperture mode.
    float4 GlareScreen;
    // x=blade count, y=rotation radians, z=Fresnel exponent,
    // w=aperture size.
    float4 GlareOptics;
    // x=spherical aberration, y=chromatic spread, z=kernel scale,
    // w=PSF sharpness.
    float4 GlarePsf;
    // x=PSF noise floor, yzw unused.
    float4 GlareTail;
};

static const float Pi = 3.14159265358979323846;

uint FftResolution()
{
    return (uint)GlareCore.w;
}

uint EyeIndex()
{
    return (uint)GlareScreen.z;
}

uint ApertureMode()
{
    return (uint)GlareScreen.w;
}

uint ApertureBlades()
{
    return (uint)GlareOptics.x;
}

float3 SanitizeHdr(float3 value)
{
    const bool3 notANumber =
        !(value < 0.0 || value > 0.0 || value == 0.0);
    value = float3(
        notANumber.x ? 0.0 : value.x,
        notANumber.y ? 0.0 : value.y,
        notANumber.z ? 0.0 : value.z);
    return min(max(value, 0.0), 65000.0);
}
