// Exclusive lighting ownership shader. Each compiled mode exposes one native
// DFComposite input as RGB and does not compose unrelated lighting paths.
#ifndef LIGHTING_OWNERSHIP_MODE
#define LIGHTING_OWNERSHIP_MODE 0
#endif

cbuffer NativeCompositeControl : register(b0)
{
    float4 CompositeControl[3];
};

cbuffer NativeCompositeScale : register(b2)
{
    float4 PackedPixelScale;
};

Texture2D<float3> DirectSpecular : register(t4);
Texture2D<float3> DirectDiffuse : register(t5);
Texture2D<float4> ScreenSpaceReflection : register(t14);

SamplerState DirectSpecularSampler : register(s4);
SamplerState DirectDiffuseSampler : register(s5);
SamplerState ScreenSpaceReflectionSampler : register(s14);

struct PixelInput
{
    float4 Position : SV_Position;
};

float4 main(PixelInput input) : SV_Target0
{
    const float2 packedUv = input.Position.xy * PackedPixelScale.xy;

#if LIGHTING_OWNERSHIP_MODE == 1
    // Native DFLight stores direct diffuse at one-third scale in t5.
    const float3 value = DirectDiffuse.SampleLevel(
        DirectDiffuseSampler,
        packedUv,
        0.0f) * 3.0f;
#elif LIGHTING_OWNERSHIP_MODE == 2
    const float3 value = DirectSpecular.SampleLevel(
        DirectSpecularSampler,
        packedUv,
        0.0f);
#elif LIGHTING_OWNERSHIP_MODE == 3
    const float4 reflection = ScreenSpaceReflection.SampleLevel(
        ScreenSpaceReflectionSampler,
        packedUv,
        0.0f);
    const float weight = saturate(
        reflection.w * CompositeControl[2].z);
    const float3 value = reflection.xyz *
        CompositeControl[1].x * weight;
#elif LIGHTING_OWNERSHIP_MODE == 5
    const float3 value = 0.0f;
#else
#error Unsupported LIGHTING_OWNERSHIP_MODE
#endif

    return float4(value, 1.0f);
}
