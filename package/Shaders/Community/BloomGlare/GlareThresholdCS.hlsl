#include "BloomGlareCommon.hlsli"

Texture2D<float4> SceneTexture : register(t0);
RWTexture2D<float2> FftRed : register(u0);
RWTexture2D<float2> FftGreen : register(u1);
RWTexture2D<float2> FftBlue : register(u2);

[numthreads(8, 8, 1)]
void CS_Threshold(uint2 tid : SV_DispatchThreadID)
{
    const uint resolution = FftResolution();
    if (tid.x >= resolution || tid.y >= resolution) {
        return;
    }

    const uint padding = (uint)(resolution * GlareCore.z);
    const uint sceneSize = resolution - 2u * padding;
    float3 extracted = 0.0;
    if (tid.x >= padding && tid.x < padding + sceneSize &&
        tid.y >= padding && tid.y < padding + sceneSize) {
        const float2 local = float2(tid - padding) + 0.5;
        const float2 uv = local / max((float)sceneSize, 1.0);
        const uint eyeWidth = (uint)GlareScreen.x;
        const uint screenHeight = (uint)GlareScreen.y;
        const uint2 screenPosition = uint2(
            EyeIndex() * eyeWidth + min((uint)(uv.x * eyeWidth), eyeWidth - 1u),
            min((uint)(uv.y * screenHeight), screenHeight - 1u));
        extracted = max(
            SanitizeHdr(SceneTexture[screenPosition].rgb) - GlareCore.x,
            0.0);
    }
    FftRed[tid] = float2(extracted.r, 0.0);
    FftGreen[tid] = float2(extracted.g, 0.0);
    FftBlue[tid] = float2(extracted.b, 0.0);
}
