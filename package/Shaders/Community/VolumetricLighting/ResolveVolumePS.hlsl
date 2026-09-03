#include "VolumetricCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture3D<float2> IntegratedVolume : register(t1);
SamplerState VolumeSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelOutput
{
    float2 volume : SV_Target0;
    float depth : SV_Target1;
};

PixelOutput main(PixelInput input)
{
    uint volumeWidth;
    uint volumeHeight;
    uint volumeDepth;
    IntegratedVolume.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
    uint fullWidth;
    uint fullHeight;
    SceneDepth.GetDimensions(fullWidth, fullHeight);
    const uint halfWidth = 2u * (((fullWidth >> 1u) + 1u) >> 1u);
    const uint halfHeight = (fullHeight + 1u) >> 1u;
    const uint2 halfCoordinate = min(
        uint2(input.position.xy),
        uint2(halfWidth - 1u, halfHeight - 1u));
    const uint eye = min(
        halfCoordinate.x / max(halfWidth >> 1u, 1u),
        1u);
    const uint2 fullCoordinate = min(
        halfCoordinate * 2u + 1u,
        uint2(fullWidth - 1u, fullHeight - 1u));
    const float depth = SceneDepth.Load(int3(fullCoordinate, 0));
    const float2 packedUv =
        (float2(fullCoordinate) + 0.5f) / float2(fullWidth, fullHeight);
    float receiverDistance = VolumeParams.x;
    if (depth < 0.999999f) {
        const float2 fullPixelPosition = packedUv /
            max(DFLight[0].xy, 1.0e-7f);
        const float3 relativePosition = ReconstructRelativePosition(
            fullPixelPosition, eye, depth);
        const float candidateDistance = length(relativePosition);
        if (Finite(candidateDistance)) {
            receiverDistance = candidateDistance;
        }
    }
    const float z = DistanceFraction(receiverDistance);
    const float3 texel = 0.5f /
        float3(volumeWidth, volumeHeight, volumeDepth);
    const float3 volumeUv = clamp(
        float3(packedUv, z),
        texel,
        1.0f.xxx - texel);
    float2 integral = max(
        IntegratedVolume.SampleLevel(VolumeSampler, volumeUv, 0.0f),
        0.0f.xx);
    integral.x = min(integral.x, integral.y);
    const float total = integral.y;
    const float lit = integral.x;
    const float occluded = max(total - lit, 0.0f);
    // A shaft requires both illuminated and occluded medium on the same
    // world-space ray. Uniform open air therefore cannot become global fog,
    // while a sunlit opening beside an occluder receives a strong response.
    const float mixedVisibility = saturate(
        4.0f * lit * occluded / max(total * total, 1.0e-6f));
    const float base = pow(max(lit - 1.0f / 128.0f, 0.0f), 1.35f);
    const float shaft = pow(max(lit, 0.0f), 0.72f) * mixedVisibility;
    PixelOutput output;
    output.volume = float2(base, shaft);
    output.depth = depth;
    return output;
}
