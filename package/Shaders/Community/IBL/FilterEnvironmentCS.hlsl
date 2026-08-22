// Scene-linear, validity-aware GGX prefilter for the FO4VR shared environment
// provider. Uncovered directions are never synthesized. The paired validity
// chain tells later material consumers how much of each filtered sample may
// replace their existing localized vanilla cubemap.

TextureCube<float3> CapturedRadiance : register(t0);
TextureCube<float> CapturedValidity : register(t1);
RWTexture2DArray<float3> FilteredRadiance : register(u0);
RWTexture2DArray<float> FilteredValidity : register(u1);
SamplerState LinearClampSampler : register(s0);

cbuffer EnvironmentFilterConstants : register(b11)
{
    uint TargetExtent;
    uint MipLevel;
    uint MipCount;
    float Roughness;
};

static const float Pi = 3.14159265358979323846f;
static const float Epsilon = 1.0e-5f;
static const uint SampleCount = 32u;

float3 CubeDirection(uint face, float2 coordinate)
{
    float3 direction = 0.0f;
    switch (face)
    {
    case 0:
        direction = float3(1.0f, -coordinate.y, -coordinate.x);
        break;
    case 1:
        direction = float3(-1.0f, -coordinate.y, coordinate.x);
        break;
    case 2:
        direction = float3(coordinate.x, 1.0f, coordinate.y);
        break;
    case 3:
        direction = float3(coordinate.x, -1.0f, -coordinate.y);
        break;
    case 4:
        direction = float3(coordinate.x, -coordinate.y, 1.0f);
        break;
    default:
        direction = float3(-coordinate.x, -coordinate.y, -1.0f);
        break;
    }
    return normalize(direction);
}

float RadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) |
        ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) |
        ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) |
        ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) |
        ((bits & 0xFF00FF00u) >> 8u);
    return (float)bits * 2.3283064365386963e-10f;
}

float3 SampleGgx(float2 hammersley, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float phi = 2.0f * Pi * hammersley.x;
    const float cosine = sqrt((1.0f - hammersley.y) /
        (1.0f + (alphaSquared - 1.0f) * hammersley.y));
    const float sine = sqrt(saturate(1.0f - cosine * cosine));
    return float3(sine * cos(phi), sine * sin(phi), cosine);
}

void BuildBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    const float3 axis = abs(normal.z) < 0.999f ?
        float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(axis, normal));
    bitangent = cross(normal, tangent);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= TargetExtent || dispatchId.y >= TargetExtent ||
        dispatchId.z >= 6u)
    {
        return;
    }

    const float2 faceCoordinate =
        ((float2(dispatchId.xy) + 0.5f) / (float)TargetExtent) * 2.0f - 1.0f;
    const float3 normal = CubeDirection(dispatchId.z, faceCoordinate);

    if (MipLevel == 0u)
    {
        const float validity = saturate(CapturedValidity.SampleLevel(
            LinearClampSampler, normal, 0.0f));
        FilteredRadiance[dispatchId] = validity > 0.0f ?
            max(0.0f, CapturedRadiance.SampleLevel(
                LinearClampSampler, normal, 0.0f)) /
                max(validity, Epsilon) : 0.0f;
        FilteredValidity[dispatchId] = validity;
        return;
    }

    float3 tangent;
    float3 bitangent;
    BuildBasis(normal, tangent, bitangent);

    float3 accumulatedRadiance = 0.0f;
    float validWeight = 0.0f;
    float totalWeight = 0.0f;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < SampleCount; ++sampleIndex)
    {
        const float2 hammersley = float2(
            ((float)sampleIndex + 0.5f) / (float)SampleCount,
            RadicalInverse(sampleIndex));
        const float3 localHalf = SampleGgx(hammersley, Roughness);
        const float3 halfVector = normalize(
            tangent * localHalf.x + bitangent * localHalf.y +
            normal * localHalf.z);
        const float3 incoming =
            2.0f * dot(normal, halfVector) * halfVector - normal;
        const float cosine = saturate(dot(normal, incoming));
        if (cosine <= Epsilon)
        {
            continue;
        }

        const float validity = saturate(CapturedValidity.SampleLevel(
            LinearClampSampler, incoming, 0.0f));
        const float weight = cosine;
        // The capture texture stores zero outside valid texels, so hardware
        // linear sampling already returns coverage-premultiplied radiance.
        accumulatedRadiance += max(0.0f, CapturedRadiance.SampleLevel(
            LinearClampSampler, incoming, 0.0f)) * weight;
        validWeight += validity * weight;
        totalWeight += weight;
    }

    FilteredRadiance[dispatchId] = validWeight > Epsilon ?
        accumulatedRadiance / validWeight : 0.0f;
    FilteredValidity[dispatchId] = totalWeight > Epsilon ?
        saturate(validWeight / totalWeight) : 0.0f;
}
