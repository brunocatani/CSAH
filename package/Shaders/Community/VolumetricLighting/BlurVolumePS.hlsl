Texture2D<float2> InputVolume : register(t0);
Texture2D<float> ReceiverDepth : register(t1);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

int2 ClampCoordinate(int2 coordinate, int2 dimensions, uint eye)
{
    coordinate.y = clamp(coordinate.y, 0, dimensions.y - 1);
    const int eyeWidth = dimensions.x >> 1;
    coordinate.x = clamp(
        coordinate.x,
        eye == 0u ? 0 : eyeWidth,
        eye == 0u ? eyeWidth - 1 : dimensions.x - 1);
    return coordinate;
}

float2 main(PixelInput input) : SV_Target0
{
    uint width;
    uint height;
    InputVolume.GetDimensions(width, height);
    const int2 dimensions = int2(width, height);
    const int2 center = min(int2(input.position.xy), dimensions - 1);
    const uint eye = center.x < (dimensions.x >> 1) ? 0u : 1u;
#if BLUR_HORIZONTAL
    const int2 axis = int2(1, 0);
#else
    const int2 axis = int2(0, 1);
#endif
    const int offsets[5] = { -5, -2, 0, 2, 5 };
    const float weights[5] = {
        0.120078f, 0.233881f, 0.292082f, 0.233881f, 0.120078f
    };
    const float centerDepth = ReceiverDepth.Load(int3(center, 0));
    const float depthScale = max(abs(1.0f - centerDepth), 1.0e-4f);
    float2 sum = 0.0f.xx;
    float weightSum = 0.0f;
    [unroll]
    for (uint tap = 0u; tap < 5u; ++tap) {
        const int2 coordinate = ClampCoordinate(
            center + axis * offsets[tap], dimensions, eye);
        const float tapDepth = ReceiverDepth.Load(int3(coordinate, 0));
        const float relativeDepth = abs(tapDepth - centerDepth) / depthScale;
        const float bilateral = exp2(-64.0f * relativeDepth);
        const float weight = weights[tap] * bilateral;
        sum += max(InputVolume.Load(int3(coordinate, 0)), 0.0f.xx) * weight;
        weightSum += weight;
    }
    return sum / max(weightSum, 1.0e-5f);
}
