// FO4VR ImageSpace[130] / BSImagespaceShaderSSLRPrepass replacement.
// The stock prepass reconstructs each eye independently, but emits its ray
// endpoint in eye-local X. The retained flat raytrace shader consumes a packed
// start UV. Preserve the prepass and repack only its final endpoint X so both
// values enter the stock 32-step marcher in the same coordinate domain.

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
    float2 rayEndpoint : SV_Target0;
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
    const float eyeLocalX = (input.uv.x - (rightEye ? 0.5f : 0.0f)) * 2.0f;

    float viewZ;
    float4 viewPositionH;
    const bool lowDepth = depth <= 0.01f;
    const float4 clipPosition = float4(
        eyeLocalX * 2.0f - 1.0f,
        (1.0f - input.uv.y) * 2.0f - 1.0f,
        lowDepth ? depth * 100.0f : depth * 1.01f - 0.01f,
        1.0f);
    if (lowDepth) {
        viewPositionH = multiplyRows(eyeMatrixOffset + 40u, clipPosition);
    } else {
        viewPositionH = multiplyRows(eyeMatrixOffset + 32u, clipPosition);
    }
    const float3 viewPosition = viewPositionH.xyz / viewPositionH.w;
    viewZ = viewPosition.z;

    const float3 viewDirection = normalize(-viewPosition);
    float3 normal = decodeNormal(
        NormalTexture.Sample(NormalSampler, input.uv).xy);

    if (dot(normal, viewDirection) >= 0.0f) {
        normal = transformRows(20u, normal);
        normal.z *= SslrParams[2].x;
        normal = normalize(normal);
        normal = normalize(transformRows(0u, normal));

        const float3 reflected = reflect(-viewDirection, normal);
        if (reflected.z > SslrParams[1].y) {
            const float4 reflectedPosition =
                float4(viewPosition + reflected * 1000.0f, 1.0f);
            const float4 projectedH = multiplyRows(
                eyeMatrixOffset + 4u,
                reflectedPosition);
            const float3 projected = projectedH.w == 0.0f ?
                1.0f.xxx : projectedH.xyz / projectedH.w;
            const float3 projectedUvDepth = float3(
                projected.x * 0.5f + 0.5f,
                projected.y * -0.5f + 0.5f,
                projected.z);
            const float3 start = float3(eyeLocalX, input.uv.y, depth);
            const float3 delta = projectedUvDepth - start;
            const float2 eyeLocalEndpoint =
                start.xy - depth * (delta.xy / delta.z);

            output.rayEndpoint = float2(
                eyeLocalEndpoint.x * 0.5f + (rightEye ? 0.5f : 0.0f),
                eyeLocalEndpoint.y);
            output.viewDepth.x = viewZ;
        }
    }
    return output;
}
