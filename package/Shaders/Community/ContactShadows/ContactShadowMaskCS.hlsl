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
    const uint sampleCount = (uint)round(clamp(
        ContactParams0.w,
        2.0f,
        16.0f));
    float sampleBudget = (float)sampleCount * distanceScale;
    if (sampleBudget <= 0.0f) {
        ContactShadowMask[pixel] = cloudVisibility;
        return;
    }

    const float2 eyeUv = float2(
        (float(pixel.x - eyeFirstPixel) + 0.5f) /
            max(float(width / 2u), 1.0f),
        packedUv.y);
    if (ContactParams1.w > 0.5f) {
        const float2 radial = (eyeUv - 0.5f) * float2(1.0f, 0.78f);
        const float outer = smoothstep(0.30f, 0.62f, length(radial));
        sampleBudget *= lerp(1.0f, ContactParams1.z, outer);
    }

    const float rayLength = ContactParams0.y;
    const uint sampleStride = StableRayStride(sampleCount);
    const float4 startClip = ProjectViewPosition(surface, eye);
    const float4 endClip = ProjectViewPosition(
        surface + towardLight * rayLength,
        eye);
    float occlusion = 0.0f;
    [loop]
    for (uint index = 0u; index < 16u; ++index) {
        if (index >= sampleCount) {
            break;
        }
        // Only the boundary tap changes continuously as the budget changes.
        // Earlier taps retain fixed ray positions, removing screen-space rings
        // and whole-lattice jumps from foveation and distance scaling.
        const float sampleWeight = saturate(sampleBudget - (float)index);
        if (sampleWeight <= 0.0f) {
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
        uint2 samplePixel = uint2(samplePackedUv * dimensions);
        samplePixel.x = clamp(samplePixel.x, eyeFirstPixel, eyeLastPixel);
        samplePixel.y = min(samplePixel.y, height - 1u);
        const float sampleDepth = SceneDepth.Load(int3(samplePixel, 0));
        if (sampleDepth <= 1.0e-6f ||
            (sampleDepth <= 0.01f) != (centerDepth <= 0.01f)) {
            continue;
        }

        const float candidateDepth = abs(
            surface.z + towardLight.z * rayDistance);
        const float sampledDepth = abs(ReconstructViewDepth(
            (float2(samplePixel) + 0.5f) / dimensions,
            sampleDepth,
            eye));
        const float separation = candidateDepth - sampledDepth;
        const float thickness = max(
            ContactParams1.x,
            candidateDepth * ContactParams0.z);
        const float hit = separation > ContactParams1.y &&
                separation < thickness ?
            1.0f - separation / thickness : 0.0f;
        occlusion = max(occlusion, hit * sampleWeight);
    }

    const float contactVisibility =
        1.0f - occlusion * saturate(ContactParams0.x);
    ContactShadowMask[pixel] = contactVisibility * cloudVisibility;
}
