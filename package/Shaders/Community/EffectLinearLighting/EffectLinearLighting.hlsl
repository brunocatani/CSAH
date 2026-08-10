#include "../LinearLighting/LinearLighting.hlsli"

#ifndef EFFECT_TECHNIQUE
#error EFFECT_TECHNIQUE must identify a verified FO4VR Effect technique
#endif

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
    float4 baseColor = EffectBaseColor;
    baseColor.xyz = LinearLightingEffect(baseColor.xyz);
#if (EFFECT_TECHNIQUE & 0x4) != 0
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
    baseColor.xyz *= LinearLightingEffect(textureColor.xyz);
    baseColor.w *= textureColor.w;
#endif
    const float3 propertyColor =
        LinearLightingEffect(EffectPropertyColor.xyz);
    float3 lightColor = lerp(
        baseColor.xyz,
        propertyColor * baseColor.xyz,
        EffectLightingInfluence.x);
    if (enableLinearLighting != 0u) {
        lightColor *= otherEffectMult;
    }
    const float fogFactor = LinearLightingFogAlpha(input.fogParam.w);
#if (EFFECT_TECHNIQUE & 0x20) != 0
    float3 blendedColor = lightColor * (1.0f - fogFactor);
#else
    const float3 fogColor = LinearLightingFog(input.fogParam.xyz);
    float3 blendedColor = lerp(lightColor, fogColor, fogFactor);
#endif

    const float alpha = baseColor.w * EffectPropertyColor.w;
    if (alpha - EffectAlphaTest.x < 0.0f) {
        discard;
    }
    [branch] if (EffectAlphaTest.y < 1.0f) {
#if (EFFECT_TECHNIQUE & 0x4) != 0
        const float sampledAlpha = textureColor.w;
#else
        const float sampledAlpha = EffectTexture.Sample(
            EffectSampler,
            input.texCoord.xy).w;
#endif
        if (EffectAlphaTest.y - sampledAlpha < 0.0f) {
            discard;
        }
    }

    const float outputAlpha = LinearLightingEffectAlpha(alpha);
#if (EFFECT_TECHNIQUE & 0x40000000) != 0
    blendedColor *= outputAlpha;
#endif
    return float4(blendedColor, outputAlpha);
}
