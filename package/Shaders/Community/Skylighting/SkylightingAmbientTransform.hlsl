Texture2D<float4> NativeNormal : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
Texture2D<float> NativeDepth : register(t3);
Texture3D<float4> SkylightingProbeArray : register(t50);

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

cbuffer SkylightingSettings : register(b13)
{
    column_major float4x4 OcclusionViewProjection;
    float4 OcclusionDirection;
    float4 ArraySize;
    float4 CellSize;
    float4 PositionOffset;
    uint4 ArrayDimensions;
    uint4 ArrayOrigin;
    int4 ValidMargin;
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

float EdgeFade(float3 relativePosition)
{
    const float3 uvw = saturate(
        relativePosition / ArraySize.xyz + 0.5f);
    const float3 edgeDistance = min(uvw, 1.0f - uvw);
    return saturate(
        min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) * 20.0f);
}

bool SampleSkylighting(
    float3 relativeWorldPosition,
    float3 normal,
    out float4 visibilitySh,
    out float fade)
{
    visibilitySh = float4(
        3.54490770181103205460f,
        0.0f,
        0.0f,
        0.0f);
    fade = 0.0f;
    if (Response.z <= 0.5f || any(ArrayDimensions.xyz == 0u)) {
        return false;
    }

    relativeWorldPosition += normal * CellSize.xyz * 0.5f;
    const float3 adjusted = relativeWorldPosition - PositionOffset.xyz;
    const float3 uvw = adjusted / ArraySize.xyz + 0.5f;
    if (any(uvw < 0.0f) || any(uvw > 1.0f)) {
        return false;
    }

    const int3 dimensions = int3(ArrayDimensions.xyz);
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
                        CellSize.xyz +
                    PositionOffset.xyz;
                const float3 towardCell = cellCentre - relativeWorldPosition;
                const float distanceSquared = dot(towardCell, towardCell);
                const float tangentWeight = distanceSquared > 1.0e-8f ?
                    dot(towardCell * rsqrt(distanceSquared), normal) *
                            0.5f +
                        0.5f :
                    1.0f;
                const float weight = trilinearWeight * tangentWeight;
                const int3 physicalCell = int3(
                    (uint3(logicalCell) + ArrayOrigin.xyz) %
                    ArrayDimensions.xyz);
                sum += SkylightingProbeArray.Load(
                    int4(physicalCell, 0)) * weight;
                weightSum += weight;
            }
        }
    }
    if (weightSum <= 1.0e-6f) {
        return false;
    }
    visibilitySh = sum / weightSum;
    fade = EdgeFade(relativeWorldPosition);
    return true;
}

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    output.DiffuseVisibility = 1.0f;
    output.SpecularVisibility = 1.0f;
    if (Response.z > 0.5f) {
        uint width;
        uint height;
        NativeDepth.GetDimensions(width, height);
        const int2 pixel = clamp(
            int2(input.Position.xy),
            int2(0, 0),
            int2((int)width - 1, (int)height - 1));
        const float depth = NativeDepth.Load(int3(pixel, 0));
        if (depth > 1.0e-6f) {
            const float2 packedUv =
                (float2(pixel) + 0.5f) / float2(width, height);
            const float3 relativeWorldPosition =
                ReconstructRelativeWorldPosition(
                    packedUv,
                    depth,
                    input.Eye);
            if (all(abs(relativeWorldPosition) <= 1.0e8f)) {
                const float3 normal = DecodeNativeNormal(
                    NativeNormal.Load(int3(pixel, 0)).xy);
                float4 visibilitySh;
                float fade;
                if (SampleSkylighting(
                        relativeWorldPosition,
                        normal,
                        visibilitySh,
                        fade)) {
                    const float diffuseVisibility = lerp(
                        1.0f,
                        saturate(dot(
                            visibilitySh,
                            CosineLobe(normal)) / kPi),
                        fade);
                    const float3 viewDirection = normalize(
                        -relativeWorldPosition);
                    const float nativeGloss = saturate(
                        NativeMaterial.Load(int3(pixel, 0)).x);
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
                }
            }
        }
    }
    return output;
}
