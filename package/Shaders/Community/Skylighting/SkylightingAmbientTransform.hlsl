Texture2D<float4> NativeNormal : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
Texture2D<float4> NativeDepth : register(t3);
Texture3D<float4> NearSkylightingProbeArray : register(t50);
Texture3D<float4> FarSkylightingProbeArray : register(t51);
RWByteAddressBuffer SkylightingAmbientDiagnostic : register(u7);
SamplerState NativeDepthSampler : register(s3);

struct ProbeLevelSettings
{
    float4 ArraySize;
    float4 CellSize;
    float4 PositionOffset;
    uint4 ArrayDimensions;
    uint4 ArrayOrigin;
    // Update-only metadata. Retained here to keep the shared b13 layout exact.
    int4 UpdateRegion;
};

cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

cbuffer SkylightingSettings : register(b13)
{
    column_major float4x4 OcclusionViewProjection;
    float4 OcclusionDirection;
    ProbeLevelSettings NearLevel;
    ProbeLevelSettings FarLevel;
    // x=level (0 near, 1 far), yzw=logical update origin.
    uint4 UpdateControl;
    // x=minimum diffuse visibility, y=minimum specular visibility,
    // z=feature active, w=reserved.
    float4 Response;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    nointerpolation uint Eye : EYEINDEX;
};

struct PixelOutput
{
    float4 DiffuseVisibility : SV_Target0;
    float4 SpecularVisibility : SV_Target1;
};

static const float kPi = 3.14159265358979323846f;

float4 NativeClip(float2 screenUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = screenUv.x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    return float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) *
            (Stereo[0].x + 1.0f),
        1.0f - screenUv.y * 2.0f,
        nativeDepth,
        1.0f);
}

float3 ReconstructRelativeWorldPosition(
    float2 screenUv,
    float depth,
    uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(screenUv, depth, eye);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    return homogeneous.xyz / max(abs(homogeneous.w), 1.0e-7f);
}

float3 DecodeNativeNormal(float2 encoded)
{
    const float2 expanded = encoded * 4.0f - 2.0f;
    const float lengthSquared = dot(expanded, expanded);
    return normalize(float3(
        expanded * sqrt(max(1.0f - lengthSquared * 0.25f, 0.0f)),
        -(1.0f - lengthSquared * 0.5f)));
}

float4 CosineLobe(float3 direction)
{
    return float4(
        0.88622692545275801370f,
        -1.02332670794648848850f * direction.y,
        1.02332670794648848850f * direction.z,
        -1.02332670794648848850f * direction.x);
}

float4 DirectionalLobe(float3 direction)
{
    return float4(
        0.28209479177387814347f,
        -0.48860251190291992159f * direction.y,
        0.48860251190291992159f * direction.z,
        -0.48860251190291992159f * direction.x);
}

float4 FauxSpecularLobe(
    float3 normal,
    float3 viewDirection,
    float roughness)
{
    const float factor =
        (1.0f - roughness) *
        (sqrt(max(1.0f - roughness, 0.0f)) + roughness);
    const float3 reflection = reflect(-viewDirection, normal);
    const float3 dominant = normalize(lerp(normal, reflection, factor));
    const float roughnessSquared = roughness * roughness;
    const float halfAngle = clamp(
        4.1679f * roughnessSquared * roughnessSquared -
            9.0127f * roughnessSquared * roughness +
            4.6161f * roughnessSquared +
            1.7048f * roughness +
            0.1f,
        0.0f,
        0.5f * kPi);
    const float broadness = halfAngle / (0.5f * kPi);
    return lerp(
        CosineLobe(dominant) / kPi,
        DirectionalLobe(dominant),
        broadness);
}

float LevelExtent(
    float3 relativePosition,
    ProbeLevelSettings level)
{
    const float3 normalized = abs(
        (relativePosition - level.PositionOffset.xyz) /
        max(level.ArraySize.xyz * 0.5f, 1.0f));
    return max(length(normalized.xy), normalized.z);
}

bool SampleProbeLevel(
    uint levelIndex,
    float3 relativeWorldPosition,
    float3 normal,
    out float4 visibilitySh,
    out bool insideVolume,
    out bool weightedSample)
{
    visibilitySh = float4(
        3.54490770181103205460f,
        0.0f,
        0.0f,
        0.0f);
    insideVolume = false;
    weightedSample = false;
    ProbeLevelSettings level;
    if (levelIndex == 0u) {
        level = NearLevel;
    } else {
        level = FarLevel;
    }
    if (Response.z <= 0.5f ||
        any(level.ArrayDimensions.xyz == 0u)) {
        return false;
    }

    relativeWorldPosition += normal * level.CellSize.xyz * 0.5f;
    const float3 adjusted =
        relativeWorldPosition - level.PositionOffset.xyz;
    const float3 uvw = adjusted / level.ArraySize.xyz + 0.5f;
    if (any(uvw < 0.0f) || any(uvw > 1.0f)) {
        return false;
    }
    insideVolume = true;

    const int3 dimensions = int3(level.ArrayDimensions.xyz);
    const float3 cellCoordinates = uvw * float3(dimensions);
    const int3 cell000 = int3(floor(cellCoordinates - 0.5f));
    const float3 trilinearPosition =
        cellCoordinates - 0.5f - float3(cell000);
    float4 sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int x = 0; x < 2; ++x) {
        [unroll]
        for (int y = 0; y < 2; ++y) {
            [unroll]
            for (int z = 0; z < 2; ++z) {
                const int3 offset = int3(x, y, z);
                const int3 logicalCell = cell000 + offset;
                if (any(logicalCell < 0) || any(logicalCell >= dimensions)) {
                    continue;
                }
                const float3 axisWeights =
                    1.0f - abs(float3(offset) - trilinearPosition);
                const float trilinearWeight =
                    axisWeights.x * axisWeights.y * axisWeights.z;
                const float3 cellCentre =
                    (float3(logicalCell) + 0.5f -
                        float3(dimensions) * 0.5f) *
                        level.CellSize.xyz +
                    level.PositionOffset.xyz;
                const float3 towardCell = cellCentre - relativeWorldPosition;
                const float distanceSquared = dot(towardCell, towardCell);
                const float tangentWeight = distanceSquared > 1.0e-8f ?
                    dot(towardCell * rsqrt(distanceSquared), normal) *
                            0.5f +
                        0.5f :
                    1.0f;
                const float weight = trilinearWeight * tangentWeight;
                const int3 physicalCell = int3(
                    (uint3(logicalCell) + level.ArrayOrigin.xyz) %
                    level.ArrayDimensions.xyz);
                [branch]
                if (levelIndex == 0u) {
                    sum += NearSkylightingProbeArray.Load(
                        int4(physicalCell, 0)) * weight;
                } else {
                    sum += FarSkylightingProbeArray.Load(
                        int4(physicalCell, 0)) * weight;
                }
                weightSum += weight;
            }
        }
    }
    if (weightSum <= 1.0e-6f) {
        return false;
    }
    weightedSample = true;
    visibilitySh = sum / weightSum;
    return true;
}

bool SampleSkylighting(
    float3 relativeWorldPosition,
    float3 normal,
    out float4 visibilitySh,
    out float fade,
    out bool insideVolume,
    out bool weightedSample)
{
    const float4 unitVisibility = float4(
        3.54490770181103205460f,
        0.0f,
        0.0f,
        0.0f);
    visibilitySh = unitVisibility;
    fade = 0.0f;
    insideVolume = false;
    weightedSample = false;

    const float nearExtent = LevelExtent(
        relativeWorldPosition,
        NearLevel);
    float nearWeight = 1.0f - smoothstep(
        0.55f,
        0.90f,
        nearExtent);
    const float farExtent = LevelExtent(
        relativeWorldPosition,
        FarLevel);
    const float farFade = 1.0f - smoothstep(
        0.75f,
        0.98f,
        farExtent);

    float4 nearVisibility = unitVisibility;
    bool nearInside = false;
    bool nearWeighted = false;
    const bool nearSampled = nearWeight > 0.0f && SampleProbeLevel(
        0u,
        relativeWorldPosition,
        normal,
        nearVisibility,
        nearInside,
        nearWeighted);
    if (!nearSampled) {
        nearWeight = 0.0f;
    }

    float4 farVisibility = unitVisibility;
    bool farInside = false;
    bool farWeighted = false;
    const bool farSampled = nearWeight < 0.999f && farFade > 0.0f &&
        SampleProbeLevel(
            1u,
            relativeWorldPosition,
            normal,
            farVisibility,
            farInside,
            farWeighted);

    insideVolume = nearInside || farInside;
    weightedSample = nearWeighted || farWeighted;
    if (!nearSampled && !farSampled) {
        return false;
    }
    if (nearWeight >= 0.999f && nearSampled) {
        visibilitySh = nearVisibility;
        fade = 1.0f;
        return true;
    }

    visibilitySh = lerp(farVisibility, nearVisibility, nearWeight);
    fade = farFade;
    return true;
}

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    output.DiffuseVisibility = 1.0f;
    output.SpecularVisibility = 1.0f;
    const uint2 diagnosticPixel = uint2(input.Position.xy);
    const bool diagnosticSample = Response.w > 0.5f &&
        all((diagnosticPixel & uint2(255u, 255u)) ==
            uint2(128u, 128u));
    uint ignored;
    if (diagnosticSample) {
        SkylightingAmbientDiagnostic.InterlockedAdd(0u, 1u, ignored);
    }
    if (Response.z > 0.5f) {
        uint depthWidth;
        uint depthHeight;
        NativeDepth.GetDimensions(depthWidth, depthHeight);
        const float2 screenUv = input.Position.xy /
            float2(depthWidth, depthHeight);
        const int2 depthPixel = clamp(
            int2(input.Position.xy),
            int2(0, 0),
            int2((int)depthWidth - 1, (int)depthHeight - 1));
        const float4 sampledDepth = NativeDepth.SampleLevel(
            NativeDepthSampler,
            screenUv,
            0.0f);
        const float4 loadedDepth = NativeDepth.Load(
            int3(depthPixel, 0));
        if (diagnosticSample) {
            SkylightingAmbientDiagnostic.InterlockedMin(
                56u, asuint(sampledDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                60u, asuint(sampledDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                64u, asuint(sampledDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                68u, asuint(sampledDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                72u, asuint(sampledDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                76u, asuint(sampledDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                80u, asuint(sampledDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                84u, asuint(sampledDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                88u, asuint(screenUv.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                92u, asuint(screenUv.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                96u, asuint(screenUv.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                100u, asuint(screenUv.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                104u, asuint(loadedDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                108u, asuint(loadedDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                112u, asuint(loadedDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                116u, asuint(loadedDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                120u, asuint(loadedDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                124u, asuint(loadedDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                128u, asuint(loadedDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                132u, asuint(loadedDepth.w), ignored);
        }
        const float depth = loadedDepth.x;
        if (depth > 1.0e-6f) {
            if (diagnosticSample) {
                SkylightingAmbientDiagnostic.InterlockedAdd(
                    4u, 1u, ignored);
            }
            const float3 relativeWorldPosition =
                ReconstructRelativeWorldPosition(
                    screenUv,
                    depth,
                    input.Eye);
            if (all(abs(relativeWorldPosition) <= 1.0e8f)) {
                if (diagnosticSample) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        8u, 1u, ignored);
                }
                uint normalWidth;
                uint normalHeight;
                NativeNormal.GetDimensions(normalWidth, normalHeight);
                const int2 normalPixel = clamp(
                    int2(screenUv * float2(normalWidth, normalHeight)),
                    int2(0, 0),
                    int2((int)normalWidth - 1, (int)normalHeight - 1));
                const float3 normal = DecodeNativeNormal(
                    NativeNormal.Load(int3(normalPixel, 0)).xy);
                float4 visibilitySh;
                float fade;
                bool insideVolume;
                bool weightedSample;
                const bool sampled = SampleSkylighting(
                    relativeWorldPosition,
                    normal,
                    visibilitySh,
                    fade,
                    insideVolume,
                    weightedSample);
                if (diagnosticSample && insideVolume) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        12u, 1u, ignored);
                }
                if (diagnosticSample && weightedSample) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        16u, 1u, ignored);
                }
                if (sampled) {
                    const float diffuseVisibility = lerp(
                        1.0f,
                        saturate(dot(
                            visibilitySh,
                            CosineLobe(normal)) / kPi),
                        fade);
                    const float3 viewDirection = normalize(
                        -relativeWorldPosition);
                    uint materialWidth;
                    uint materialHeight;
                    NativeMaterial.GetDimensions(
                        materialWidth,
                        materialHeight);
                    const int2 materialPixel = clamp(
                        int2(screenUv *
                            float2(materialWidth, materialHeight)),
                        int2(0, 0),
                        int2(
                            (int)materialWidth - 1,
                            (int)materialHeight - 1));
                    const float nativeGloss = saturate(
                        NativeMaterial.Load(int3(materialPixel, 0)).x);
                    const float roughness = saturate(1.0f - nativeGloss);
                    const float specularVisibility = lerp(
                        1.0f,
                        saturate(dot(
                            visibilitySh,
                            FauxSpecularLobe(
                                normal,
                                viewDirection,
                                roughness))),
                        fade);
                    output.DiffuseVisibility = lerp(
                        Response.x,
                        1.0f,
                        diffuseVisibility);
                    output.SpecularVisibility = lerp(
                        Response.y,
                        1.0f,
                        specularVisibility);
                    if (diagnosticSample) {
                        if (fade > 1.0e-3f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                20u, 1u, ignored);
                        }
                        if (output.DiffuseVisibility.x < 0.999f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                24u, 1u, ignored);
                        }
                        if (output.SpecularVisibility.x < 0.999f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                28u, 1u, ignored);
                        }
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            32u,
                            asuint(output.DiffuseVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            36u,
                            asuint(output.DiffuseVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            40u,
                            asuint(output.SpecularVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            44u,
                            asuint(output.SpecularVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            48u,
                            asuint(fade),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            52u,
                            asuint(fade),
                            ignored);
                    }
                }
            }
        }
    }
    return output;
}
