// This shader is an offline DXBC instruction template. It is never loaded by
// the game. The contract generator compiles it and transplants its verified
// shallow/deep color transform into the exact FO4VR Water pixel shaders.

cbuffer WaterPerMaterial : register(b1)
{
    float4 ShallowColor : packoffset(c0);
    float4 DeepColor : packoffset(c1);
};

cbuffer LinearLightingFrame : register(b5)
{
    uint EnableLinearLighting : packoffset(c0.x);
    float3 LinearLightingFramePad0 : packoffset(c0.y);
    float4 LinearLightingFramePad1 : packoffset(c1);
    float4 LinearLightingFramePad2 : packoffset(c2);
    float WaterGamma : packoffset(c3.y);
};

struct TransformOutput
{
    float4 shallow : SV_Target0;
    float4 deep : SV_Target1;
};

TransformOutput PSMain()
{
    TransformOutput output;
    output.shallow = ShallowColor;
    output.deep = DeepColor;
    if (EnableLinearLighting != 0u) {
        output.shallow.xyz = pow(abs(output.shallow.xyz), WaterGamma);
        output.deep.xyz = pow(abs(output.deep.xyz), WaterGamma);
    }
    return output;
}
