TextureCubeArray<float4> VanillaEnvironment : register(t8);
Texture2D<float3> DFLightAlbedo : register(t29);
TextureCube<float3> PublishedEnvironment : register(t30);
TextureCube<float> PublishedValidity : register(t31);
Texture2D<float> SurfaceClass : register(t47);
SamplerState EnvironmentSampler : register(s8);
SamplerState MaterialSampler : register(s3);

cbuffer IblMaterialConstants : register(b5)
{
    float IblWeight : packoffset(c0.x);
    float ComplexMaterialWeight : packoffset(c0.y);
};

cbuffer BasicWetnessSettings : register(b9)
{
    // x=enabled, y=wetness, z=diffuse darkening, w=specular multiplier.
    float4 BasicWetnessParams;
    // x=IBL roughness/LOD scale. Remaining values are reserved.
    float4 BasicWetnessMaterialParams;
};

struct PixelInput
{
    float4 DirectionAndArray : TEXCOORD0;
    float Lod : TEXCOORD1;
    float EncodedMaterialTag : TEXCOORD2;
    float2 ScreenUv : TEXCOORD3;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    const float surfaceClass = SurfaceClass.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0);
    const float surfaceCode = surfaceClass * 255.0;
    const float ordinaryOrGrass = 1.0 - step(1.5, surfaceCode);
    const float terrain = 1.0 - step(0.5, abs(surfaceCode - 4.0));
    const float wetness = saturate(
        BasicWetnessParams.x * BasicWetnessParams.y *
        saturate(ordinaryOrGrass + terrain));
    const float wetLod = input.Lod * lerp(
        1.0,
        saturate(BasicWetnessMaterialParams.x),
        wetness);
    float4 vanilla = VanillaEnvironment.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray,
        wetLod);
    [branch]
    if (IblWeight > 1.0 / 255.0)
    {
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
    }

    float ordinaryMaterialTag = step(0.5, input.EncodedMaterialTag);
    float metalness = saturate(
        (1.0 - input.EncodedMaterialTag) * 2.0) *
        ComplexMaterialWeight * ordinaryMaterialTag;
    [branch]
    if (metalness > 1.0 / 255.0)
    {
        float3 retainedDiffuse = DFLightAlbedo.SampleLevel(
            MaterialSampler,
            input.ScreenUv,
            0.0);
        float3 baseColour =
            retainedDiffuse / max(1.0 - metalness, 1.0 / 255.0);
        vanilla.xyz *= lerp(1.0.xxx, baseColour, metalness);
    }
    vanilla.xyz *= lerp(
        1.0,
        max(BasicWetnessParams.w, 0.0),
        wetness);
    return vanilla;
}
