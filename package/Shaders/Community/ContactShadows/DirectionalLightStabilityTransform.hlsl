cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

cbuffer StableDirectionalLight : register(b7)
{
    float4 StableWorldDirectionAndWeight;
};

struct PixelInput
{
    float4 position : SV_Position;
    uint eye : EYEINDEX;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    const float3 stockViewDirection = DFLight[input.eye + 1u].xyz;
    const float3 stableViewUnnormalized = float3(
        dot(Camera[0].xyz, StableWorldDirectionAndWeight.xyz),
        dot(Camera[1].xyz, StableWorldDirectionAndWeight.xyz),
        dot(Camera[2].xyz, StableWorldDirectionAndWeight.xyz));
    const float stableLengthSquared =
        dot(stableViewUnnormalized, stableViewUnnormalized);
    const float stableValid =
        saturate(StableWorldDirectionAndWeight.w) *
        step(1.0e-6f, stableLengthSquared);
    const float3 stableViewDirection = stableViewUnnormalized *
        rsqrt(max(stableLengthSquared, 1.0e-6f));
    const float3 selectedDirection = lerp(
        stockViewDirection,
        stableViewDirection,
        stableValid);
    return float4(selectedDirection, 0.0f);
}
