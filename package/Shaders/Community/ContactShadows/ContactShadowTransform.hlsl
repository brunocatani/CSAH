Texture2D<float> SceneDepth : register(t3);
SamplerState PointClamp : register(s0);

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
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    nointerpolation uint Eye : EYEINDEX;
};

float3 ReconstructViewPosition(float2 packedUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = packedUv.x / DFLight[45].x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    const float nativeClipX =
        (baseClipX + eyeClipOffset * Stereo[0].x) *
        (Stereo[0].x + 1.0f);
    const float4 clip = float4(
        nativeClipX,
        1.0f - packedUv.y / DFLight[45].y * 2.0f,
        nativeDepth,
        1.0f);
    const uint row = eye * 4u + (compressedNearDepth ? 40u : 32u);
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

float4 PSMain(PixelInput input) : SV_Target0
{
    const uint eye = min(input.Eye, 1u);
    const float2 packedUv =
        input.Position.xy * DFLight[45].xy * DFLight[0].xy;
    const float centerDepth = SceneDepth.SampleLevel(PointClamp, packedUv, 0.0f);
    float visibility = 1.0f;
    if (centerDepth > 1.0e-6f && ContactParams0.x > 0.0f) {
        const float3 surface = ReconstructViewPosition(
            packedUv,
            centerDepth,
            eye);
        const float3 lightDirection = normalize(DFLight[eye + 1u].xyz);
        const float2 eyeUv = float2(frac(packedUv.x * 2.0f), packedUv.y);
        const float distanceScale = saturate(
            1.0f - length(surface) / max(ContactParams2.x, 1.0f));
        uint sampleCount = (uint)round(
            clamp(ContactParams0.w, 2.0f, 16.0f) * distanceScale);
        if (ContactParams1.w > 0.5f && sampleCount > 0u) {
            const float2 radial = (eyeUv - 0.5f) * float2(1.0f, 0.78f);
            const float outer = smoothstep(0.30f, 0.62f, length(radial));
            const float scaled = lerp(
                (float)sampleCount,
                max(1.0f, (float)sampleCount * ContactParams1.z),
                outer);
            sampleCount = (uint)scaled;
        }

        float occlusion = 0.0f;
        [loop]
        for (uint index = 0u; index < 16u; ++index) {
            if (index >= sampleCount) {
                break;
            }
            // A fixed sequence avoids view-dependent jitter disagreement between eyes.
            const float step = ((float)index + 0.5f) / (float)sampleCount;
            const float rayDistance = ContactParams0.y *
                (0.18f * step + 0.82f * step * step);
            const float3 rayPoint = surface + lightDirection * rayDistance;
            const float4 projected = ProjectViewPosition(rayPoint, eye);
            if (projected.w <= 1.0e-5f) {
                break;
            }

            const float2 ndc = projected.xy / projected.w;
            const float2 sampleEyeUv = float2(
                ndc.x * 0.5f + 0.5f,
                0.5f - ndc.y * 0.5f);
            // Reject before side-by-side packing so a ray cannot sample the other eye.
            if (any(sampleEyeUv <= 0.0f) || any(sampleEyeUv >= 1.0f)) {
                break;
            }
            const float2 samplePackedUv = float2(
                (sampleEyeUv.x + (float)eye) * 0.5f,
                sampleEyeUv.y);
            const float sampleDepth = SceneDepth.SampleLevel(
                PointClamp,
                samplePackedUv,
                0.0f);
            if (sampleDepth <= 1.0e-6f) {
                continue;
            }

            const float3 samplePosition = ReconstructViewPosition(
                samplePackedUv,
                sampleDepth,
                eye);
            const float candidateDistance = length(rayPoint);
            const float sampledDistance = length(samplePosition);
            const float separation = candidateDistance - sampledDistance;
            const float thickness = max(
                ContactParams1.x,
                candidateDistance * ContactParams0.z);
            const float hit = separation > ContactParams1.y &&
                    separation < thickness ?
                1.0f - separation / thickness : 0.0f;
            occlusion = max(occlusion, hit);
        }
        visibility = 1.0f - saturate(ContactParams0.x) * occlusion;
    }
    return visibility.xxxx;
}
