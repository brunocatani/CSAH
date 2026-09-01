// Exclusive lighting ownership shader. Each compiled mode exposes one native
// DFComposite input as RGB and does not compose unrelated lighting paths.
#ifndef LIGHTING_OWNERSHIP_MODE
#define LIGHTING_OWNERSHIP_MODE 0
#endif

cbuffer NativeCompositeScale : register(b2)
{
    float4 PackedPixelScale;
};

Texture2D<float3> DirectSpecular : register(t4);
Texture2D<float3> DirectDiffuse : register(t5);

SamplerState DirectSpecularSampler : register(s4);
SamplerState DirectDiffuseSampler : register(s5);

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
#elif LIGHTING_OWNERSHIP_MODE == 5
    const float3 value = 0.0f;
#else
#error Unsupported LIGHTING_OWNERSHIP_MODE
#endif

    return float4(value, 1.0f);
}
