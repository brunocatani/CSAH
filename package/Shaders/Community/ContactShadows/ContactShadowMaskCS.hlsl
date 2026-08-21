// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Adapted for FO4VR from the Bend screen-space shadow wavefront algorithm.

Texture2D<float> SceneDepth : register(t0);

struct DispatchRecord
{
    float4 lightCoordinate;
    int2 waveOffset;
    uint eye;
    uint active;
};

StructuredBuffer<DispatchRecord> DispatchRecords : register(t1);
RWTexture2D<unorm float> ContactShadowMask : register(u0);

cbuffer ContactShadowSettings : register(b13)
{
    // x=strength, y=maximum screen-space ray length, z=surface thickness,
    // w=quality cap in legacy sample units.
    float4 ContactParams0;
    // x/y are retained for binary compatibility, z=foveated outer sample
    // scale, w=foveation enabled.
    float4 ContactParams1;
    // x=view-space fade distance, y=mask active, z=contact active.
    float4 ContactParams2;
    float4 CloudParams;
};

static const uint kWaveSize = 64u;
static const uint kMaximumSampleCount = 256u;
static const uint kReadCount = kMaximumSampleCount / kWaveSize + 2u;
static const uint kInvalidDepthDomain = 2u;
static const float kFarDepthValue = 1.0f;
static const float kNearDepthValue = 0.0f;

groupshared float SharedDepth[kReadCount * kWaveSize];
groupshared uint SharedDepthDomain[kReadCount * kWaveSize];

void ComputeWavefrontExtents(
    DispatchRecord record,
    uint3 groupID,
    uint groupThreadID,
    out float2 deltaXY,
    out float2 pixelXY,
    out float pixelDistance,
    out bool majorAxisX)
{
    int2 xy = int2(groupID.yz) * (int)kWaveSize + record.waveOffset;
    const float2 lightPixel = floor(record.lightCoordinate.xy) + 0.5f;
    float2 lightFraction = record.lightCoordinate.xy - lightPixel;
    const bool reverseDirection = record.lightCoordinate.w > 0.0f;

    const int2 signXY = sign(xy);
    const bool horizontal =
        abs(xy.x + signXY.y) < abs(xy.y - signXY.x);
    const int2 axis = horizontal ?
        int2(signXY.y, 0) : int2(0, -signXY.x);
    xy = axis * (int)groupID.x + xy;

    const float2 relativePixel = (float2)xy;
    majorAxisX = abs(relativePixel.x) > abs(relativePixel.y);
    const float majorAxis = majorAxisX ?
        relativePixel.x : relativePixel.y;
    const float majorStart = abs(majorAxis);
    const float majorEnd = majorStart - (float)kWaveSize;
    float majorLightFraction = majorAxisX ?
        lightFraction.x : lightFraction.y;
    majorLightFraction = majorAxis > 0.0f ?
        -majorLightFraction : majorLightFraction;

    const float2 startXY = relativePixel + lightPixel;
    const float denominator = majorStart + majorLightFraction;
    const float2 endXY = abs(denominator) > 1.0e-5f ?
        lerp(
            record.lightCoordinate.xy,
            startXY,
            (majorEnd + majorLightFraction) / denominator) :
        record.lightCoordinate.xy;
    const float2 rayDelta = startXY - endXY;
    const uint threadStep = groupThreadID ^
        (reverseDirection ? 0u : (kWaveSize - 1u));

    pixelXY = lerp(
        startXY,
        endXY,
        (float)threadStep / (float)kWaveSize);
    pixelDistance = majorStart - (float)threadStep + majorLightFraction;
    deltaXY = rayDelta;
}

bool LoadNativeDepth(
    int2 localPixel,
    uint eye,
    uint eyeWidth,
    uint height,
    out float nativeDepth,
    out uint depthDomain)
{
    if (localPixel.x < 0 || localPixel.x >= (int)eyeWidth ||
        localPixel.y < 0 || localPixel.y >= (int)height) {
        nativeDepth = kFarDepthValue;
        depthDomain = kInvalidDepthDomain;
        return false;
    }

    const int2 packedPixel = int2(
        localPixel.x + (int)(eye * eyeWidth),
        localPixel.y);
    const float rawDepth = SceneDepth.Load(int3(packedPixel, 0));
    if (rawDepth <= 1.0e-6f) {
        nativeDepth = kFarDepthValue;
        depthDomain = kInvalidDepthDomain;
        return false;
    }

    const bool compressedNearDepth = rawDepth <= 0.01f;
    nativeDepth = compressedNearDepth ?
        rawDepth * 100.0f : rawDepth * 1.01f - 0.01f;
    depthDomain = compressedNearDepth ? 0u : 1u;
    return true;
}

[numthreads(64, 1, 1)]
void CSMain(
    uint3 groupID : SV_GroupID,
    uint groupThreadID : SV_GroupThreadID)
{
    const uint dispatchIndex = (uint)round(ContactParams2.w);
    const DispatchRecord record = DispatchRecords[dispatchIndex];
    uint width;
    uint height;
    SceneDepth.GetDimensions(width, height);
    const uint eyeWidth = width / 2u;

    float2 rayDelta;
    float2 pixelXY;
    float pixelDistance;
    bool majorAxisX;
    ComputeWavefrontExtents(
        record,
        groupID,
        groupThreadID,
        rayDelta,
        pixelXY,
        pixelDistance,
        majorAxisX);

    float samplingDepth[kReadCount];
    float shadowingDepth[kReadCount];
    float depthThicknessScale[kReadCount];
    float sampleDistance[kReadCount];
    uint sampleDomain[kReadCount];
    bool sampleValid[kReadCount];
    const float direction = -record.lightCoordinate.w;
    const float depthSign =
        kNearDepthValue > kFarDepthValue ? -1.0f : 1.0f;
    const int2 writePixel = (int2)floor(pixelXY);

    [unroll]
    for (uint readIndex = 0u; readIndex < kReadCount; ++readIndex) {
        const int2 basePixel = (int2)floor(pixelXY);
        const float minorCoordinate = majorAxisX ? pixelXY.y : pixelXY.x;
        const float bilinear = frac(minorCoordinate) - 0.5f;
        const int bias = bilinear > 0.0f ? 1 : -1;
        const int2 neighborOffset = majorAxisX ?
            int2(0, bias) : int2(bias, 0);

        float baseDepth;
        float neighborDepth;
        uint baseDomain;
        uint neighborDomain;
        const bool baseValid = LoadNativeDepth(
            basePixel,
            record.eye,
            eyeWidth,
            height,
            baseDepth,
            baseDomain);
        const bool neighborValid = LoadNativeDepth(
            basePixel + neighborOffset,
            record.eye,
            eyeWidth,
            height,
            neighborDepth,
            neighborDomain);

        sampleValid[readIndex] = baseValid;
        sampleDomain[readIndex] = baseDomain;
        samplingDepth[readIndex] = baseDepth;
        depthThicknessScale[readIndex] = max(
            abs(kFarDepthValue - baseDepth),
            1.0e-4f);

        const bool usePointFilter = !neighborValid ||
            neighborDomain != baseDomain ||
            abs(baseDepth - neighborDepth) >
                depthThicknessScale[readIndex] * 0.02f;
        shadowingDepth[readIndex] = usePointFilter ?
            baseDepth : baseDepth + abs(baseDepth - neighborDepth) * depthSign;
        sampleDistance[readIndex] =
            pixelDistance + (float)(kWaveSize * readIndex) * direction;
        pixelXY += rayDelta * direction;
    }

    [unroll]
    for (uint storeIndex = 0u; storeIndex < kReadCount; ++storeIndex) {
        float storedDepth = 1.0e10f;
        if (sampleValid[storeIndex] &&
            (storeIndex == 0u || sampleDistance[storeIndex] > 0.0f) &&
            abs(sampleDistance[storeIndex]) > 1.0e-5f) {
            storedDepth =
                (shadowingDepth[storeIndex] -
                    record.lightCoordinate.z) /
                sampleDistance[storeIndex];
        }
        const uint sharedIndex =
            storeIndex * kWaveSize + groupThreadID;
        SharedDepth[sharedIndex] = storedDepth;
        SharedDepthDomain[sharedIndex] = sampleValid[storeIndex] ?
            sampleDomain[storeIndex] : kInvalidDepthDomain;
    }

    GroupMemoryBarrierWithGroupSync();

    if (record.active == 0u || writePixel.x < 0 ||
        writePixel.x >= (int)eyeWidth || writePixel.y < 0 ||
        writePixel.y >= (int)height || !sampleValid[0]) {
        return;
    }

    float startDepth = samplingDepth[0];
    if (abs(pixelDistance) <= 1.0e-5f) {
        return;
    }
    startDepth =
        (startDepth - record.lightCoordinate.z) / pixelDistance;

    const float surfaceThickness = max(ContactParams0.z, 1.0e-4f);
    const float depthScale = min(
        pixelDistance + direction,
        1.0f / surfaceThickness) *
        pixelDistance / depthThicknessScale[0];
    startDepth = startDepth * depthScale - depthSign;

    uint activeSampleCount = min(
        (uint)round(clamp(ContactParams0.y, 8.0f, 256.0f)),
        (uint)round(clamp(ContactParams0.w, 2.0f, 16.0f)) * 16u);
    if (ContactParams1.w > 0.5f) {
        const float2 eyeUv =
            ((float2)writePixel + 0.5f) /
            float2(max(eyeWidth, 1u), max(height, 1u));
        const float2 radial = (eyeUv - 0.5f) * float2(1.0f, 0.78f);
        const float outer = smoothstep(0.30f, 0.62f, length(radial));
        activeSampleCount = max(
            32u,
            (uint)round((float)activeSampleCount * lerp(
                1.0f,
                ContactParams1.z,
                outer)));
    }

    const uint firstSharedSample = groupThreadID + 1u;
    float4 shadowValue = 1.0f;
    [loop]
    for (uint sampleIndex = 0u;
         sampleIndex < kMaximumSampleCount;
         ++sampleIndex) {
        if (sampleIndex >= activeSampleCount) {
            break;
        }
        const uint sharedIndex = firstSharedSample + sampleIndex;
        if (SharedDepthDomain[sharedIndex] != sampleDomain[0]) {
            continue;
        }
        const float depthDelta = abs(
            startDepth - SharedDepth[sharedIndex] * depthScale);
        const uint lane = sampleIndex & 3u;
        if (lane == 0u) {
            shadowValue.x = min(shadowValue.x, depthDelta);
        } else if (lane == 1u) {
            shadowValue.y = min(shadowValue.y, depthDelta);
        } else if (lane == 2u) {
            shadowValue.z = min(shadowValue.z, depthDelta);
        } else {
            shadowValue.w = min(shadowValue.w, depthDelta);
        }
    }

    // Open Shaders uses the Bend VR reference contrast of four, then averages
    // four interleaved minima to suppress one-pixel depth noise without a
    // detached screen-space dilation pass.
    shadowValue = saturate(shadowValue * 4.0f - 3.0f);
    const float visibility = dot(shadowValue, 0.25f);
    const uint2 packedWritePixel = uint2(
        (uint)writePixel.x + record.eye * eyeWidth,
        (uint)writePixel.y);
    ContactShadowMask[packedWritePixel] = visibility;
}
