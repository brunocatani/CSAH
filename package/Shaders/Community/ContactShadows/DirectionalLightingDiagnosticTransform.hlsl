#ifndef DIRECTIONAL_DIAGNOSTIC_MODE
#define DIRECTIONAL_DIAGNOSTIC_MODE 4
#endif

cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

struct PixelInput
{
    float4 position : SV_Position;
    uint eye : EYEINDEX;
    float3 decodedNormal : TEXCOORD0;
};

float3 EncodeDirection(float3 direction)
{
    return normalize(direction) * 0.5f + 0.5f;
}

float3 WorldToViewDirection(float3 direction)
{
    return normalize(float3(
        dot(Camera[0].xyz, direction),
        dot(Camera[1].xyz, direction),
        dot(Camera[2].xyz, direction)));
}

float4 PSMain(PixelInput input) : SV_Target0
{
    const float3 suppliedLight = normalize(DFLight[input.eye + 1u].xyz);
#if DIRECTIONAL_DIAGNOSTIC_MODE == 4
    const float3 signal = EncodeDirection(suppliedLight);
#elif DIRECTIONAL_DIAGNOSTIC_MODE == 5
    const float3 signal = EncodeDirection(
        WorldToViewDirection(suppliedLight));
#elif DIRECTIONAL_DIAGNOSTIC_MODE == 6
    const float correctedNdotL = saturate(dot(
        normalize(input.decodedNormal),
        WorldToViewDirection(suppliedLight)));
    const float3 signal = float3(correctedNdotL, 0.0f, 0.0f);
#else
#error Unsupported DIRECTIONAL_DIAGNOSTIC_MODE
#endif
    // DFLight stores target zero divided by three. DFComposite restores the
    // accumulation scale before the generic diagnostic presenter reads t5.
    return float4(signal / 3.0f, 1.0f);
}
