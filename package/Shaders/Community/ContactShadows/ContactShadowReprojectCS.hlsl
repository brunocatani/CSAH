Texture2D<float> SceneDepth : register(t0);
Texture2D<float> LeftEyeShadow : register(t1);
RWTexture2D<unorm float> StereoShadow : register(u0);

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

float3 ReconstructRelativeWorldPosition(
    float2 packedUv,
    float depth,
    uint eye)
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

float4 ProjectRelativeWorldPosition(float3 position, uint eye)
{
    const uint row = 4u + eye * 4u;
    const float4 homogeneousPoint = float4(position, 1.0f);
    return float4(
        dot(Camera[row + 0u], homogeneousPoint),
        dot(Camera[row + 1u], homogeneousPoint),
        dot(Camera[row + 2u], homogeneousPoint),
        dot(Camera[row + 3u], homogeneousPoint));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    SceneDepth.GetDimensions(width, height);
    const uint eyeWidth = width / 2u;
    const uint2 localPixel = dispatchThread.xy;
    if (localPixel.x >= eyeWidth || localPixel.y >= height) {
        return;
    }

    const uint2 rightPixel = uint2(
        localPixel.x + eyeWidth,
        localPixel.y);
    float result = 1.0f;
    const float rightDepth = SceneDepth.Load(int3(rightPixel, 0));
    if (rightDepth > 1.0e-6f) {
        const float2 dimensions = float2(width, height);
        const float2 rightUv =
            ((float2)rightPixel + 0.5f) / dimensions;
        const float3 rightPosition = ReconstructRelativeWorldPosition(
            rightUv,
            rightDepth,
            1u);
        const float4 leftClip = ProjectRelativeWorldPosition(
            rightPosition,
            0u);
        if (leftClip.w > 1.0e-6f) {
            const float2 leftNdc = leftClip.xy / leftClip.w;
            const float2 leftEyeUv = float2(
                leftNdc.x * 0.5f + 0.5f,
                0.5f - leftNdc.y * 0.5f);
            if (all(leftEyeUv > 0.0f) && all(leftEyeUv < 1.0f)) {
                const uint2 leftPixel = min(
                    (uint2)(leftEyeUv * float2(eyeWidth, height)),
                    uint2(eyeWidth - 1u, height - 1u));
                const float leftDepth = SceneDepth.Load(
                    int3(leftPixel, 0));
                const bool matchingDepthDomain =
                    (leftDepth <= 0.01f) == (rightDepth <= 0.01f);
                if (leftDepth > 1.0e-6f && matchingDepthDomain) {
                    const float2 leftUv =
                        ((float2)leftPixel + 0.5f) / dimensions;
                    const float3 leftPosition =
                        ReconstructRelativeWorldPosition(
                            leftUv,
                            leftDepth,
                            0u);
                    const float relativeDisagreement =
                        length(leftPosition - rightPosition) /
                        max(abs(rightPosition.z), 1.0f);
                    if (relativeDisagreement <= 0.02f) {
                        result = saturate(LeftEyeShadow.Load(
                            int3(leftPixel, 0)));
                    }
                }
            }
        }
    }
    StereoShadow[rightPixel] = result;
}
