cbuffer NativeDFLight : register(b0)
{
    float4 DFLight[46];
};

cbuffer NativeStereo : register(b1)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b2)
{
    float4 Camera[51];
};

cbuffer VolumetricFrame : register(b3)
{
    float4 EyeOrigin[2];
    // x=max distance, y=extinction distance, z=density scale, w=density mix
    float4 VolumeParams;
    // x=global intensity, y=base gain, z=shaft gain, w=phase mix
    float4 ApplyParams;
    // rgb=normalized scattering colour, w=weather intensity
    float4 MediumColor;
    // x=forward g, y=backward g, z=forward weight, w=backward weight
    float4 PhaseParams;
    // x=jitter phase, y=frame phase, z=linear-lighting active, w=reserved
    float4 FrameParams;
    // xyz=world-space density wind offset, w=cloud sampling active
    float4 WindParams;
    // x=cloud opacity, y=cloud height, z=planet radius, w=reserved
    float4 CloudParams;
};

bool Finite(float value)
{
    return (asuint(value) & 0x7F800000u) != 0x7F800000u;
}

bool Finite3(float3 value)
{
    return all((asuint(value) & 0x7F800000u) != 0x7F800000u);
}

float NativeDepth(float submittedDepth)
{
    return submittedDepth <= 0.01f ?
        submittedDepth * 100.0f : submittedDepth * 1.01f - 0.01f;
}

float3 ReconstructRelativePosition(
    float2 fullPixelPosition,
    uint eye,
    float depth)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float baseClipX =
        fullPixelPosition.x * DFLight[0].x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    const float4 clip = float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) * (Stereo[0].x + 1.0f),
        1.0f - fullPixelPosition.y * DFLight[0].y * 2.0f,
        NativeDepth(depth),
        1.0f);
    const uint row = eye * 4u + (compressedNearDepth ? 40u : 32u);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    return homogeneous.xyz /
        (abs(homogeneous.w) > 1.0e-7f ? homogeneous.w : 1.0f);
}

float ProjectNativeDepth(uint eye, float3 relativePosition)
{
    const float4 position = float4(relativePosition, 1.0f);
    const uint row = 4u + eye * 4u;
    const float projectedDepth = dot(Camera[row + 2u], position);
    const float projectedW = dot(Camera[row + 3u], position);
    return projectedDepth /
        (abs(projectedW) > 1.0e-7f ? projectedW : 1.0f);
}

float3 ProjectShadow(uint eye, uint cascade, float3 relativePosition)
{
    const float4 position = float4(relativePosition, 1.0f);
    const uint row = eye == 0u ?
        (cascade == 0u ? 13u : 16u) :
        (cascade == 0u ? 25u : 28u);
    return float3(
        dot(DFLight[row + 0u], position),
        dot(DFLight[row + 1u], position),
        dot(DFLight[row + 2u], position));
}

float SampleCascade(
    Texture2DArray<float> shadow,
    SamplerComparisonState comparisonSampler,
    uint eye,
    uint cascade,
    float3 relativePosition,
    out bool inBounds)
{
    const float3 coordinate = ProjectShadow(eye, cascade, relativePosition);
    inBounds = Finite3(coordinate) && all(coordinate.xy >= 0.0f) &&
        all(coordinate.xy <= 1.0f);
    if (!inBounds) {
        return 1.0f;
    }
    const float2 depthRange = cascade == 0u ? DFLight[38].zw : DFLight[39].zw;
    const float receiverDepth = coordinate.z -
        0.275f / max(depthRange.y - depthRange.x, 1.0e-7f);
    const float visibility = shadow.SampleCmpLevelZero(
        comparisonSampler,
        float3(coordinate.xy, (float)cascade),
        receiverDepth);
    const float2 edgeDistance = min(coordinate.xy, 1.0f - coordinate.xy);
    const float nearestEdge = min(edgeDistance.x, edgeDistance.y);
    const float texel = max(abs(DFLight[37].z), abs(DFLight[37].w));
    return lerp(
        1.0f,
        visibility,
        smoothstep(0.0f, max(texel * 16.0f, 1.0e-4f), nearestEdge));
}

float SampleDirectionalShadow(
    Texture2DArray<float> shadow,
    SamplerComparisonState comparisonSampler,
    uint eye,
    float3 relativePosition)
{
    const float nativeDepth = ProjectNativeDepth(eye, relativePosition);
    const float splitNear = DFLight[11].x;
    const float splitFar = DFLight[12].x;
    const bool firstRequested = nativeDepth < splitFar;
    const bool secondRequested = nativeDepth > splitNear;
    bool firstInBounds = false;
    bool secondInBounds = false;
    float first = 1.0f;
    float second = 1.0f;
    if (firstRequested) {
        first = SampleCascade(
            shadow, comparisonSampler, eye, 0u, relativePosition,
            firstInBounds);
    }
    if (secondRequested) {
        second = SampleCascade(
            shadow, comparisonSampler, eye, 1u, relativePosition,
            secondInBounds);
    }
    if (!firstInBounds && !secondRequested) {
        second = SampleCascade(
            shadow, comparisonSampler, eye, 1u, relativePosition,
            secondInBounds);
    }
    if (!secondInBounds && !firstRequested) {
        first = SampleCascade(
            shadow, comparisonSampler, eye, 0u, relativePosition,
            firstInBounds);
    }
    if (!firstInBounds && !secondInBounds) {
        return 1.0f;
    }
    if (!firstInBounds) {
        return second;
    }
    if (!secondInBounds) {
        return first;
    }
    const float linearBlend = saturate(
        (nativeDepth - splitNear) / max(splitFar - splitNear, 1.0e-7f));
    const float blend = linearBlend * linearBlend *
        (3.0f - 2.0f * linearBlend);
    return lerp(first, second, blend);
}

float DensityHash(float3 coordinate)
{
    coordinate = frac(coordinate * 0.1031f);
    coordinate += dot(coordinate, coordinate.yzx + 33.33f);
    return frac((coordinate.x + coordinate.y) * coordinate.z);
}

float DensityNoise(float3 position)
{
    const float3 cell = floor(position);
    float3 blend = frac(position);
    blend = blend * blend * (3.0f.xxx - 2.0f * blend);
    const float x00 = lerp(
        DensityHash(cell), DensityHash(cell + float3(1.0f, 0.0f, 0.0f)),
        blend.x);
    const float x10 = lerp(
        DensityHash(cell + float3(0.0f, 1.0f, 0.0f)),
        DensityHash(cell + float3(1.0f, 1.0f, 0.0f)), blend.x);
    const float x01 = lerp(
        DensityHash(cell + float3(0.0f, 0.0f, 1.0f)),
        DensityHash(cell + float3(1.0f, 0.0f, 1.0f)), blend.x);
    const float x11 = lerp(
        DensityHash(cell + float3(0.0f, 1.0f, 1.0f)),
        DensityHash(cell + float3(1.0f, 1.0f, 1.0f)), blend.x);
    return lerp(lerp(x00, x10, blend.y), lerp(x01, x11, blend.y), blend.z);
}

float SampleDensity(uint eye, float3 relativePosition)
{
    const float3 worldPosition =
        relativePosition + EyeOrigin[eye].xyz + WindParams.xyz;
    const float noise = DensityNoise(worldPosition * VolumeParams.z);
    const float altitude = 1.0f - 0.75f * smoothstep(
        0.0f, 1.0f, saturate(worldPosition.z * (2.0f / 300.0f)));
    return lerp(1.0f, noise * altitude, saturate(VolumeParams.w));
}

float RelativePhase(float viewTowardLight)
{
    const float forwardG = clamp(PhaseParams.x, -0.92f, 0.92f);
    const float backwardG = clamp(PhaseParams.y, -0.92f, 0.92f);
    const float forwardDenominator = max(
        1.0f + forwardG * forwardG - 2.0f * forwardG * viewTowardLight,
        0.02f);
    const float backwardDenominator = max(
        1.0f + backwardG * backwardG + 2.0f * backwardG * viewTowardLight,
        0.02f);
    const float forward = (1.0f - forwardG * forwardG) /
        pow(forwardDenominator, 1.5f);
    const float backward = (1.0f - backwardG * backwardG) /
        pow(backwardDenominator, 1.5f);
    const float directional =
        (forward * PhaseParams.z + backward * PhaseParams.w) /
        max(PhaseParams.z + PhaseParams.w, 1.0e-4f);
    // Bethesda's authored phase values are retained, but a bounded relative
    // lobe prevents the view-aligned "face sun" produced by an unbounded HG
    // term in a headset.
    return lerp(
        1.0f,
        clamp(directional, 0.5f, 2.0f),
        saturate(ApplyParams.w));
}

float DistanceFraction(float distance)
{
    const float maximumDistance = max(VolumeParams.x, 1.0f);
    const float extinctionDistance = max(VolumeParams.y, 1.0f);
    const float terminal = exp(-maximumDistance / extinctionDistance);
    const float current = exp(-clamp(distance, 0.0f, maximumDistance) /
        extinctionDistance);
    return saturate((1.0f - current) / max(1.0f - terminal, 1.0e-5f));
}
