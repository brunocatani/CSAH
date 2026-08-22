// FO4VR stereo environment update. The shader projects one shared
// world-space cube into both halves of the exact packed DFComposite
// radiance/depth pair, then retains bounded validity-weighted history both
// inside and outside the current stereo frusta. Visible-direction blending
// prevents once-per-capture radiance steps while preserving scene response.

Texture2D<float3> ReflectionFreeRadiance : register(t0);
Texture2D<float> SceneDepth : register(t1);
TextureCube<float3> PreviousEnvironment : register(t2);
TextureCube<float> PreviousValidity : register(t3);
TextureCube<float4> PreviousPosition : register(t4);
RWTexture2DArray<float3> EnvironmentMip : register(u0);
RWTexture2DArray<float> EnvironmentValidity : register(u1);
RWTexture2DArray<float4> EnvironmentPosition : register(u2);
SamplerState LinearClampSampler : register(s0);

cbuffer EnvironmentUpdateConstants : register(b11)
{
    uint2 SourceExtent;
    uint TargetExtent;
    uint HistoryAvailable;
    float HistoryDecay;
    float HistoryBlend;
    uint2 Reserved;
    float3 PreviousProbeOrigin;
    uint PreviousProbeOriginValid;
};

// The exact FO4VR DFComposite draw binds an 85-float4 buffer at b12. Local
// material DXBC independently proves c0..c2 rotate world vectors into the
// current view, c4..c11 are the ordinary per-eye projections, c32..c39 are
// the ordinary per-eye inverse projections, and c59/c60 are the live per-eye
// origins. The verified 85-row VR layout carries persistent camera-position
// adjustments at c80/c81. Do not use the compressed c63..c70 rows here: the
// headset-qualified GPFIX world cache proved that they do not preserve a
// stable world-direction key under HMD rotation.
cbuffer Fo4VrSceneConstants : register(b12)
{
    float4 Scene[85];
};

static const float PositionEpsilon = 1.0e-5f;
static const float MinimumCaptureDistance = 16.5f;
static const float MaximumCaptureDistance = 2000000.0f;
groupshared float4 SharedCameraOrigins[2];

bool CameraPositionAdjust(uint eye, out float3 adjustment)
{
    adjustment = Scene[80u + eye].xyz;
    const bool finite = all(adjustment == adjustment) &&
        all(abs(adjustment) < MaximumCaptureDistance * 4.0f);
    if (!finite)
    {
        adjustment = 0.0f;
    }
    return finite;
}

bool CameraOrigin(uint eye, out float3 origin)
{
    // Exact FO4VR reflection-composite DXBC exposes the live camera-relative
    // eye origins directly at c59/c60. Matrix inversion of the projection
    // rows yields the camera-relative coordinate origin and discards headset
    // translation, which makes position history follow the player.
    origin = Scene[59u + eye].xyz;
    const bool finite = all(origin == origin) &&
        all(abs(origin) < MaximumCaptureDistance * 4.0f);
    if (!finite)
    {
        origin = 0.0f;
    }
    return finite;
}

bool ReconstructPersistentWorldPosition(
    float2 localUv,
    float depth,
    uint eye,
    out float3 worldPosition,
    out float distance)
{
    worldPosition = 0.0f;
    distance = 0.0f;
    if (!(depth > PositionEpsilon && depth < 1.0f - PositionEpsilon))
    {
        return false;
    }

    const float mappedDepth = depth <= 0.01f ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float4 clipPosition = float4(
        localUv.x * 2.0f - 1.0f,
        1.0f - localUv.y * 2.0f,
        mappedDepth,
        1.0f);
    const uint inverseProjectionBase = 32u + eye * 4u;
    const float4 homogeneousView = float4(
        dot(Scene[inverseProjectionBase + 0u], clipPosition),
        dot(Scene[inverseProjectionBase + 1u], clipPosition),
        dot(Scene[inverseProjectionBase + 2u], clipPosition),
        dot(Scene[inverseProjectionBase + 3u], clipPosition));
    if (!all(homogeneousView == homogeneousView) ||
        !(abs(homogeneousView.w) > PositionEpsilon))
    {
        return false;
    }

    const float3 eyeRelativeView =
        homogeneousView.xyz / homogeneousView.w;
    distance = length(eyeRelativeView);
    const float3 leftOrigin = Scene[59u].xyz;
    const float3 rightOrigin = Scene[60u].xyz;
    const float3 midpointOrigin = (leftOrigin + rightOrigin) * 0.5f;
    const float3 eyeOrigin = eye == 0u ? leftOrigin : rightOrigin;
    const float3 eyeToMidpointWorld = eyeOrigin - midpointOrigin;
    const float3 eyeToMidpointView = float3(
        dot(Scene[0u].xyz, eyeToMidpointWorld),
        dot(Scene[1u].xyz, eyeToMidpointWorld),
        dot(Scene[2u].xyz, eyeToMidpointWorld));
    const float3 midpointRelativeView =
        eyeRelativeView + eyeToMidpointView;
    const float3 midpointRelativeWorld = float3(
        dot(Scene[20u].xyz, midpointRelativeView),
        dot(Scene[21u].xyz, midpointRelativeView),
        dot(Scene[22u].xyz, midpointRelativeView));
    worldPosition = midpointOrigin + midpointRelativeWorld +
        Scene[80u + eye].xyz;
    return distance > 0.0f && distance < MaximumCaptureDistance &&
        all(worldPosition == worldPosition) &&
        all(abs(worldPosition) < MaximumCaptureDistance * 4.0f);
}

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

bool ProjectEye(float3 worldDirection, uint eye, out float2 localUv,
    out float edgeWeight)
{
    const float3 viewDirection = float3(
        dot(Scene[0u].xyz, worldDirection),
        dot(Scene[1u].xyz, worldDirection),
        dot(Scene[2u].xyz, worldDirection));
    const float4 viewVector = float4(viewDirection, 0.0f);
    const uint projectionBase = 4u + eye * 4u;
    const float4 clipDirection = float4(
        dot(Scene[projectionBase + 0u], viewVector),
        dot(Scene[projectionBase + 1u], viewVector),
        dot(Scene[projectionBase + 2u], viewVector),
        dot(Scene[projectionBase + 3u], viewVector));
    if (!all(clipDirection == clipDirection) ||
        !(clipDirection.w > PositionEpsilon))
    {
        localUv = 0.0f;
        edgeWeight = 0.0f;
        return false;
    }

    const float2 ndc = clipDirection.xy / clipDirection.w;
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

bool SampleEye(
    float3 worldDirection,
    uint eye,
    out float3 radiance,
    out float weight,
    out float3 worldPosition,
    out float positionWeight)
{
    worldPosition = 0.0f;
    positionWeight = 0.0f;
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

    // Both zero-depth sky and finite surfaces are valid radiance. Finite
    // positions are converted from camera-relative to persistent coordinates;
    // malformed samples fail closed.
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

    float distance;
    if (ReconstructPersistentWorldPosition(
            localUv,
            depth,
            eye,
            worldPosition,
            distance))
    {
        // Hands, weapons, and headset-adjacent geometry must not become the
        // environment reflected by the world around the player.
        if (distance < MinimumCaptureDistance)
        {
            radiance = 0.0f;
            weight = 0.0f;
            worldPosition = 0.0f;
            return false;
        }
        positionWeight = weight;
    }
    return true;
}

float3 ReprojectHistoryDirection(
    float3 worldDirection,
    float3 currentProbeOrigin)
{
    if (PreviousProbeOriginValid == 0u)
    {
        return worldDirection;
    }

    const float4 initialHit = PreviousPosition.SampleLevel(
        LinearClampSampler,
        worldDirection,
        0.0f);
    const float3 previousProbeToHit =
        initialHit.xyz - PreviousProbeOrigin;
    const float radius = length(previousProbeToHit);
    const float3 previousProbeToCurrent =
        currentProbeOrigin - PreviousProbeOrigin;
    const float currentDistanceSquared = dot(
        previousProbeToCurrent,
        previousProbeToCurrent);
    const float projection = dot(
        previousProbeToCurrent,
        worldDirection);
    const float discriminant = projection * projection -
        (currentDistanceSquared - radius * radius);
    if (!(initialHit.w > PositionEpsilon && radius > MinimumCaptureDistance &&
          currentDistanceSquared < radius * radius &&
          discriminant > PositionEpsilon))
    {
        return worldDirection;
    }

    const float travel = -projection + sqrt(discriminant);
    return normalize(
        previousProbeToCurrent + worldDirection * travel);
}

[numthreads(8, 8, 1)]
void main(
    uint3 dispatchId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x == 0u && groupThreadId.y == 0u)
    {
        float3 leftOrigin;
        float3 rightOrigin;
        const bool leftValid = CameraOrigin(0u, leftOrigin);
        const bool rightValid = CameraOrigin(1u, rightOrigin);
        SharedCameraOrigins[0] = float4(leftOrigin, leftValid ? 1.0f : 0.0f);
        SharedCameraOrigins[1] = float4(rightOrigin, rightValid ? 1.0f : 0.0f);
    }
    GroupMemoryBarrierWithGroupSync();

    if (dispatchId.x >= TargetExtent || dispatchId.y >= TargetExtent ||
        dispatchId.z >= 6u)
    {
        return;
    }

    const float2 faceCoordinate =
        ((float2(dispatchId.xy) + 0.5f) / (float)TargetExtent) * 2.0f - 1.0f;
    const float3 worldDirection = CubeDirection(
        dispatchId.z, faceCoordinate);

    float3 cameraOrigins[2];
    bool cameraOriginValid[2];
    float3 cameraPositionAdjusts[2];
    bool cameraPositionAdjustValid[2];
    cameraOrigins[0] = SharedCameraOrigins[0].xyz;
    cameraOrigins[1] = SharedCameraOrigins[1].xyz;
    cameraOriginValid[0] = SharedCameraOrigins[0].w > 0.0f;
    cameraOriginValid[1] = SharedCameraOrigins[1].w > 0.0f;
    cameraPositionAdjustValid[0] = CameraPositionAdjust(
        0u, cameraPositionAdjusts[0]);
    cameraPositionAdjustValid[1] = CameraPositionAdjust(
        1u, cameraPositionAdjusts[1]);
    float3 cameraCenter = 0.0f;
    float cameraCount = 0.0f;
    [unroll]
    for (uint cameraIndex = 0u; cameraIndex < 2u; ++cameraIndex)
    {
        if (cameraOriginValid[cameraIndex] &&
            cameraPositionAdjustValid[cameraIndex])
        {
            cameraCenter += cameraOrigins[cameraIndex] +
                cameraPositionAdjusts[cameraIndex];
            cameraCount += 1.0f;
        }
    }
    cameraCenter = cameraCount > 0.0f ?
        cameraCenter / cameraCount : 0.0f;

    float3 accumulated = 0.0f;
    float totalWeight = 0.0f;
    float3 accumulatedPosition = 0.0f;
    float totalPositionWeight = 0.0f;
    [unroll]
    for (uint eye = 0u; eye < 2u; ++eye)
    {
        float3 eyeRadiance;
        float eyeWeight;
        float3 eyePosition;
        float eyePositionWeight;
        if (SampleEye(
                worldDirection,
                eye,
                eyeRadiance,
                eyeWeight,
                eyePosition,
                eyePositionWeight))
        {
            accumulated += eyeRadiance * eyeWeight;
            totalWeight += eyeWeight;
            accumulatedPosition += eyePosition * eyePositionWeight;
            totalPositionWeight += eyePositionWeight;
        }
    }

    const float currentValidity = saturate(totalWeight);
    const float3 normalizedRadiance = totalWeight > 0.0f ?
        accumulated / totalWeight : 0.0f;
    const float3 historyDirection = HistoryAvailable != 0u ?
        ReprojectHistoryDirection(worldDirection, cameraCenter) :
        worldDirection;
    const float previousValidity = HistoryAvailable != 0u ?
        saturate(PreviousValidity.SampleLevel(
            LinearClampSampler, historyDirection, 0.0f)) : 0.0f;
    const float4 previousPosition = HistoryAvailable != 0u ?
        PreviousPosition.SampleLevel(
            LinearClampSampler, historyDirection, 0.0f) : 0.0f;
    float positionHistoryConfidence = 1.0f;
    if (previousPosition.w > 0.0f)
    {
        positionHistoryConfidence = 0.0f;
        if (cameraCount > 0.0f && all(previousPosition.xyz ==
                previousPosition.xyz))
        {
            const float3 translated = previousPosition.xyz - cameraCenter;
            const float translatedDistance = length(translated);
            if (translatedDistance > MinimumCaptureDistance &&
                translatedDistance < MaximumCaptureDistance)
            {
                const float angularAgreement = dot(
                    translated / translatedDistance,
                    worldDirection);
                positionHistoryConfidence = smoothstep(
                    0.985f,
                    0.9995f,
                    angularAgreement);
            }
        }
    }
    float retainedValidity = previousValidity * saturate(HistoryDecay) *
        positionHistoryConfidence;
    float3 previousRadiance = retainedValidity > 0.0f ?
        max(0.0f, PreviousEnvironment.SampleLevel(
            LinearClampSampler, historyDirection, 0.0f)) : 0.0f;
    bool inferredHistory = false;
    if (HistoryAvailable != 0u && retainedValidity < 0.02f &&
        currentValidity <= 0.0f)
    {
        // The coarse prior mip provides a bounded neighbourhood inference.
        // Its deliberately low confidence preserves the localized vanilla
        // cubemap as the dominant fallback for never-observed directions.
        const float inferredValidity = saturate(
            PreviousValidity.SampleLevel(
                LinearClampSampler, historyDirection, 3.0f)) *
            saturate(HistoryDecay) * 0.15f;
        if (inferredValidity > retainedValidity)
        {
            retainedValidity = inferredValidity;
            previousRadiance = max(0.0f,
                PreviousEnvironment.SampleLevel(
                    LinearClampSampler, historyDirection, 3.0f));
            inferredHistory = true;
        }
    }
    const float combinedValidity = saturate(
        currentValidity + retainedValidity * (1.0f - currentValidity));
    const float historyBlend = saturate(HistoryBlend);
    const float historyWeight = retainedValidity *
        (currentValidity > 0.0f ? historyBlend : 1.0f);
    const float currentWeight = currentValidity *
        (historyWeight > 0.0f ? 1.0f - historyBlend : 1.0f);
    const float totalBlendWeight = currentWeight + historyWeight;
    const float3 blendedRadiance = totalBlendWeight > 0.0f ?
        (normalizedRadiance * currentWeight +
            previousRadiance * historyWeight) / totalBlendWeight : 0.0f;
    const float3 combinedPremultiplied =
        blendedRadiance * combinedValidity;
    // Store premultiplied radiance so cube filtering across validity edges
    // cannot darken or amplify the normalized published result.
    EnvironmentMip[dispatchId] = max(0.0f, combinedPremultiplied);
    EnvironmentValidity[dispatchId] = combinedValidity;

    float4 publishedPosition = 0.0f;
    if (totalPositionWeight > 0.0f)
    {
        publishedPosition = float4(
            accumulatedPosition / totalPositionWeight,
            saturate(totalPositionWeight));
    }
    else if (currentValidity <= 0.0f && retainedValidity > 0.0f &&
        previousPosition.w > 0.0f && !inferredHistory)
    {
        publishedPosition = previousPosition;
    }
    EnvironmentPosition[dispatchId] = publishedPosition;
}
