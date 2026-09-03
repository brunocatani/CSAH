Texture2D<float> InputVolume : register(t0);
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

float main(PixelInput input) : SV_Target0
{
    uint width;
    uint height;
    InputVolume.GetDimensions(width, height);
    const int2 dimensions = int2(width, height);
    const int2 center = min(int2(input.position.xy), dimensions - 1);
    const uint eye = center.x < (dimensions.x >> 1) ? 0u : 1u;
#if BLUR_HORIZONTAL
    const int2 offset6 = int2(6, 0);
    const int2 offset12 = int2(12, 0);
#else
    const int2 offset6 = int2(0, 6);
    const int2 offset12 = int2(0, 12);
#endif
    const int2 minus12 = ClampCoordinate(
        center - offset12, dimensions, eye);
    const int2 minus6 = ClampCoordinate(
        center - offset6, dimensions, eye);
    const int2 plus6 = ClampCoordinate(
        center + offset6, dimensions, eye);
    const int2 plus12 = ClampCoordinate(
        center + offset12, dimensions, eye);
    const float centerDepth = ReceiverDepth.Load(int3(center, 0));
    const float depthDifference = abs(
        centerDepth * 4.0f -
        ReceiverDepth.Load(int3(minus12, 0)) -
        ReceiverDepth.Load(int3(minus6, 0)) -
        ReceiverDepth.Load(int3(plus6, 0)) -
        ReceiverDepth.Load(int3(plus12, 0)));
    const float centerVolume = InputVolume.Load(int3(center, 0));
    if (depthDifference > 0.002f) {
        return centerVolume;
    }
    return
        InputVolume.Load(int3(minus12, 0)) * 0.178400f +
        InputVolume.Load(int3(minus6, 0)) * 0.210431f +
        centerVolume * 0.222338f +
        InputVolume.Load(int3(plus6, 0)) * 0.210431f +
        InputVolume.Load(int3(plus12, 0)) * 0.178400f;
}
