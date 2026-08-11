// FO4VR port of the local Community Shaders first-order diffuse IBL
// projection. The output stores RGB spherical-harmonic coefficients as
// three float4 texels: L0, L1(-Y), L1(+Z), L1(-X).

TextureCube<float4> NativeEnvironment : register(t0);
RWTexture2D<float4> ProjectedDiffuse : register(u0);
SamplerState LinearSampler : register(s0);

static const uint AxisSampleCount = 16;
static const uint TotalSampleCount = AxisSampleCount * AxisSampleCount;
static const float Pi = 3.14159265358979323846f;

groupshared float4 SharedRed[TotalSampleCount];
groupshared float4 SharedGreen[TotalSampleCount];
groupshared float4 SharedBlue[TotalSampleCount];

float3 UniformSphereSample(float2 sampleCoordinate)
{
    const float phi = 2.0f * Pi * sampleCoordinate.x;
    const float y = 1.0f - 2.0f * sampleCoordinate.y;
    const float radial = sqrt(max(0.0f, 1.0f - y * y));
    return float3(radial * cos(phi), y, radial * sin(phi));
}

float4 EvaluateFirstOrderSH(float3 direction)
{
    return float4(
        0.28209479177387814347f,
        -0.48860251190291992159f * direction.y,
        0.48860251190291992159f * direction.z,
        -0.48860251190291992159f * direction.x);
}

[numthreads(AxisSampleCount, AxisSampleCount, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    const float inverseAxisSampleCount = rcp((float)AxisSampleCount);
    const float2 sampleCoordinate =
        ((float2)dispatchId.xy + 0.5f) * inverseAxisSampleCount;
    const float3 rayDirection = UniformSphereSample(sampleCoordinate);
    const float3 radiance =
        NativeEnvironment.SampleLevel(LinearSampler, -rayDirection, 0).rgb;
    const float integrationWeight =
        4.0f * Pi * inverseAxisSampleCount * inverseAxisSampleCount;
    const float4 basis = EvaluateFirstOrderSH(rayDirection) * integrationWeight;

    SharedRed[groupIndex] = basis * radiance.r;
    SharedGreen[groupIndex] = basis * radiance.g;
    SharedBlue[groupIndex] = basis * radiance.b;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = TotalSampleCount / 2; stride > 0; stride >>= 1) {
        if (groupIndex < stride) {
            SharedRed[groupIndex] += SharedRed[groupIndex + stride];
            SharedGreen[groupIndex] += SharedGreen[groupIndex + stride];
            SharedBlue[groupIndex] += SharedBlue[groupIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        ProjectedDiffuse[uint2(0, 0)] = SharedRed[0];
        ProjectedDiffuse[uint2(1, 0)] = SharedGreen[0];
        ProjectedDiffuse[uint2(2, 0)] = SharedBlue[0];
    }
}
