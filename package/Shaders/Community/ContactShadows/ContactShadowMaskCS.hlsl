Texture2D<float> SceneDepth : register(t0);
RWTexture2D<unorm float> ContactShadowMask : register(u0);

cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

cbuffer ContactShadowSettings : register(b13)
{
    // x=strength, y=max view-space ray length, z=relative thickness,
    // w=maximum samples.
    float4 ContactParams0;
    // x=minimum thickness, y=self-intersection bias, z=foveated outer
    // sample scale, w=foveation enabled.
    float4 ContactParams1;
    // x=view-space distance where contact shadows fade out.
    float4 ContactParams2;
    // x=enabled, y=opacity, z=cloud-shell height, w=planet radius.
    float4 CloudParams;
};

float4 NativeClip(float2 packedUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = packedUv.x / DFLight[45].x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    return float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) *
            (Stereo[0].x + 1.0f),
        1.0f - packedUv.y / DFLight[45].y * 2.0f,
        nativeDepth,
        1.0f);
}

float3 ReconstructViewPosition(float2 packedUv, float depth, uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(packedUv, depth, eye);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    return homogeneous.xyz / max(abs(homogeneous.w), 1.0e-7f);
}

float ReconstructViewDepth(float2 packedUv, float depth, uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(packedUv, depth, eye);
    const float homogeneousDepth = dot(Camera[row + 2u], clip);
    const float homogeneousW = dot(Camera[row + 3u], clip);
    return homogeneousDepth / max(abs(homogeneousW), 1.0e-7f);
}

float4 ProjectViewPosition(float3 position, uint eye)
{
    const uint row = 4u + eye * 4u;
    const float4 homogeneousPoint = float4(position, 1.0f);
    return float4(
        dot(Camera[row + 0u], homogeneousPoint),
        dot(Camera[row + 1u], homogeneousPoint),
        dot(Camera[row + 2u], homogeneousPoint),
        dot(Camera[row + 3u], homogeneousPoint));
}

bool LoadCompatibleViewPosition(
    int2 requestedPixel,
    float centerDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float3 viewPosition)
{
    const int2 samplePixel = int2(
        clamp(requestedPixel.x, (int)eyeFirstPixel, (int)eyeLastPixel),
        clamp(requestedPixel.y, 0, (int)height - 1));
    const float sampleDepth = SceneDepth.Load(int3(samplePixel, 0));
    if (sampleDepth <= 1.0e-6f ||
        (sampleDepth <= 0.01f) != (centerDepth <= 0.01f)) {
        viewPosition = 0.0f;
        return false;
    }

    viewPosition = ReconstructViewPosition(
        (float2(samplePixel) + 0.5f) / dimensions,
        sampleDepth,
        eye);
    return all(abs(viewPosition) < 1.0e8f) &&
        abs(viewPosition.z) > 0.0f;
}

bool SampleEdgeAwareViewPosition(
    float2 samplePackedUv,
    float2 rayPixelDelta,
    float centerDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float3 sampledPosition)
{
    // Bend's screen-space shadow resolve interpolates only across the ray's
    // minor axis and switches to point sampling at a depth discontinuity.
    // That keeps a projected ray from blending foreground and background
    // surfaces while removing the one-texel stepping of a nearest Load.
    const float2 pixelPosition = samplePackedUv * dimensions - 0.5f;
    const bool xMajor = abs(rayPixelDelta.x) >= abs(rayPixelDelta.y);
    const float majorCoordinate = xMajor ?
        pixelPosition.x : pixelPosition.y;
    const float minorCoordinate = xMajor ?
        pixelPosition.y : pixelPosition.x;
    const int majorPixel = (int)floor(majorCoordinate + 0.5f);
    const int minorPixel = (int)floor(minorCoordinate);
    const float minorWeight = frac(minorCoordinate);
    const int2 firstPixel = xMajor ?
        int2(majorPixel, minorPixel) :
        int2(minorPixel, majorPixel);
    const int2 secondPixel = xMajor ?
        int2(majorPixel, minorPixel + 1) :
        int2(minorPixel + 1, majorPixel);

    float3 firstPosition;
    float3 secondPosition;
    const bool firstValid = LoadCompatibleViewPosition(
        firstPixel,
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        firstPosition);
    const bool secondValid = LoadCompatibleViewPosition(
        secondPixel,
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        secondPosition);
    if (!firstValid && !secondValid) {
        sampledPosition = 0.0f;
        return false;
    }
    if (!firstValid || !secondValid) {
        sampledPosition = firstValid ? firstPosition : secondPosition;
        return true;
    }

    static const float kBilinearThreshold = 0.02f;
    const float firstDepth = abs(firstPosition.z);
    const float secondDepth = abs(secondPosition.z);
    const float relativeDifference =
        abs(firstDepth - secondDepth) /
        max(min(firstDepth, secondDepth), 1.0f);
    sampledPosition = relativeDifference > kBilinearThreshold ?
        (minorWeight < 0.5f ? firstPosition : secondPosition) :
        lerp(firstPosition, secondPosition, minorWeight);
    return true;
}

float3 ClosestSurfaceTangent(
    float3 center,
    float3 negativePosition,
    bool negativeValid,
    float3 positivePosition,
    bool positiveValid)
{
    const float3 negativeTangent = center - negativePosition;
    const float3 positiveTangent = positivePosition - center;
    if (negativeValid && positiveValid) {
        return dot(negativeTangent, negativeTangent) <
                dot(positiveTangent, positiveTangent) ?
            negativeTangent : positiveTangent;
    }
    return negativeValid ? negativeTangent : positiveTangent;
}

bool EstimateReceiverNormal(
    uint2 pixel,
    float centerDepth,
    float3 surface,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float3 receiverNormal)
{
    float3 leftPosition;
    float3 rightPosition;
    float3 upPosition;
    float3 downPosition;
    const bool leftValid = LoadCompatibleViewPosition(
        int2(pixel) + int2(-1, 0),
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        leftPosition);
    const bool rightValid = LoadCompatibleViewPosition(
        int2(pixel) + int2(1, 0),
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        rightPosition);
    const bool upValid = LoadCompatibleViewPosition(
        int2(pixel) + int2(0, -1),
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        upPosition);
    const bool downValid = LoadCompatibleViewPosition(
        int2(pixel) + int2(0, 1),
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        downPosition);
    if ((!leftValid && !rightValid) || (!upValid && !downValid)) {
        receiverNormal = 0.0f;
        return false;
    }

    const float3 tangentX = ClosestSurfaceTangent(
        surface,
        leftPosition,
        leftValid,
        rightPosition,
        rightValid);
    const float3 tangentY = ClosestSurfaceTangent(
        surface,
        upPosition,
        upValid,
        downPosition,
        downValid);
    const float3 unnormalizedNormal = cross(tangentX, tangentY);
    const float normalLengthSquared = dot(
        unnormalizedNormal,
        unnormalizedNormal);
    if (normalLengthSquared <= 1.0e-10f) {
        receiverNormal = 0.0f;
        return false;
    }
    receiverNormal = unnormalizedNormal * rsqrt(normalLengthSquared);
    return true;
}

float BlockerOcclusion(float separation, float bias, float thickness)
{
    const float validRange = thickness - bias;
    if (validRange <= 1.0e-5f) {
        return 0.0f;
    }

    // Thickness is a blocker-validity slab, not an opacity ramp. Preserve a
    // fully occluding plateau and feather only the two depth boundaries. This
    // prevents the old dark-border/transparent-interior result and stops tiny
    // reconstructed-depth changes from continuously changing shadow strength.
    const float edgeFeather = min(
        max(bias, validRange * 0.08f),
        validRange * 0.25f);
    const float entry = smoothstep(bias, bias + edgeFeather, separation);
    const float exit = 1.0f - smoothstep(
        thickness - edgeFeather,
        thickness,
        separation);
    return entry * exit;
}

float SegmentBlockerOcclusion(
    float previousSeparation,
    float separation,
    float bias,
    float thickness,
    float inferenceConfidence)
{
    const float endpointOcclusion = max(
        BlockerOcclusion(previousSeparation, bias, thickness),
        BlockerOcclusion(separation, bias, thickness));
    const float validRange = thickness - bias;
    if (validRange <= 1.0e-5f || separation <= previousSeparation) {
        return endpointOcclusion;
    }

    // Sparse point tests detect only the contour where a tap happens to land
    // inside the blocker slab. The ordered ray segment between two fixed taps
    // is continuous: if its separation interval crosses the slab, evaluate
    // the strongest point in that interval. This fills the projected blocker
    // without increasing depth reads or inventing screen-space dilation.
    const float segmentMinimum = min(previousSeparation, separation);
    const float segmentMaximum = max(previousSeparation, separation);
    if (segmentMaximum <= bias || segmentMinimum >= thickness) {
        return endpointOcclusion;
    }
    // A normal coarse crossing receives a full-confidence plateau. Attenuate
    // only jumps tens of blocker slabs wide: those are unrelated foreground
    // discontinuities, not a missed sample inside one solid blocker.
    const float spanInThicknesses =
        (segmentMaximum - segmentMinimum) / validRange;
    const float spanConfidence =
        1.0f - smoothstep(16.0f, 64.0f, spanInThicknesses);
    const float intervalProbe = clamp(
        0.5f * (bias + thickness),
        segmentMinimum,
        segmentMaximum);
    return max(
        endpointOcclusion,
        BlockerOcclusion(intervalProbe, bias, thickness) *
            spanConfidence * inferenceConfidence);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    SceneDepth.GetDimensions(width, height);
    const uint2 pixel = dispatchThread.xy;
    if (pixel.x >= width || pixel.y >= height || width < 2u) {
        return;
    }

    const float centerDepth = SceneDepth.Load(int3(pixel, 0));
    if (centerDepth <= 1.0e-6f) {
        ContactShadowMask[pixel] = 1.0f;
        return;
    }

    const uint eye = min(pixel.x / max(width / 2u, 1u), 1u);
    const uint eyeFirstPixel = eye * (width / 2u);
    const uint eyeLastPixel = (eye + 1u) * (width / 2u) - 1u;
    const float2 dimensions = float2(width, height);
    const float2 packedUv = (float2(pixel) + 0.5f) / dimensions;
    const float3 surface = ReconstructViewPosition(
        packedUv,
        centerDepth,
        eye);

    // Native DFLight evaluates N.L against DFLight[eye + 1].xyz. Marching
    // along that same effective light vector is required to find blockers.
    const float3 towardLight = normalize(DFLight[eye + 1u].xyz);
    const bool contactEnabled =
        ContactParams2.z > 0.5f && ContactParams0.x > 0.0f;
    if (!contactEnabled) {
        ContactShadowMask[pixel] = 1.0f;
        return;
    }
    // Fade by forward view depth, matching the mature VR Contact Shadows
    // contract. Euclidean length creates a literal eye-centred sphere whose
    // shadow budget slides over world geometry as the headset moves. The
    // smooth Hermite response also has zero slope at both endpoints, avoiding
    // a visible contour where the bounded raymarch reaches its far limit.
    const float viewDepth = abs(surface.z);
    const float fadeDistance = max(ContactParams2.x, 1.0f);
    const float distanceScale =
        1.0f - smoothstep(0.0f, fadeDistance, viewDepth);
    if (distanceScale <= 0.0f) {
        ContactShadowMask[pixel] = 1.0f;
        return;
    }

    float3 receiverNormal;
    if (!EstimateReceiverNormal(
            pixel,
            centerDepth,
            surface,
            eye,
            eyeFirstPixel,
            eyeLastPixel,
            height,
            dimensions,
            receiverNormal)) {
        ContactShadowMask[pixel] = 1.0f;
        return;
    }
    const float normalTowardLight = dot(receiverNormal, towardLight);
    if (abs(normalTowardLight) <= 1.0e-4f) {
        ContactShadowMask[pixel] = 1.0f;
        return;
    }
    const float planeOrientation = normalTowardLight >= 0.0f ? 1.0f : -1.0f;
    // Reject only tangent-plane reconstruction noise. Contact shadows exist
    // specifically to resolve very close blockers, so this tolerance must not
    // inherit the much wider blocker-thickness slab or grow visibly with depth.
    const float receiverPlaneBias = max(
        1.0e-3f,
        viewDepth * 1.0e-6f);
    const uint sampleCount = (uint)round(clamp(
        ContactParams0.w,
        2.0f,
        16.0f));

    const float2 eyeUv = float2(
        (float(pixel.x - eyeFirstPixel) + 0.5f) /
            max(float(width / 2u), 1.0f),
        packedUv.y);
    uint activeSampleCount = sampleCount;
    if (ContactParams1.w > 0.5f) {
        const float2 radial = (eyeUv - 0.5f) * float2(1.0f, 0.78f);
        const float outer = smoothstep(0.30f, 0.62f, length(radial));
        // The first eight taps are a stable coverage floor. Foveation may
        // reduce only user-requested quality taps above that floor, so the
        // default shadow cannot fade or lose its blocker as geometry moves
        // through a headset-centred quality region.
        const uint stableSampleFloor = min(sampleCount, 8u);
        const uint optionalSamples = sampleCount - stableSampleFloor;
        const float optionalScale = lerp(
            1.0f,
            ContactParams1.z,
            outer);
        activeSampleCount = stableSampleFloor + (uint)round(
            (float)optionalSamples * optionalScale);
    }

    const float rayLength = ContactParams0.y;
    const float4 startClip = ProjectViewPosition(surface, eye);
    const float4 endClip = ProjectViewPosition(
        surface + towardLight * rayLength,
        eye);
    const float2 startNdc = startClip.xy /
        max(abs(startClip.w), 1.0e-7f);
    const float2 endNdc = endClip.xy /
        max(abs(endClip.w), 1.0e-7f);
    const float2 rayPixelDelta = float2(
        (endNdc.x - startNdc.x) * 0.25f * dimensions.x,
        (startNdc.y - endNdc.y) * 0.5f * dimensions.y);
    float occlusion = 0.0f;
    float previousSeparation = 0.0f;
    bool previousSeparationValid = true;
    uint selectionAccumulator = 0u;
    [loop]
    for (uint sampleSlot = 0u; sampleSlot < 16u; ++sampleSlot) {
        if (sampleSlot >= sampleCount) {
            break;
        }
        // Select a fixed, ordered subset of the requested lattice. The integer
        // accumulator distributes the foveated budget across the full ray;
        // surviving taps never move when the budget changes.
        selectionAccumulator += activeSampleCount;
        if (selectionAccumulator < sampleCount) {
            continue;
        }
        selectionAccumulator -= sampleCount;
        const float rayStep =
            ((float)sampleSlot + 0.5f) / (float)sampleCount;
        const float rayFraction =
            0.18f * rayStep + 0.82f * rayStep * rayStep;
        const float rayDistance = rayLength * rayFraction;
        const float4 projected = lerp(startClip, endClip, rayFraction);
        if (projected.w <= 1.0e-5f) {
            break;
        }

        const float2 ndc = projected.xy / projected.w;
        const float2 sampleEyeUv = float2(
            ndc.x * 0.5f + 0.5f,
            0.5f - ndc.y * 0.5f);
        if (any(sampleEyeUv <= 0.0f) || any(sampleEyeUv >= 1.0f)) {
            break;
        }
        const float2 samplePackedUv = float2(
            (sampleEyeUv.x + (float)eye) * 0.5f,
            sampleEyeUv.y);
        float3 sampledPosition;
        if (!SampleEdgeAwareViewPosition(
                samplePackedUv,
                rayPixelDelta,
                centerDepth,
                eye,
                eyeFirstPixel,
                eyeLastPixel,
                height,
                dimensions,
                sampledPosition)) {
            previousSeparationValid = false;
            continue;
        }

        // A valid blocker must lie on the receiver tangent plane's light-facing
        // side. Samples on the plane are the receiver itself; samples behind it
        // are adjacent facets of convex geometry (the mailbox-body failure).
        // The orientation term makes this independent of the reconstructed
        // normal's arbitrary winding.
        const float orientedPlaneSeparation =
            dot(sampledPosition - surface, receiverNormal) *
            planeOrientation;
        if (orientedPlaneSeparation <= receiverPlaneBias) {
            previousSeparation = 0.0f;
            previousSeparationValid = true;
            continue;
        }

        const float sampledDepth = abs(sampledPosition.z);
        const float candidateDepth = abs(
            surface.z + towardLight.z * rayDistance);
        const float separation = candidateDepth - sampledDepth;
        const float thickness = max(
            ContactParams1.x,
            candidateDepth * ContactParams0.z);
        // Inferred crossings are a contact completion, not a replacement for
        // long-range shadow maps. Keep the nearby fill intact and remove the
        // detached blob as the ray approaches its configured far extent.
        const float inferenceConfidence = 1.0f - smoothstep(
            0.35f,
            0.85f,
            rayFraction);
        const float hit = previousSeparationValid ?
            SegmentBlockerOcclusion(
                previousSeparation,
                separation,
                ContactParams1.y,
                thickness,
                inferenceConfidence) :
            BlockerOcclusion(
                separation,
                ContactParams1.y,
                thickness);
        previousSeparation = separation;
        previousSeparationValid = true;
        occlusion = max(occlusion, hit);
        if (occlusion >= 0.999f) {
            break;
        }
    }

    const float contactVisibility =
        1.0f - occlusion * distanceScale * saturate(ContactParams0.x);
    ContactShadowMask[pixel] = contactVisibility;
}
