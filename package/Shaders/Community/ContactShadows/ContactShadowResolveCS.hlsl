Texture2D<float> SceneDepth : register(t0);
Texture2D<float> RawContactShadow : register(t1);
TextureCube<float> CloudOcclusion : register(t2);
RWTexture2D<unorm float> ResolvedShadowMask : register(u0);
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
    float4 ContactParams0;
    float4 ContactParams1;
    float4 ContactParams2;
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

bool LoadSameReceiverVisibility(
    int2 requestedPixel,
    float centerDepth,
    float centerViewDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions,
    out float visibility)
{
    const int2 samplePixel = int2(
        clamp(requestedPixel.x, (int)eyeFirstPixel, (int)eyeLastPixel),
        clamp(requestedPixel.y, 0, (int)height - 1));
    const float sampleDepth = SceneDepth.Load(int3(samplePixel, 0));
    if (sampleDepth <= 1.0e-6f ||
        (sampleDepth <= 0.01f) != (centerDepth <= 0.01f)) {
        visibility = 1.0f;
        return false;
    }
    const float sampleViewDepth = abs(ReconstructViewPosition(
        (float2(samplePixel) + 0.5f) / dimensions,
        sampleDepth,
        eye).z);
    const float relativeDifference =
        abs(sampleViewDepth - centerViewDepth) /
        max(min(sampleViewDepth, centerViewDepth), 1.0f);
    if (relativeDifference > 0.03f) {
        visibility = 1.0f;
        return false;
    }
    visibility = saturate(RawContactShadow.Load(int3(samplePixel, 0)));
    return true;
}

float TwoSidedBridge(
    uint2 pixel,
    float2 offset,
    float centerDepth,
    float centerViewDepth,
    uint eye,
    uint eyeFirstPixel,
    uint eyeLastPixel,
    uint height,
    float2 dimensions)
{
    const int2 negativePixel = int2(round(float2(pixel) - offset));
    const int2 positivePixel = int2(round(float2(pixel) + offset));
    if (all(negativePixel == int2(pixel)) ||
        all(positivePixel == int2(pixel))) {
        return 1.0f;
    }
    float negativeVisibility;
    float positiveVisibility;
    const bool negativeValid = LoadSameReceiverVisibility(
        negativePixel,
        centerDepth,
        centerViewDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        negativeVisibility);
    const bool positiveValid = LoadSameReceiverVisibility(
        positivePixel,
        centerDepth,
        centerViewDepth,
        eye,
        eyeFirstPixel,
        eyeLastPixel,
        height,
        dimensions,
        positiveVisibility);
    // Visibility is lower in shadow. max() requires both sides to be dark,
    // so this closes an internal gap without dilating either outer edge.
    return negativeValid && positiveValid ?
        max(negativeVisibility, positiveVisibility) : 1.0f;
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
        ResolvedShadowMask[pixel] = 1.0f;
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
    const float3 towardLight = normalize(DFLight[eye + 1u].xyz);
    const float cloudVisibility = CloudVisibility(surface, towardLight);
    float contactVisibility = saturate(
        RawContactShadow.Load(int3(pixel, 0)));

    const bool contactEnabled =
        ContactParams2.z > 0.5f && ContactParams0.x > 0.0f;
    if (contactEnabled) {
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
        const float sampleCount = clamp(ContactParams0.w, 2.0f, 16.0f);
        const float2 averageStep = rayPixelDelta / sampleCount;
        const float centerViewDepth = abs(surface.z);
        const float bridgeQuarter = TwoSidedBridge(
            pixel,
            averageStep * 0.25f,
            centerDepth,
            centerViewDepth,
            eye,
            eyeFirstPixel,
            eyeLastPixel,
            height,
            dimensions);
        const float bridgeHalf = TwoSidedBridge(
            pixel,
            averageStep * 0.5f,
            centerDepth,
            centerViewDepth,
            eye,
            eyeFirstPixel,
            eyeLastPixel,
            height,
            dimensions);
        contactVisibility = min(
            contactVisibility,
            min(bridgeQuarter, bridgeHalf));
    }

    ResolvedShadowMask[pixel] =
        contactVisibility * cloudVisibility;
}
