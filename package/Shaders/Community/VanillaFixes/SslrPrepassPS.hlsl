// FO4VR ImageSpace[130] / BSImagespaceShaderSSLRPrepass replacement.
// Reconstruct the receiver and its geometric macro normal in one eye-local
// view space. Store the reflected direction in a stable world basis. The
// matching raytrace converts it back to the selected eye only for current
// depth marching, so HMD rotation cannot change the persistent lookup key.

cbuffer SslrParameters : register(b0)
{
    float4 SslrParams[3];
};

cbuffer CameraParameters : register(b12)
{
    float4 CameraData[48];
};

Texture2D<float4> DepthTexture : register(t0);
Texture2D<float4> NormalTexture : register(t1);
Texture2D<float4> MaterialMaskTexture : register(t2);
SamplerState DepthSampler : register(s0);
SamplerState NormalSampler : register(s1);
SamplerState MaterialMaskSampler : register(s2);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelOutput
{
    float2 rayDirection : SV_Target0;
    float4 viewDepth : SV_Target1;
};

float4 multiplyRows(uint firstRow, float4 value)
{
    return float4(
        dot(CameraData[firstRow + 0], value),
        dot(CameraData[firstRow + 1], value),
        dot(CameraData[firstRow + 2], value),
        dot(CameraData[firstRow + 3], value));
}

float3 transformRows(uint firstRow, float3 value)
{
    return float3(
        dot(CameraData[firstRow + 0].xyz, value),
        dot(CameraData[firstRow + 1].xyz, value),
        dot(CameraData[firstRow + 2].xyz, value));
}

float3 decodeNormal(float2 encoded)
{
    float2 xy = encoded * 4.0f - 2.0f;
    const float lengthSquared = dot(xy, xy);
    xy *= sqrt(1.0f - lengthSquared * 0.25f);
    return normalize(float3(xy, -(1.0f - lengthSquared * 0.5f)));
}

float2 encodeDirection(float3 direction)
{
    direction /= max(
        abs(direction.x) + abs(direction.y) + abs(direction.z),
        1.0e-6f);
    float2 encoded = direction.xy;
    if (direction.z < 0.0f)
    {
        const float2 signs = float2(
            encoded.x >= 0.0f ? 1.0f : -1.0f,
            encoded.y >= 0.0f ? 1.0f : -1.0f);
        encoded = (1.0f - abs(encoded.yx)) * signs;
    }
    return encoded;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output = (PixelOutput)0;

    const float materialMask =
        MaterialMaskTexture.Sample(MaterialMaskSampler, input.uv).x;
    clip(materialMask * SslrParams[2].z - 0.01f);

    const float depth = DepthTexture.SampleLevel(
        DepthSampler,
        input.uv,
        0.0f).x;
    const bool rightEye = input.uv.x >= 0.5f;
    const uint eyeMatrixOffset = rightEye ? 4u : 0u;
    const float eyeLocalX =
        (input.uv.x - (rightEye ? 0.5f : 0.0f)) * 2.0f;

    float viewZ;
    float4 viewPositionH;
    const bool lowDepth = depth <= 0.01f;
    const float4 clipPosition = float4(
        eyeLocalX * 2.0f - 1.0f,
        (1.0f - input.uv.y) * 2.0f - 1.0f,
        lowDepth ? depth * 100.0f : depth * 1.01f - 0.01f,
        1.0f);
    const float4 rayClipPosition = float4(
        clipPosition.xy,
        0.5f,
        1.0f);
    if (lowDepth)
    {
        viewPositionH = multiplyRows(
            eyeMatrixOffset + 40u,
            clipPosition);
    }
    else
    {
        viewPositionH = multiplyRows(
            eyeMatrixOffset + 32u,
            clipPosition);
    }
    const float3 viewPosition = viewPositionH.xyz / viewPositionH.w;
    viewZ = viewPosition.z;

    // Receiver distance and incident direction have separate contracts.
    // The fixed clip plane removes the compressed depth path from the
    // angular key while preserving native receiver distance.
    const float4 eyeRayH = multiplyRows(
        eyeMatrixOffset + 32u,
        rayClipPosition);
    const float3 incidentView = normalize(eyeRayH.xyz / eyeRayH.w);

    float3 normal = decodeNormal(
        NormalTexture.Sample(NormalSampler, input.uv).xy);
    const float3 geometricCandidate = cross(
        ddx(viewPosition),
        ddy(viewPosition));
    const float geometricLengthSquared = dot(
        geometricCandidate,
        geometricCandidate);
    if (all(isfinite(geometricCandidate)) &&
        isfinite(geometricLengthSquared) &&
        geometricLengthSquared > 1.0e-8f)
    {
        // Macro geometry owns traversal. Material normal detail remains in
        // ordinary lighting but cannot bend the screen-space intersection.
        normal = geometricCandidate * rsqrt(geometricLengthSquared);
    }

    // Reflection is invariant to normal sign. The stock-facing predicate
    // formed a camera-centred acceptance circle across the wide VR image and
    // is intentionally absent.
    float3 worldNormal = transformRows(20u, normal);
    worldNormal.z *= SslrParams[2].x;
    worldNormal = normalize(worldNormal);
    const float3 incidentWorld = normalize(transformRows(
        20u,
        incidentView));
    const float3 reflectedWorld = normalize(reflect(
        incidentWorld,
        worldNormal));
    if (all(isfinite(reflectedWorld)))
    {
        output.rayDirection = encodeDirection(reflectedWorld);
        output.viewDepth.x = viewZ;
    }
    return output;
}
