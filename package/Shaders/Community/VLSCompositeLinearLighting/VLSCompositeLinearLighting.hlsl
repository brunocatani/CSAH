#include "../LinearLighting/LinearLighting.hlsli"

cbuffer VLSCompositePerTechnique : register(b2)
{
    float4 vlsCompositeUnused;
    float4 vlsCompositeColor;
};

Texture2D<float4> vlsCompositeTexture : register(t0);
SamplerState vlsCompositeSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION0;
    float2 texCoord : TEXCOORD0;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    float power = vlsCompositeTexture.Sample(
        vlsCompositeSampler, input.texCoord).x;
    power = LinearLightingVolumetricLighting(power.xxx).x;
    return float4(power * vlsCompositeColor.xyz, 1.0f);
}
