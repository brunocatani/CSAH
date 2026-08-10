#include "../LinearLighting/LinearLighting.hlsli"

#ifndef EFFECT_TECHNIQUE
#error EFFECT_TECHNIQUE must identify a verified FO4VR Effect technique
#endif

struct EffectPixelInput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float4 pipboyTexCoord : TEXCOORD4;
#endif
#if (EFFECT_TECHNIQUE & 0x01000000) != 0
    float4 depthTestData : TEXCOORD3;
#endif
#if (EFFECT_TECHNIQUE & 0x1) != 0
    float4 vertexColor : COLOR0;
#endif
    float4 fogParam : COLOR1;
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float3 pipboyData : TEXCOORD1;
#endif
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
#if (EFFECT_TECHNIQUE & 0x01000000) != 0
    float4 EffectUnusedPerMaterialDepthTest : packoffset(c3);
    float4 EffectDepthTestParameters : packoffset(c4);
#endif
};

cbuffer EffectPerGeometry : register(b2)
{
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float4 EffectUnusedPerGeometryBeforePipboy[12] : packoffset(c0);
    float4 EffectPipboyControls : packoffset(c12);
    float4 EffectUnusedPerGeometryAfterPipboy[7] : packoffset(c13);
#else
    float4 EffectUnusedPerGeometry[20] : packoffset(c0);
#endif
    float4 EffectPropertyColor : packoffset(c20);
    float4 EffectAlphaTest : packoffset(c21);
};

SamplerState EffectSampler : register(s0);
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
SamplerState EffectGrayscaleSampler : register(s4);
#endif
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
SamplerState EffectPipboySampler : register(s6);
#endif
Texture2D<float4> EffectTexture : register(t0);
Texture2D<float4> EffectDepthTexture : register(t3);
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
Texture2D<float4> EffectGrayscaleTexture : register(t4);
#endif
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
Texture2D<float4> EffectPipboyTexture : register(t6);
#endif
#if (EFFECT_TECHNIQUE & 0x01000000) != 0
Texture2D<float4> EffectDepthTestTexture : register(t8);
#endif

float4 LinearLightingEffectVertexColor(float4 color)
{
    if (enableLinearLighting != 0u) {
        return float4(LinearLightingEffect(color.xyz), color.w);
    }
    return exp2(log2(color) * 2.2f);
}

#if (EFFECT_TECHNIQUE & 0x00100000) != 0
float4 LinearLightingEffectPipboyColor(float4 color)
{
    if (enableLinearLighting != 0u) {
        return float4(LinearLightingEffect(color.xyz), color.w);
    }
    return exp2(log2(color) * 2.2f);
}
#endif

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
#if (EFFECT_TECHNIQUE & 0x01000000) != 0
    const int2 depthTestCoordinate = int2(
        (input.depthTestData.xy + 1.0f) *
        EffectDepthTestParameters.x * 0.5f);
    const float depthTestSample = EffectDepthTestTexture.Load(
        int3(depthTestCoordinate, 0)).x;
    if (depthTestSample - input.depthTestData.z < 0.0f) {
        discard;
    }
#endif
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
    float4 baseColor = float4(
        LinearLightingEffect(EffectBaseColor.xyz),
        EffectBaseColor.w);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    baseColor.xyz *= LinearLightingEffectVertexColor(input.vertexColor).xyz;
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    baseColor.w *= input.vertexColor.w;
#else
    baseColor.w *= LinearLightingEffectVertexColor(input.vertexColor).w;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00006004) != 0
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
#if (EFFECT_TECHNIQUE & 0x00002004) == 0x4
    baseColor.xyz *= LinearLightingEffect(textureColor.xyz);
#endif
#if (EFFECT_TECHNIQUE & 0x00004000) == 0
    baseColor.w *= textureColor.w;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    const float softParticleFade = EffectSoftParticleFade(
        input.position.xy,
        input.particleData.z);
#if (EFFECT_TECHNIQUE & 0x00004000) == 0
    baseColor.w *= softParticleFade;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00002000) != 0
    float grayscaleColorY =
        pow(abs(EffectBaseColor.x), 1.0f / 2.2f) * input.texCoord.z;
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleColorY *= input.vertexColor.x;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    grayscaleColorY *= softParticleFade;
#endif
    const float2 grayscaleColorCoordinate = float2(
        pow(abs(textureColor.y), 1.0f / 2.2f),
        grayscaleColorY);
    const float3 grayscaleColor = EffectUnusedPerMaterial.x *
        EffectGrayscaleTexture.Sample(
            EffectGrayscaleSampler,
            grayscaleColorCoordinate).xyz;
    baseColor.xyz = LinearLightingEffect(grayscaleColor);
#endif
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    float grayscaleAlphaY =
        pow(abs(EffectBaseColor.w), 1.0f / 2.2f) *
        input.texCoord.z *
        pow(abs(EffectPropertyColor.w), 1.0f / 2.2f);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleAlphaY *= input.vertexColor.w;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    grayscaleAlphaY *= softParticleFade;
#endif
    const float grayscaleAlpha = EffectGrayscaleTexture.Sample(
        EffectGrayscaleSampler,
        float2(textureColor.w, grayscaleAlphaY)).w;
#endif
#else
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
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    const float alpha = grayscaleAlpha;
#else
    const float alpha = baseColor.w * EffectPropertyColor.w;
#endif
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
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    float alpha = grayscaleAlpha;
#else
    float alpha = baseColor.w * EffectPropertyColor.w;
#endif
#else
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    const float alpha = grayscaleAlpha;
#else
    const float alpha = baseColor.w * EffectPropertyColor.w;
#endif
#endif
#endif
    if (alpha - EffectAlphaTest.x < 0.0f) {
        discard;
    }
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float4 pipboyColor = EffectPipboyTexture.Sample(
        EffectPipboySampler,
        input.texCoord.xy);
    if (EffectPipboyControls.y == 0.0f) {
        pipboyColor = LinearLightingEffectPipboyColor(pipboyColor);
    }
    blendedColor += pipboyColor.xyz * EffectPipboyControls.w *
        (enableLinearLighting != 0u ? otherEffectMult : 1.0f);
    const bool usePipboyAlpha = EffectAlphaTest.w != 0.0f;
    alpha = usePipboyAlpha ? pipboyColor.w : alpha;
    if (usePipboyAlpha && EffectPipboyControls.x != 0.0f) {
        blendedColor *= EffectBaseColor.w;
        alpha *= EffectBaseColor.w;
    }
    alpha *= usePipboyAlpha ? EffectPipboyControls.z : 1.0f;
#endif
    [branch] if (EffectAlphaTest.y < 1.0f) {
#if (EFFECT_TECHNIQUE & 0x00006004) != 0
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
