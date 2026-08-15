cbuffer ComposeConstants : register(b0)
{
    uint DestinationLeft;
    uint DestinationTop;
    uint2 RegionDimensions;
    uint EyeStride;
    float FeatherPixels;
    float Sharpness;
    uint VisualizeCenter;
};

Texture2D<float4> NeuralLeft : register(t0);
Texture2D<float4> NeuralRight : register(t1);
RWTexture2D<unorm float4> CompositionSurface : register(u0);

float3 LoadClamped(int2 coordinate, uint eye)
{
    const int2 maximumCoordinate = int2(RegionDimensions) - 1;
    const int3 location = int3(clamp(coordinate, int2(0, 0), maximumCoordinate), 0);
    return eye == 0 ? NeuralLeft.Load(location).rgb : NeuralRight.Load(location).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= RegionDimensions.x * 2 ||
        dispatchThreadId.y >= RegionDimensions.y)
        return;

    const uint eye = dispatchThreadId.x >= RegionDimensions.x ? 1 : 0;
    const uint2 localPixel = uint2(
        dispatchThreadId.x - eye * RegionDimensions.x,
        dispatchThreadId.y);
    const int2 local = int2(localPixel);
    const float3 center = LoadClamped(local, eye);
    const float3 left = LoadClamped(local + int2(-1, 0), eye);
    const float3 right = LoadClamped(local + int2(1, 0), eye);
    const float3 up = LoadClamped(local + int2(0, -1), eye);
    const float3 down = LoadClamped(local + int2(0, 1), eye);
    float3 neural = center;
    if (Sharpness > 0.0)
    {
        const float3 neighborhoodMinimum = min(
            min(min(up, left), center),
            min(right, down));
        const float3 neighborhoodMaximum = max(
            max(max(up, left), center),
            max(right, down));
        float3 amplitude = saturate(
            min(neighborhoodMinimum, 1.0 - neighborhoodMaximum) /
            max(neighborhoodMaximum, 1.0e-4));
        amplitude = sqrt(amplitude);

        // Contrast-adaptive sharpening: zero maps to the conservative -1/8
        // lobe and one to -1/5. The branch is uniform, and callers pass zero
        // when CAS is disabled so the neural result remains untouched.
        const float peak = -rcp(lerp(8.0, 5.0, saturate(Sharpness)));
        const float weight = amplitude.g * peak;
        neural = saturate(
            ((up + left + right + down) * weight + center) /
            (1.0 + 4.0 * weight));
    }

    if (VisualizeCenter != 0)
        neural = lerp(neural, float3(0.10, 0.95, 0.82), 0.30);

    const float2 pixel = float2(localPixel) + 0.5;
    const float2 opposite = float2(RegionDimensions) - pixel;
    const float edgeDistance = min(min(pixel.x, pixel.y), min(opposite.x, opposite.y));
    const float blend = FeatherPixels > 0.5 ?
        smoothstep(0.0, FeatherPixels, edgeDistance) : 1.0;

    const uint2 destination = uint2(
        eye * EyeStride + DestinationLeft + localPixel.x,
        DestinationTop + localPixel.y);
    const float4 vanilla = CompositionSurface[destination];
    CompositionSurface[destination] = float4(
        lerp(vanilla.rgb, neural, blend),
        vanilla.a);
}
