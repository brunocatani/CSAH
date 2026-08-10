#include "../LinearLighting/LinearLighting.hlsli"

#ifndef EFFECT_TECHNIQUE
#error EFFECT_TECHNIQUE must identify a verified FO4VR Effect technique
#endif

struct EffectPixelInput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#if (EFFECT_TECHNIQUE & 0x1) != 0
    float4 vertexColor : COLOR0;
#endif
    float4 fogParam : COLOR1;
#if (EFFECT_TECHNIQUE & 0x1080) != 0
    float3 particleData : TEXCOORD5;
#endif
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

cbuffer EffectPerTechnique : register(b0)
{
    float4 EffectDepthParameters : packoffset(c0);
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
Texture2D<float4> EffectDepthTexture : register(t3);

float4 LinearLightingEffectVertexColor(float4 color)
{
    if (enableLinearLighting != 0u) {
        return float4(LinearLightingEffect(color.xyz), color.w);
    }
    return exp2(log2(color) * 2.2f);
}

float EffectSoftParticleFade(float2 pixelPosition, float particleDepth)
{
    const float deviceDepth = 1.0f - EffectDepthTexture.Load(
        int3(int2(pixelPosition), 0)).x;
    const float sceneDepth = mad(
        deviceDepth,
        EffectDepthParameters.z,
        EffectDepthParameters.y);
    const float cameraFadeDepth = mad(
        EffectDepthParameters.y,
        EffectDepthParameters.z,
        EffectDepthParameters.y);
    const float2 scaledDepth = EffectLightingInfluence.yy /
        float2(sceneDepth, cameraFadeDepth);
    const float intersectionFade = saturate(
        scaledDepth.x - particleDepth);
    float cameraFade = saturate(particleDepth - scaledDepth.y);
    cameraFade = saturate((cameraFade - 0.075f) * 2.352941176470588f);
    cameraFade = cameraFade * cameraFade * (3.0f - 2.0f * cameraFade);
    return intersectionFade * cameraFade;
}

float4 PSMain(EffectPixelInput input) : SV_Target0
{
    float4 baseColor = EffectBaseColor;
    baseColor.xyz = LinearLightingEffect(baseColor.xyz);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    baseColor *= LinearLightingEffectVertexColor(input.vertexColor);
#endif
#if (EFFECT_TECHNIQUE & 0x4) != 0
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
    baseColor.xyz *= LinearLightingEffect(textureColor.xyz);
    baseColor.w *= textureColor.w;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    baseColor.w *= EffectSoftParticleFade(
        input.position.xy,
        input.particleData.z);
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
#elif (EFFECT_TECHNIQUE & 0x40) != 0
    const float alpha = baseColor.w * EffectPropertyColor.w;
    const float outputAlpha = LinearLightingEffectAlpha(alpha);
    const float3 foggedMultiplyColor = lerp(
        lightColor,
        1.0f.xxx,
        saturate(1.5f * fogFactor));
    float3 blendedColor = lerp(
        1.0f.xxx,
        foggedMultiplyColor,
        outputAlpha);
#else
    const float3 fogColor = LinearLightingFog(input.fogParam.xyz);
    float3 blendedColor = lerp(lightColor, fogColor, fogFactor);
#endif
#if (EFFECT_TECHNIQUE & 0x40) == 0 || (EFFECT_TECHNIQUE & 0x20) != 0
    const float alpha = baseColor.w * EffectPropertyColor.w;
#endif
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

#if (EFFECT_TECHNIQUE & 0x40) == 0 || (EFFECT_TECHNIQUE & 0x20) != 0
    const float outputAlpha = LinearLightingEffectAlpha(alpha);
#endif
#if (EFFECT_TECHNIQUE & 0x40000000) != 0
    blendedColor *= outputAlpha;
#endif
    return float4(blendedColor, outputAlpha);
}
