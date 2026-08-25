#include "BloomGlareCommon.hlsli"

Texture2D<float2> DiffractionTexture : register(t0);
RWTexture2D<float2> PsfOutput : register(u0);
SamplerState WrapSampler : register(s0);

float3 WavelengthToXyz(float wavelength)
{
    const float x1 = (wavelength - 599.8) / 37.9;
    const float x2 = (wavelength - 442.0) / 16.0;
    const float x3 = (wavelength - 501.1) / 20.4;
    const float y1 = (wavelength - 568.8) / 46.9;
    const float y2 = (wavelength - 530.9) / 16.3;
    const float z1 = (wavelength - 437.0) / 11.8;
    const float z2 = (wavelength - 459.0) / 26.0;
    return float3(
        1.056 * exp(-0.5 * x1 * x1) +
            0.362 * exp(-0.5 * x2 * x2) -
            0.065 * exp(-0.5 * x3 * x3),
        0.821 * exp(-0.5 * y1 * y1) +
            0.286 * exp(-0.5 * y2 * y2),
        1.217 * exp(-0.5 * z1 * z1) +
            0.681 * exp(-0.5 * z2 * z2));
}

float3 XyzToLinearRgb(float3 xyz)
{
    return max(float3(
        3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
        -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
        0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z), 0.0);
}

[numthreads(8, 8, 1)]
void CS_Psf(uint2 tid : SV_DispatchThreadID)
{
    const uint resolution = FftResolution();
    if (tid.x >= resolution || tid.y >= resolution) {
        return;
    }
    float2 frequency = tid;
    if (frequency.x >= resolution * 0.5) {
        frequency.x -= resolution;
    }
    if (frequency.y >= resolution * 0.5) {
        frequency.y -= resolution;
    }
    frequency /= max(GlarePsf.z, 0.01);

    float result = 0.0;
    for (uint index = 0u; index < 32u; ++index) {
        const float wavelength =
            380.0 + index * (390.0 / 31.0) + GlareTail.y;
        const float scale = 1.0 +
            (wavelength / 575.0 - 1.0) * GlarePsf.y;
        const float2 uv =
            (frequency / scale + 0.5) / resolution;
        const float2 complexValue = DiffractionTexture.SampleLevel(
            WrapSampler, uv, 0.0);
        const float diffraction = dot(complexValue, complexValue);
        const float3 rgb = XyzToLinearRgb(WavelengthToXyz(wavelength));
#if GLARE_CHANNEL == 0
        const float channelWeight = rgb.r;
#elif GLARE_CHANNEL == 1
        const float channelWeight = rgb.g;
#else
        const float channelWeight = rgb.b;
#endif
        result += diffraction * channelWeight / 32.0;
    }
    result = pow(max(result, 0.0), GlarePsf.w);
    result = max(result - GlareTail.x, 0.0);
    PsfOutput[tid] = float2(result, 0.0);
}
