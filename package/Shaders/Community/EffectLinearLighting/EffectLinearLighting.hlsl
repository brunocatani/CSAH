#include "../LinearLighting/LinearLighting.hlsli"

struct EffectPixelInput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
    float4 fogParam : COLOR1;
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

cbuffer EffectPerMaterial : register(b1)
{
    float4 EffectBaseColor : packoffset(c0);
    float4 EffectUnusedPerMaterial : packoffset(c1);
    float4 EffectLightingInfluence : packoffset(c2);
};

cbuffer EffectPerGeometry : register(b2)
{
    float4 EffectUnusedPerGeometry[20] : packoffset(c0);
    float4 EffectPropertyColor : packoffset(c20);
    float4 EffectAlphaTest : packoffset(c21);
};

SamplerState EffectSampler : register(s0);
Texture2D<float4> EffectTexture : register(t0);

float4 PSMain(EffectPixelInput input) : SV_Target0
{
    const float3 baseColor = LinearLightingEffect(EffectBaseColor.xyz);
    const float3 propertyColor =
        LinearLightingEffect(EffectPropertyColor.xyz);
    float3 lightColor = lerp(
        baseColor,
        propertyColor * baseColor,
        EffectLightingInfluence.x);
    if (enableLinearLighting != 0u) {
        lightColor *= otherEffectMult;
    }
    const float3 fogColor = LinearLightingFog(input.fogParam.xyz);
    const float fogFactor = LinearLightingFogAlpha(input.fogParam.w);
    const float3 blendedColor = lerp(lightColor, fogColor, fogFactor);

    const float alpha = EffectBaseColor.w * EffectPropertyColor.w;
    if (alpha - EffectAlphaTest.x < 0.0f) {
        discard;
    }
    [branch] if (EffectAlphaTest.y < 1.0f) {
        const float sampledAlpha = EffectTexture.Sample(
            EffectSampler,
            input.texCoord.xy).w;
        if (EffectAlphaTest.y - sampledAlpha < 0.0f) {
            discard;
        }
    }

    return float4(blendedColor, LinearLightingEffectAlpha(alpha));
}
