// FO4VR ImageSpace[116] / BSImagespaceShaderSAORawAOCS replacement.
// The native sample count, radius, temporal accumulation, and output packing
// are preserved. Packed-atlas coordinates are made explicitly eye-local only
// where projection or history-vector units require it.

cbuffer SaoRawAoParameters : register(b0)
{
    float4 ProjectionInfo;
    float4 SsaoInfo;
    float4 ScreenInfo;
    float4 VrScaleInfo;
};

SamplerState DepthSampler : register(s0);
SamplerState AccumAoSampler : register(s2);
SamplerState MotionVectorSampler : register(s3);

Texture2D<float4> CsZBuffer : register(t0);
Texture2D<float4> Normals : register(t1);
Texture2D<float4> AccumAo : register(t2);
Texture2D<float4> MotionVectors : register(t3);
RWTexture2D<float4> Output : register(u0);

bool isRightEye(int pixelX, uint width)
{
    return (width & 1u) == 0u && pixelX >= int(width >> 1u);
}
int clampInternalEyeSampleX(int centerX, int sampleX, uint width)
{
    if ((width & 1u) != 0u) {
        return sampleX;
    }
    const int boundary = int(width >> 1u);
    if (centerX < boundary && sampleX >= boundary) {
        return boundary - 1;
    }
    if (centerX >= boundary && sampleX < boundary) {
        return boundary;
    }
    return sampleX;
}

float projectionPixelX(int pixelX, uint width)
{
    if ((width & 1u) != 0u) {
        return float(pixelX) + 0.5f;
    }
    const int eyeWidth = int(width >> 1u);
    const int eyeLocalX = pixelX - (pixelX >= eyeWidth ? eyeWidth : 0);
    return float(eyeLocalX * 2 + 1);
}

float eyeLocalUvX(float packedUvX, bool rightEye)
{
    return packedUvX * 2.0f - (rightEye ? 1.0f : 0.0f);
}

float clampInternalEyeUv(float packedUvX, bool rightEye, float border)
{
    return rightEye ?
        max(packedUvX, 0.5f + border) :
        min(packedUvX, 0.5f - border);
}

float2 viewRayForPixel(int2 pixel, uint width)
{
    const float2 projectionPixel = float2(
        projectionPixelX(pixel.x, width),
        float(pixel.y) + 0.5f);
    return projectionPixel * ProjectionInfo.xy + ProjectionInfo.zw;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pixel = int2(dispatchThreadId.xy);
    const uint width = uint(ScreenInfo.x);
    const bool rightEye = isRightEye(pixel.x, width);
    const float2 rawUv = (float2(pixel) + 0.5f) * ScreenInfo.zw;
    const float2 atlasUv = rawUv * VrScaleInfo.xy;

    const float centerDepth = CsZBuffer.SampleLevel(
        DepthSampler,
        atlasUv,
        0.0f).x;
    const float2 centerRay = viewRayForPixel(pixel, width);
    const float3 centerPosition = float3(
        centerRay * centerDepth,
        centerDepth);

    const float encodedDepth = saturate(centerDepth * 0.000143f);
    const float scaledDepth = encodedDepth * 256.0f;
    const float depthFloor = floor(scaledDepth);
    const float2 packedDepth = float2(
        depthFloor * (1.0f / 256.0f),
        scaledDepth - depthFloor);

    uint randomSeed = dispatchThreadId.x * 3u;
    randomSeed ^= dispatchThreadId.y * (dispatchThreadId.x + 1u);
    randomSeed *= 10u;
    const float rotationSeed = float(randomSeed) +
        (encodedDepth <= 0.5f ? SsaoInfo.x : 0.0f);

    float2 encodedNormal = Normals.Load(
        int3(int2(dispatchThreadId.xy) * 2, 0)).xy;
    encodedNormal = encodedNormal * 4.0f - 2.0f;
    const float encodedNormalLength = dot(encodedNormal, encodedNormal);
    const float normalScale = sqrt(
        1.0f - encodedNormalLength * 0.25f);
    const float3 normal = float3(
        -encodedNormal.x * normalScale,
        encodedNormal.y * normalScale,
        -(1.0f - encodedNormalLength * 0.5f));

    const float radius = SsaoInfo.y;
    const float radiusPixels = radius * 100.0f / centerDepth;
    const float2 eyeLocalUv = float2(
        eyeLocalUvX(atlasUv.x, rightEye),
        atlasUv.y);
    const float2 screenPosition = eyeLocalUv * 2.0f - 1.0f;
    const float depthBias = max(encodedDepth - 0.3f, 0.0f) * 10.0f +
        SsaoInfo.z;
    const float sampleBias =
        dot(abs(screenPosition), abs(screenPosition)) * 5.0f + depthBias;
    const float radiusSquared = radius * radius;

    float occlusion = 0.0f;
    [loop]
    for (int sampleIndex = 0; sampleIndex < 5; ++sampleIndex) {
        const float sampleOrdinal = float(sampleIndex) + 0.5f;
        const float sampleDistance = radiusPixels * sampleOrdinal * 0.2f;
        const float angle = sampleOrdinal * 2.512f + rotationSeed;
        float angleSin;
        float angleCos;
        sincos(angle, angleSin, angleCos);
        const float2 sampleOffset = sampleDistance * float2(
            angleCos,
            angleSin);
        int2 samplePixel = pixel + int2(sampleOffset);
        samplePixel.x = clampInternalEyeSampleX(
            pixel.x,
            samplePixel.x,
            width);

        const int mipLevel = clamp(
            int(floor(log2(sampleDistance))) - 3,
            0,
            4);
        const float2 sampleUv =
            (float2(samplePixel) + 0.5f) * ScreenInfo.zw * VrScaleInfo.xy;
        const float sampleDepth = CsZBuffer.SampleLevel(
            DepthSampler,
            sampleUv,
            float(mipLevel)).x;
        const float2 sampleRay = viewRayForPixel(samplePixel, width);
        const float3 samplePosition = float3(
            sampleRay * sampleDepth,
            sampleDepth);
        const float3 delta = samplePosition - centerPosition;
        const float distanceSquared = dot(delta, delta);
        const float normalDistance = dot(delta, normal);
        const float radiusWeight = max(
            radiusSquared - distanceSquared,
            0.0f);
        const float radiusWeightCubed =
            radiusWeight * radiusWeight * radiusWeight;
        const float angleWeight = max(
            (normalDistance - sampleBias) / (distanceSquared + 0.01f),
            0.0f);
        occlusion += radiusWeightCubed * angleWeight;
    }

    const float radiusToSixth =
        radiusSquared * radiusSquared * radiusSquared;
    const float rawAo = max(
        1.0f - (occlusion / radiusToSixth) * SsaoInfo.w,
        0.0f);

    const float2 motion = MotionVectors.SampleLevel(
        MotionVectorSampler,
        atlasUv,
        0.0f).xy;
    const float motionLengthSquared = dot(motion, motion);
    const float2 motionDirection = motion * rsqrt(motionLengthSquared);
    float2 motionProbeUv = atlasUv +
        float2(motionDirection.x * 0.5f, motionDirection.y) * 0.055f;
    motionProbeUv.x = clampInternalEyeUv(
        motionProbeUv.x,
        rightEye,
        ScreenInfo.z * 0.25f);
    const float2 adjacentMotion = MotionVectors.SampleLevel(
        MotionVectorSampler,
        motionProbeUv,
        0.0f).xy;

    const float2 packedMotion = float2(motion.x * 0.5f, motion.y);
    float2 historyUv = (rawUv * VrScaleInfo.xy + packedMotion) *
        VrScaleInfo.zw;
    historyUv.x = clampInternalEyeUv(
        historyUv.x,
        rightEye,
        ScreenInfo.z * 0.5f);
    const float4 history = AccumAo.SampleLevel(
        AccumAoSampler,
        historyUv,
        0.0f);
    const float historyDepth = dot(
        history.yz,
        float2(0.996109f, 0.003891f));
    const float depthAgreement = max(
        1.0f - abs(history.w - historyDepth) * 200.0f,
        0.0f);
    const float motionAgreement = max(
        1.0f - length(motion - adjacentMotion) * 200.0f,
        0.2f);
    const float historyWeight =
        depthAgreement * motionAgreement * 0.99f;
    const float accumulatedAo = lerp(rawAo, history.x, historyWeight);
    const bool rejectDarkHistory = rawAo >= 0.95f && history.x < 0.7f;

    Output[dispatchThreadId.xy] = float4(
        rejectDarkHistory ? rawAo : accumulatedAo,
        packedDepth,
        1.0f);
}
