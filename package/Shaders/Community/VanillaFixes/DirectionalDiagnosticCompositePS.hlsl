Texture2D<float4> DirectionalDiagnostic : register(t5);

struct PixelInput
{
    float4 position : SV_Position;
};

float4 main(PixelInput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    return float4(DirectionalDiagnostic.Load(int3(pixel, 0)).rgb, 1.0f);
}
