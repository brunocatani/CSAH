Texture2D<float4> AuthoredRmaos : register(t48);
SamplerState AuthoredMaterialSampler : register(s15);

struct PixelInput
{
    float2 Uv : TEXCOORD0;
};

struct PixelOutput
{
    float4 Material : SV_Target7;
};

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    const float4 rmaos = saturate(
        AuthoredRmaos.Sample(AuthoredMaterialSampler, input.Uv));
    // RMAOS is roughness, metalness, AO, dielectric specular. Reserve the
    // upper half of the encoded roughness byte as the authored-material tag;
    // the consumer reconstructs roughness with saturate(x * 2 - 1).
    output.Material = float4(
        0.5f + rmaos.x * 0.5f,
        rmaos.yzw);
    return output;
}
