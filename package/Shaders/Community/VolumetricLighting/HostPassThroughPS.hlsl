Texture2D<float3> SceneColor : register(t0);
SamplerState SceneSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target0
{
    return float4(SceneColor.Sample(SceneSampler, input.uv), 1.0f);
}
