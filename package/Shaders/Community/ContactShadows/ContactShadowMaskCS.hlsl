Texture2D<float> SceneDepth : register(t0);
TextureCube<float> CloudOcclusion : register(t1);
RWTexture2D<unorm float> ContactShadowMask : register(u0);
SamplerState CloudSampler : register(s0);

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

float CloudVisibility(float3 relativeWorldPosition, float3 towardLight)
{
    if (CloudParams.x <= 0.5f) {
        return 1.0f;
    }
    const float cloudHeight = max(CloudParams.z, 1.0f);
    const float planetRadius = max(CloudParams.w, cloudHeight);
    const float shellRadius = planetRadius + cloudHeight;
    const float3 p =
        (relativeWorldPosition + float3(0.0f, 0.0f, planetRadius)) /
        shellRadius;
    const float projected = dot(p, towardLight);
    const float discriminant = max(
        projected * projected - dot(p, p) + 1.0f,
        0.0f);
    const float travel = -projected + sqrt(discriminant);
    const float3 sampleDirection =
        (p + towardLight * travel) * shellRadius -
        float3(0.0f, 0.0f, planetRadius);
    const float cloud = CloudOcclusion.SampleLevel(
        CloudSampler, sampleDirection, 0.0f);
    return saturate(1.0f - cloud * saturate(CloudParams.y));
}

uint StableRayStride(uint sampleCount)
{
    // Each stride is coprime with its matching sample count, so the complete
    // sequence visits the original midpoint lattice exactly once. The order
    // spreads every prefix across the ray. Distance fading and foveation can
    // therefore reduce work without moving surviving taps with headset motion.
    switch (sampleCount) {
    case 2u:
        return 1u;
    case 3u:
        return 2u;
    case 4u:
        return 3u;
    case 5u:
        return 3u;
    case 6u:
        return 5u;
    case 7u:
        return 4u;
    case 8u:
        return 5u;
    case 9u:
        return 5u;
    case 10u:
        return 7u;
    case 11u:
        return 6u;
    case 12u:
        return 7u;
    case 13u:
        return 7u;
    case 14u:
        return 9u;
    case 15u:
        return 8u;
    case 16u:
        return 9u;
    default:
        return 1u;
    }
}

bool LoadCompatibleViewDepth(
    int2 requestedPixel,
    float centerDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float viewDepth)
{
    const int2 samplePixel = int2(
        clamp(requestedPixel.x, (int)eyeFirstPixel, (int)eyeLastPixel),
        clamp(requestedPixel.y, 0, (int)height - 1));
    const float sampleDepth = SceneDepth.Load(int3(samplePixel, 0));
    if (sampleDepth <= 1.0e-6f ||
        (sampleDepth <= 0.01f) != (centerDepth <= 0.01f)) {
        viewDepth = 0.0f;
        return false;
    }

    viewDepth = abs(ReconstructViewDepth(
        (float2(samplePixel) + 0.5f) / dimensions,
        sampleDepth,
        eye));
    return viewDepth > 0.0f && viewDepth < 1.0e8f;
}

bool SampleEdgeAwareViewDepth(
    float2 samplePackedUv,
    float2 rayPixelDelta,
    float centerDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float sampledDepth)
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

    float firstDepth;
    float secondDepth;
    const bool firstValid = LoadCompatibleViewDepth(
        firstPixel,
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        firstDepth);
    const bool secondValid = LoadCompatibleViewDepth(
        secondPixel,
        centerDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        secondDepth);
    if (!firstValid && !secondValid) {
        sampledDepth = 0.0f;
        return false;
    }
    if (!firstValid || !secondValid) {
        sampledDepth = firstValid ? firstDepth : secondDepth;
        return true;
    }

    static const float kBilinearThreshold = 0.02f;
    const float relativeDifference =
        abs(firstDepth - secondDepth) /
        max(min(firstDepth, secondDepth), 1.0f);
    sampledDepth = relativeDifference > kBilinearThreshold ?
        (minorWeight < 0.5f ? firstDepth : secondDepth) :
        lerp(firstDepth, secondDepth, minorWeight);
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
    const float cloudVisibility = CloudVisibility(surface, towardLight);
    const bool contactEnabled =
        ContactParams2.z > 0.5f && ContactParams0.x > 0.0f;
    if (!contactEnabled) {
        ContactShadowMask[pixel] = cloudVisibility;
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
        ContactShadowMask[pixel] = cloudVisibility;
        return;
    }
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
    const uint sampleStride = StableRayStride(sampleCount);
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
    [loop]
    for (uint index = 0u; index < 16u; ++index) {
        if (index >= activeSampleCount) {
            break;
        }
        const uint sampleSlot = (index * sampleStride) % sampleCount;
        const float step =
            ((float)sampleSlot + 0.5f) / (float)sampleCount;
        const float rayFraction = 0.18f * step + 0.82f * step * step;
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
        float sampledDepth;
        if (!SampleEdgeAwareViewDepth(
                samplePackedUv,
                rayPixelDelta,
                centerDepth,
                eye,
                eyeFirstPixel,
                eyeLastPixel,
                height,
                dimensions,
                sampledDepth)) {
            continue;
        }

        const float candidateDepth = abs(
            surface.z + towardLight.z * rayDistance);
        const float separation = candidateDepth - sampledDepth;
        const float thickness = max(
            ContactParams1.x,
            candidateDepth * ContactParams0.z);
        const float hit = BlockerOcclusion(
            separation,
            ContactParams1.y,
            thickness);
        occlusion = max(occlusion, hit);
        if (occlusion >= 0.999f) {
            break;
        }
    }

    const float contactVisibility =
        1.0f - occlusion * distanceScale * saturate(ContactParams0.x);
    ContactShadowMask[pixel] = contactVisibility * cloudVisibility;
}
