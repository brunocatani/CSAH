TextureCubeArray<float4> VanillaEnvironment : register(t8);
Texture2D<float4> MaterialData : register(t3);
Texture2D<float3> DFLightAlbedo : register(t29);
TextureCube<float3> PublishedEnvironment : register(t30);
TextureCube<float> PublishedValidity : register(t31);
SamplerState EnvironmentSampler : register(s8);
SamplerState MaterialSampler : register(s3);

cbuffer IblMaterialConstants : register(b5)
{
    float IblWeight : packoffset(c0.x);
    float ComplexMaterialWeight : packoffset(c0.y);
};

struct PixelInput
{
    float4 DirectionAndArray : TEXCOORD0;
    float Lod : TEXCOORD1;
    float2 ScreenUv : TEXCOORD2;
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

    float encodedMaterialTag = MaterialData.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0).w;
    float ordinaryMaterialTag = step(0.5, encodedMaterialTag);
    float metalness = saturate(
        (1.0 - encodedMaterialTag) * 2.0) *
        ComplexMaterialWeight * ordinaryMaterialTag;
    float3 retainedDiffuse = DFLightAlbedo.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0);
    float3 baseColour = retainedDiffuse / max(1.0 - metalness, 1.0 / 255.0);
    vanilla.xyz *= lerp(1.0.xxx, baseColour, metalness);
    return vanilla;
}
