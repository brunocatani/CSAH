#include "BloomGlareCommon.hlsli"

Texture2D<float4> BloomInput : register(t0);
RWTexture2D<float4> BloomOutput : register(u0);
SamplerState LinearClampSampler : register(s0);

static const float3 LuminanceWeights =
    float3(0.2126, 0.7152, 0.0722);

float3 ThresholdColor(float3 color)
{
    color = SanitizeHdr(color);
    const float luminance = dot(color, LuminanceWeights);
    return luminance > 1.0e-5 ?
        color * (max(luminance - BloomParams.x, 0.0) / luminance) : 0.0;
}

float2 ClampEyeUv(float2 uv, uint2 dimensions, uint eye)
{
    const float texelX = rcp((float)dimensions.x);
    const float texelY = rcp((float)dimensions.y);
    const float eyeMinimum = eye * 0.5 + texelX * 0.5;
    const float eyeMaximum = (eye + 1u) * 0.5 - texelX * 0.5;
    return float2(
        clamp(uv.x, eyeMinimum, eyeMaximum),
        clamp(uv.y, texelY * 0.5, 1.0 - texelY * 0.5));
}

float4 SampleEye(float2 uv, float2 offset, uint eye)
{
    uint width;
    uint height;
    BloomInput.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return BloomInput.SampleLevel(
        LinearClampSampler,
        ClampEyeUv(uv + offset / dimensions, dimensions, eye),
        0.0);
}

float4 Downsample13(float2 uv, uint eye)
{
    float4 result = SampleEye(uv, float2(-1.0, -1.0), eye);
    result += SampleEye(uv, float2(1.0, -1.0), eye);
    result += SampleEye(uv, float2(-1.0, 1.0), eye);
    result += SampleEye(uv, float2(1.0, 1.0), eye);
    result *= 0.125;
    result += SampleEye(uv, float2(-2.0, 0.0), eye) * 0.0625;
    result += SampleEye(uv, float2(2.0, 0.0), eye) * 0.0625;
    result += SampleEye(uv, float2(0.0, -2.0), eye) * 0.0625;
    result += SampleEye(uv, float2(0.0, 2.0), eye) * 0.0625;
    result += SampleEye(uv, float2(0.0, 0.0), eye) * 0.25;
    return result;
}

float4 Upsample9(float2 uv, uint eye)
{
    float4 result = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = (x == 0 ? 2.0 : 1.0) *
                (y == 0 ? 2.0 : 1.0) * 0.0625;
            result += SampleEye(
                uv,
                float2(x, y) * BloomParams.y,
                eye) * weight;
        }
    }
    return result;
}

[numthreads(8, 8, 1)]
void CS_Threshold(uint2 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    BloomOutput.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) {
        return;
    }
    BloomOutput[tid] = float4(ThresholdColor(BloomInput[tid].rgb), 1.0);
}

[numthreads(8, 8, 1)]
void CS_Downsample(uint2 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    BloomOutput.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) {
        return;
    }
    const uint eye = tid.x >= width / 2u ? 1u : 0u;
    const float2 uv = (float2(tid) + 0.5) / float2(width, height);
    BloomOutput[tid] = float4(SanitizeHdr(Downsample13(uv, eye).rgb), 1.0);
}

[numthreads(8, 8, 1)]
void CS_Upsample(uint2 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    BloomOutput.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) {
        return;
    }
    const uint eye = tid.x >= width / 2u ? 1u : 0u;
    const float2 uv = (float2(tid) + 0.5) / float2(width, height);
    const float3 current = SanitizeHdr(BloomOutput[tid].rgb);
    const float3 lower = SanitizeHdr(Upsample9(uv, eye).rgb);
    BloomOutput[tid] = float4(
        current * BloomParams.w + lower * BloomParams.z,
        1.0);
}
