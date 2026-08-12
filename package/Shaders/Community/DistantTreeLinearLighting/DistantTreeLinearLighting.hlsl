#include "../LinearLighting/LinearLighting.hlsli"

cbuffer DistantTreePerTechnique : register(b0)
{
    float4 distantTreeUnusedConstants[4];
    float4 distantTreeDirectionalColor;
    float4 distantTreeAmbientColor;
};

Texture2D<float4> distantTreeDiffuseTexture : register(t0);
SamplerState distantTreeDiffuseSampler : register(s0);

struct DistantTreePixelInput
{
    float4 position : SV_POSITION;
    float3 textureAndDirectionalAmount : TEXCOORD0;
    float4 fog : TEXCOORD1;
    float clipDistance : SV_ClipDistance0;
    float cullDistance : SV_CullDistance0;
};

float4 PSMain(DistantTreePixelInput input) : SV_Target0
{
    const float3 sampledDiffuse = distantTreeDiffuseTexture.Sample(
        distantTreeDiffuseSampler,
        input.textureAndDirectionalAmount.xy).xyz;

    const float runtimeDirectionalScale =
        enableLinearLighting != 0u && isDirLightLinear == 0u ?
        max(dirLightMult, 1e-5f) : 1.0f;
    const float3 directional = LinearLightingDirectionalLight(
        distantTreeDirectionalColor.xyz / runtimeDirectionalScale,
        isDirLightLinear != 0u) *
        runtimeDirectionalScale;
    const float3 ambient = LinearLightingAmbient(
        distantTreeAmbientColor.xyz);
    const float3 surface = LinearLightingDiffuse(sampledDiffuse) *
        (input.textureAndDirectionalAmount.z * directional + ambient);

    const float3 fogColor = LinearLightingFog(input.fog.xyz);
    const float fogAmount = LinearLightingFogAlpha(input.fog.w);
    return float4(
        lerp(surface, fogColor, fogAmount) * distantTreeAmbientColor.w,
        1.0f);
}
