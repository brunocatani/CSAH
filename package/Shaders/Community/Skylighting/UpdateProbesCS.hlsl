Texture2D<float> OcclusionDepth : register(t0);
RWTexture3D<float4> ProbeArray : register(u0);
RWTexture3D<uint> AccumulationFrames : register(u1);
SamplerComparisonState OcclusionSampler : register(s0);

cbuffer SkylightingSettings : register(b13)
{
    column_major float4x4 OcclusionViewProjection;
    float4 OcclusionDirection;
    float4 ArraySize;
    float4 CellSize;
    float4 PositionOffset;
    uint4 ArrayDimensions;
    uint4 ArrayOrigin;
    int4 ValidMargin;
    // x=minimum diffuse visibility, y=minimum specular visibility,
    // z=feature active, w=reserved.
    float4 Response;
};

static const float kPi = 3.14159265358979323846f;
static const float4 kUnitVisibilitySh =
    float4(3.54490770181103205460f, 0.0f, 0.0f, 0.0f);

float4 EvaluateSh(float3 direction)
{
    return float4(
        0.28209479177387814347f,
        -0.48860251190291992159f * direction.y,
        0.48860251190291992159f * direction.z,
        -0.48860251190291992159f * direction.x);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    const uint3 dimensions = ArrayDimensions.xyz;
    if (any(dispatchThread >= dimensions)) {
        return;
    }

    const int3 logicalCell = int3(
        (dispatchThread + dimensions - ArrayOrigin.xyz) % dimensions);
    const int3 validMinimum = max(ValidMargin.xyz, 0);
    const int3 validMaximum =
        int3(dimensions) - 1 + min(ValidMargin.xyz, 0);
    const bool previouslyValid =
        all(logicalCell >= validMinimum) &&
        all(logicalCell <= validMaximum);

    const float3 cellCentre =
        (float3(logicalCell) + 0.5f - float3(dimensions) * 0.5f) *
            CellSize.xyz +
        PositionOffset.xyz;
    const float4 projected = mul(
        OcclusionViewProjection,
        float4(cellCentre, 1.0f));
    const float reciprocalW = rcp(max(abs(projected.w), 1.0e-7f));
    const float3 occlusionPosition = projected.xyz * reciprocalW;
    const float2 occlusionUv =
        occlusionPosition.xy * float2(0.5f, -0.5f) + 0.5f;

    if (Response.z > 0.5f &&
        all(occlusionUv > 0.0f) && all(occlusionUv < 1.0f)) {
        const uint previousFrames = previouslyValid ?
            AccumulationFrames[dispatchThread] : 0u;
        const uint accumulation = min(previousFrames + 1u, 255u);
        const float visibility = OcclusionDepth.SampleCmpLevelZero(
            OcclusionSampler,
            occlusionUv,
            occlusionPosition.z);
        float4 sampledSh = EvaluateSh(
            normalize(OcclusionDirection.xyz)) *
            (visibility * 4.0f * kPi);

        static const float kConfidenceFrames = 15.0f;
        if (previousFrames != 0u) {
            const float previousConfidence =
                min(kConfidenceFrames, (float)previousFrames) /
                kConfidenceFrames;
            const float4 previous = ProbeArray[dispatchThread];
            const float4 unbiasedPrevious = kUnitVisibilitySh +
                (previous - kUnitVisibilitySh) /
                    max(previousConfidence, 1.0f / kConfidenceFrames);
            sampledSh = lerp(
                unbiasedPrevious,
                sampledSh,
                rcp((float)accumulation));
        }
        const float confidence =
            min(kConfidenceFrames, (float)accumulation) /
            kConfidenceFrames;
        ProbeArray[dispatchThread] = lerp(
            kUnitVisibilitySh,
            sampledSh,
            confidence);
        AccumulationFrames[dispatchThread] = accumulation;
    } else if (!previouslyValid) {
        ProbeArray[dispatchThread] = kUnitVisibilitySh;
        AccumulationFrames[dispatchThread] = 0u;
    }
}
