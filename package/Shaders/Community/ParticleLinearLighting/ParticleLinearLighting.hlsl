#include "../LinearLighting/LinearLighting.hlsli"

#ifndef PARTICLE_TECHNIQUE
#error PARTICLE_TECHNIQUE must identify a verified FO4VR particle technique
#endif

struct ParticlePixelInput
{
    float4 position : SV_POSITION0;
    float4 color : COLOR0;
    float2 texCoord : TEXCOORD0;
};

cbuffer ParticlePerGeometry : register(b2)
{
    float ColorScale : packoffset(c0.x);
};

SamplerState SourceSampler : register(s0);
Texture2D<float4> SourceTexture : register(t0);

#if PARTICLE_TECHNIQUE != 0
SamplerState GrayscaleSampler : register(s1);
Texture2D<float4> GrayscaleTexture : register(t1);
#endif

float4 PSMain(ParticlePixelInput input) : SV_Target0
{
    const float4 sourceColor = SourceTexture.Sample(
        SourceSampler,
        input.texCoord);
    float4 baseColor = input.color * sourceColor;

#if PARTICLE_TECHNIQUE == 1 || PARTICLE_TECHNIQUE == 3
    baseColor.xyz = GrayscaleTexture.Sample(
        GrayscaleSampler,
        float2(sourceColor.y, input.color.x)).xyz;
#endif

#if PARTICLE_TECHNIQUE == 2 || PARTICLE_TECHNIQUE == 3
    baseColor.w = GrayscaleTexture.Sample(
        GrayscaleSampler,
        float2(sourceColor.w, input.color.w)).w;
#endif

    baseColor.xyz = LinearLightingDiffuse(baseColor.xyz) * ColorScale;
    return baseColor;
}
