Texture2D<float> SurfaceClass : register(t0);
RWStructuredBuffer<uint2> ActiveTiles : register(u0);
RWByteAddressBuffer DispatchArguments : register(u1);

cbuffer SubsurfaceScatteringConstants : register(b0)
{
    float4 TargetAndDirection;
    float4 ScatteringParameters;
    float4 ClassificationParameters;
};

groupshared uint TileContainsSkin;

bool IsSkin(float surfaceClass)
{
    return abs(surfaceClass - ScatteringParameters.w) <=
        ClassificationParameters.x;
}

[numthreads(8, 8, 1)]
void CSMain(
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0u) {
        TileContainsSkin = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 dimensions = uint2(TargetAndDirection.xy);
    const uint2 tileOrigin = groupID.xy * 16u;
    const uint2 sampleOrigin = tileOrigin + groupThreadID.xy * 2u;
    bool containsSkin = false;
    [unroll]
    for (uint y = 0u; y < 2u; ++y) {
        [unroll]
        for (uint x = 0u; x < 2u; ++x) {
            const uint2 pixel = sampleOrigin + uint2(x, y);
            if (all(pixel < dimensions)) {
                containsSkin = containsSkin || IsSkin(
                    SurfaceClass.Load(int3(pixel, 0)));
            }
        }
    }
    if (containsSkin) {
        InterlockedOr(TileContainsSkin, 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0u && TileContainsSkin != 0u) {
        uint tileIndex;
        DispatchArguments.InterlockedAdd(0u, 1u, tileIndex);
        ActiveTiles[tileIndex] = groupID.xy;
    }
}
