// Image-neutral FO4VR stereo environment update diagnostic. The shader
// projects one shared world-space cube into both halves of the exact packed
// DFComposite radiance/depth pair. It writes only the provider's private back
// chain; publication and material consumption are separate transactions.

Texture2D<float3> ReflectionFreeRadiance : register(t0);
Texture2D<float> SceneDepth : register(t1);
RWTexture2DArray<float3> EnvironmentMip : register(u0);
SamplerState LinearClampSampler : register(s0);

cbuffer EnvironmentUpdateConstants : register(b11)
{
    uint2 SourceExtent;
    uint TargetExtent;
    uint Reserved;
};

// The exact FO4VR DFComposite draw binds an 85-float4 buffer at b12. Local
// material DXBC independently proves rows 63..70 are the current per-eye
// world-to-clip matrices: left rows 63..66, right rows 67..70.
cbuffer Fo4VrSceneConstants : register(b12)
{
    float4 Scene[85];
};

float3 CubeDirection(uint face, float2 coordinate)
{
    float3 direction = 0.0f;
    switch (face)
    {
    case 0:
        direction = float3(1.0f, coordinate.y, -coordinate.x);
        break;
    case 1:
        direction = float3(-1.0f, coordinate.y, coordinate.x);
        break;
    case 2:
        direction = float3(coordinate.x, 1.0f, -coordinate.y);
        break;
    case 3:
        direction = float3(coordinate.x, -1.0f, coordinate.y);
        break;
    case 4:
        direction = float3(coordinate.x, coordinate.y, 1.0f);
        break;
    default:
        direction = float3(-coordinate.x, coordinate.y, -1.0f);
        break;
    }
    return normalize(direction);
}

bool ProjectEye(float3 worldDirection, uint eye, out float2 localUv,
    out float edgeWeight)
{
    const uint matrixBase = 63u + eye * 4u;
    const float4 direction = float4(worldDirection, 0.0f);
    const float clipW = dot(Scene[matrixBase + 3u], direction);
    if (!(clipW > 1.0e-5f))
    {
        localUv = 0.0f;
        edgeWeight = 0.0f;
        return false;
    }

    const float2 ndc = float2(
        dot(Scene[matrixBase], direction),
        dot(Scene[matrixBase + 1u], direction)) / clipW;
    const float2 edge = 1.0f - abs(ndc);
    if (min(edge.x, edge.y) <= 0.0f)
    {
        localUv = 0.0f;
        edgeWeight = 0.0f;
        return false;
    }

    // FO4VR motion-vector DXBC uses the algebraically equivalent
    // previousUV-currentUV transform (-0.5,+0.5), proving this mapping.
    localUv = ndc * float2(0.5f, -0.5f) + 0.5f;
    edgeWeight = saturate(min(edge.x, edge.y) * 8.0f);
    edgeWeight *= edgeWeight;
    return true;
}

bool SampleEye(float3 worldDirection, uint eye, out float3 radiance,
    out float weight)
{
    float2 localUv;
    if (!ProjectEye(worldDirection, eye, localUv, weight))
    {
        radiance = 0.0f;
        return false;
    }

    const float2 halfTexel = 0.5f / float2(SourceExtent);
    float2 packedUv = float2((localUv.x + (float)eye) * 0.5f, localUv.y);
    const float eyeMinimum = (float)eye * 0.5f + halfTexel.x;
    const float eyeMaximum = ((float)eye + 1.0f) * 0.5f - halfTexel.x;
    packedUv.x = clamp(packedUv.x, eyeMinimum, eyeMaximum);
    packedUv.y = clamp(packedUv.y, halfTexel.y, 1.0f - halfTexel.y);

    // Depth participates in the capture contract now, while positional
    // history and near-player rejection remain deliberately absent from this
    // first diagnostic generation. Both zero-depth sky and finite surfaces
    // are valid radiance; only malformed samples fail closed.
    const float depth = SceneDepth.SampleLevel(
        LinearClampSampler, packedUv, 0.0f);
    const bool validDepth = depth == depth && depth >= 0.0f && depth <= 1.0f;
    radiance = ReflectionFreeRadiance.SampleLevel(
        LinearClampSampler, packedUv, 0.0f);
    const bool validRadiance = all(radiance == radiance) &&
        all(radiance >= 0.0f) && all(radiance < 65504.0f);
    if (!validDepth || !validRadiance)
    {
        radiance = 0.0f;
        weight = 0.0f;
        return false;
    }
    return true;
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
    const float3 worldDirection = CubeDirection(
        dispatchId.z, faceCoordinate);

    float3 accumulated = 0.0f;
    float totalWeight = 0.0f;
    [unroll]
    for (uint eye = 0u; eye < 2u; ++eye)
    {
        float3 eyeRadiance;
        float eyeWeight;
        if (SampleEye(worldDirection, eye, eyeRadiance, eyeWeight))
        {
            accumulated += eyeRadiance * eyeWeight;
            totalWeight += eyeWeight;
        }
    }

    EnvironmentMip[dispatchId] = totalWeight > 0.0f ?
        accumulated / totalWeight : 0.0f;
}
