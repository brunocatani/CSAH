#include "VolumetricCommon.hlsli"

Texture2D<float2> FilteredVolume : register(t0);
Texture2D<float> ReceiverDepth : register(t1);
Texture2D<float> FullDepth : register(t2);
Texture2D<float3> SceneColor : register(t3);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

int2 ClampHalfCoordinate(int2 coordinate, int2 dimensions, uint eye)
{
    coordinate.y = clamp(coordinate.y, 0, dimensions.y - 1);
    const int eyeWidth = dimensions.x >> 1;
    coordinate.x = clamp(
        coordinate.x,
        eye == 0u ? 0 : eyeWidth,
        eye == 0u ? eyeWidth - 1 : dimensions.x - 1);
    return coordinate;
}

float4 main(PixelInput input) : SV_Target0
{
    uint halfWidth;
    uint halfHeight;
    FilteredVolume.GetDimensions(halfWidth, halfHeight);
    uint fullWidth;
    uint fullHeight;
    FullDepth.GetDimensions(fullWidth, fullHeight);
    const int2 fullCoordinate = min(
        int2(input.position.xy),
        int2(fullWidth - 1u, fullHeight - 1u));
    const uint eye = fullCoordinate.x < int(fullWidth >> 1u) ? 0u : 1u;
    const float2 halfPixel = input.position.xy * 0.5f - 0.5f;
    const int2 base = int2(floor(halfPixel));
    const float2 fraction = frac(halfPixel);
    const int2 dimensions = int2(halfWidth, halfHeight);
    const int2 coordinates[4] = {
        ClampHalfCoordinate(base, dimensions, eye),
        ClampHalfCoordinate(base + int2(1, 0), dimensions, eye),
        ClampHalfCoordinate(base + int2(0, 1), dimensions, eye),
        ClampHalfCoordinate(base + int2(1, 1), dimensions, eye)
    };
    const float weights[4] = {
        (1.0f - fraction.x) * (1.0f - fraction.y),
        fraction.x * (1.0f - fraction.y),
        (1.0f - fraction.x) * fraction.y,
        fraction.x * fraction.y
    };
    const float fullReceiverDepth = FullDepth.Load(int3(fullCoordinate, 0));
    const float depthScale = max(abs(1.0f - fullReceiverDepth), 1.0e-4f);
    float2 volume = 0.0f.xx;
    float weightSum = 0.0f;
    [unroll]
    for (uint tap = 0u; tap < 4u; ++tap) {
        const float tapDepth = ReceiverDepth.Load(int3(coordinates[tap], 0));
        const float relativeDepth = abs(fullReceiverDepth - tapDepth) /
            depthScale;
        const float weight = weights[tap] / (0.01f + relativeDepth);
        volume += max(
            FilteredVolume.Load(int3(coordinates[tap], 0)),
            0.0f.xx) * weight;
        weightSum += weight;
    }
    volume /= max(weightSum, 1.0e-5f);
    const float scattering =
        volume.x * max(ApplyParams.y, 0.0f) +
        volume.y * max(ApplyParams.z, 0.0f);
    float3 lightColor = max(DFLight[3].rgb, 0.0f.xxx);
    if (FrameParams.z > 0.5f) {
        lightColor = pow(lightColor, 2.2f.xxx);
    }
    const float3 radiance = min(
        scattering * max(ApplyParams.x, 0.0f) *
            max(MediumColor.w, 0.0f) *
            max(MediumColor.rgb, 0.0f.xxx) * lightColor,
        8.0f.xxx);
    const float3 scene = max(
        SceneColor.Load(int3(fullCoordinate, 0)),
        0.0f.xxx);
    return float4(scene + radiance, 1.0f);
}
