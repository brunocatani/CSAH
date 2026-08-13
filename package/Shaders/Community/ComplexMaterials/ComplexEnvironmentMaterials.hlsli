#ifndef FO4VR_COMMUNITY_SHADERS_COMPLEX_ENVIRONMENT_MATERIALS_HLSLI
#define FO4VR_COMMUNITY_SHADERS_COMPLEX_ENVIRONMENT_MATERIALS_HLSLI

cbuffer ComplexEnvironmentMaterialDraw : register(b11)
{
    uint enableComplexEnvironmentMaterial;
    float3 complexEnvironmentMaterialPadding;
};

static const float kComplexMaskEpsilon = 4.0 / 255.0;
static const float kComplexMetalTagRange = 0.5;
static const float kMinimumRetainedDiffuse = 1.0 / 255.0;

struct ComplexEnvironmentMaterial
{
    float active;
    float metalness;
    float encodedTag;
    float diffuseScale;
};

ComplexEnvironmentMaterial DecodeComplexEnvironmentMaterial(
    Texture2D<float4> maskTexture,
    SamplerState maskSampler,
    float2 uv,
    float4 materialSample)
{
    ComplexEnvironmentMaterial result;
    result.active = 0.0;
    result.metalness = 0.0;
    result.encodedTag = 1.0;
    result.diffuseScale = 1.0;

    [branch]
    if (enableComplexEnvironmentMaterial != 0u &&
        materialSample.y > kComplexMaskEpsilon &&
        materialSample.z > kComplexMaskEpsilon)
    {
        float4 terminalMip = maskTexture.SampleLevel(maskSampler, uv, 15.0);
        bool grayscale =
            abs(terminalMip.x - terminalMip.y) < kComplexMaskEpsilon &&
            abs(terminalMip.x - terminalMip.z) < kComplexMaskEpsilon &&
            abs(terminalMip.y - terminalMip.z) < kComplexMaskEpsilon;
        bool solidBlackHeight =
            all(terminalMip.xyz < kComplexMaskEpsilon) &&
            terminalMip.w > kComplexMaskEpsilon &&
            terminalMip.w < 1.0 - kComplexMaskEpsilon;
        bool complexMaterial =
            terminalMip.w < 1.0 - kComplexMaskEpsilon &&
            (!grayscale || solidBlackHeight);
        if (complexMaterial)
        {
            result.active = 1.0;
            result.metalness = saturate(materialSample.z);
            result.encodedTag =
                1.0 - kComplexMetalTagRange * result.metalness;
            result.diffuseScale = max(
                1.0 - result.metalness,
                kMinimumRetainedDiffuse);
        }
    }
    return result;
}

#endif
