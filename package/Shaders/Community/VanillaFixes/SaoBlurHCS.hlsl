// FO4VR ImageSpace[117] / BSImagespaceShaderSAOBlurHCS replacement.
// The stock bilateral kernel is preserved. Only neighbor coordinates that
// cross the internal side-by-side eye boundary are clamped to their own eye.

cbuffer SaoBlurParameters : register(b0)
{
    float4 RenderTargetSize;
};

Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

static const int kTileOutputWidth = 960;
static const int kFilterRadius = 6;
static const uint kTileThreadCount = 972;

groupshared float2 Tile[kTileThreadCount];

int eyeSafeTileIndex(
    int centerX,
    int offset,
    uint width,
    uint centerTileIndex)
{
    int sampleX = centerX + offset;
    if ((width & 1u) == 0u) {
        const int eyeBoundary = int(width >> 1u);
        if (centerX < eyeBoundary && sampleX >= eyeBoundary) {
            sampleX = eyeBoundary - 1;
        } else if (centerX >= eyeBoundary && sampleX < eyeBoundary) {
            sampleX = eyeBoundary;
        }
    }
    return int(centerTileIndex) + sampleX - centerX;
}
float bilateralWeight(float sampleDepth, float centerDepth, float spatialWeight)
{
    const float depthWeight = max(
        1.0f - abs(sampleDepth - centerDepth) * 2000.0f,
        0.0f);
    return depthWeight * spatialWeight;
}

[numthreads(kTileThreadCount, 1, 1)]
void main(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex,
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int localX = int(groupIndex) - kFilterRadius;
    const int logicalX = int(groupId.x) * kTileOutputWidth + localX;
    const int logicalY = int(groupId.y);
    const float4 source = Input.Load(int3(logicalX, logicalY, 0));
    const float encodedDepth = dot(
        source.yz,
        float2(0.996109f, 0.003891f));
    Tile[groupIndex] = float2(source.x, encodedDepth);

    GroupMemoryBarrierWithGroupSync();

    const uint width = uint(RenderTargetSize.x);
    if (localX < 0 || localX >= kTileOutputWidth ||
        logicalX >= int(width)) {
        return;
    }

    const float2 center = Tile[groupIndex];
    if (center.y == 1.0f) {
        // Preserve the stock shader's zero-depth write coordinate.
        Output[dispatchThreadId.xy] = 0.0f.xxxx;
        return;
    }

    float numerator = center.x * 0.153170f;
    float denominator = 0.153170f;

    const int offsets[6] = { -6, -4, -2, 2, 4, 6 };
    const float spatialWeights[6] = {
        0.392902f,
        0.422649f,
        0.444893f,
        0.444893f,
        0.422649f,
        0.392902f,
    };

    [unroll]
    for (uint index = 0; index < 6; ++index) {
        const float2 sample = Tile[eyeSafeTileIndex(
            logicalX,
            offsets[index],
            width,
            groupIndex)];
        const float weight = bilateralWeight(
            sample.y,
            center.y,
            spatialWeights[index]);
        numerator += sample.x * weight;
        denominator += weight;
    }

    Output[uint2(logicalX, logicalY)] = float4(
        numerator / (denominator + 0.0001f),
        source.y,
        source.z,
        0.0f);
}
