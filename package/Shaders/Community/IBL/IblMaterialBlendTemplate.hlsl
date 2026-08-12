TextureCubeArray<float4> VanillaEnvironment : register(t8);
TextureCube<float3> PublishedEnvironment : register(t30);
TextureCube<float> PublishedValidity : register(t31);
SamplerState EnvironmentSampler : register(s8);

cbuffer IblMaterialConstants : register(b5)
{
    float IblWeight : packoffset(c0.x);
};

struct PixelInput
{
    float4 DirectionAndArray : TEXCOORD0;
    float Lod : TEXCOORD1;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    float4 vanilla = VanillaEnvironment.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray,
        input.Lod);
    float3 published = PublishedEnvironment.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray.xyz,
        input.Lod);
    float validity = PublishedValidity.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray.xyz,
        input.Lod);
    float weight = saturate(validity * IblWeight);
    vanilla.xyz = lerp(vanilla.xyz, published, weight);
    return vanilla;
}
