#include "BloomGlareCommon.hlsli"

Texture2D<float4> SceneTexture : register(t0);
Texture2D<float2> GlareRed : register(t1);
Texture2D<float2> GlareGreen : register(t2);
Texture2D<float2> GlareBlue : register(t3);
RWTexture2D<float4> GlareOutput : register(u0);
SamplerState LinearClampSampler : register(s0);

float SampleGlare(Texture2D<float2> textureValue, float2 uv)
{
    return textureValue.SampleLevel(LinearClampSampler, uv, 0.0).x;
}

[numthreads(8, 8, 1)]
void CS_Composite(uint2 tid : SV_DispatchThreadID)
{
    const uint eyeWidth = (uint)GlareScreen.x;
    const uint screenHeight = (uint)GlareScreen.y;
    if (tid.x >= eyeWidth || tid.y >= screenHeight) {
        return;
    }
    const uint2 packedPosition =
        uint2(EyeIndex() * eyeWidth + tid.x, tid.y);
    const float3 scene = SanitizeHdr(SceneTexture[packedPosition].rgb);
    const float2 localUv =
        (float2(tid) + 0.5) / float2(eyeWidth, screenHeight);
    const float sceneScale = 1.0 - 2.0 * GlareCore.z;
    const float2 fftUv = localUv * sceneScale + GlareCore.z;
    float3 glare = max(float3(
        SampleGlare(GlareRed, fftUv),
        SampleGlare(GlareGreen, fftUv),
        SampleGlare(GlareBlue, fftUv)), 0.0);
    glare = min(SanitizeHdr(glare), 65000.0);
    const float3 bright = max(scene - GlareCore.x, 0.0);
    const float3 contribution = max(glare - bright, 0.0) * GlareCore.y;
    GlareOutput[packedPosition] = float4(contribution, 1.0);
}
