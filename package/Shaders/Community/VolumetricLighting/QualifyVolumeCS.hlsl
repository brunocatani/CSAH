cbuffer ImageSpaceConstants : register(b0)
{
    float4 Reserved0;
    float4 TexelSize;
    float4 CameraParams;
    float4 SunParams;
    float4 GlareColor;
};

Texture2D<float> StructuredGlare : register(t0);
Texture2D<float> FilteredGlare : register(t1);
Texture2D<float> ReceiverDepth : register(t2);
RWStructuredBuffer<float4> ProbeOutput : register(u0);

[numthreads(8, 8, 1)]
void main(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    StructuredGlare.GetDimensions(width, height);
    const uint eye = min(groupId.x, 1u);
    const uint eyeWidth = max(width >> 1u, 1u);
    const uint x = min(
        eye * eyeWidth +
            ((groupThreadId.x * 2u + 1u) * eyeWidth) / 16u,
        (eye + 1u) * eyeWidth - 1u);
    const uint y = min(
        ((groupThreadId.y * 2u + 1u) * height) / 16u,
        height - 1u);
    const uint record = eye * 64u +
        groupThreadId.y * 8u + groupThreadId.x;
    const float structured = StructuredGlare.Load(int3(x, y, 0));
    const float filtered = FilteredGlare.Load(int3(x, y, 0));
    const float depth = ReceiverDepth.Load(int3(x, y, 0));
    ProbeOutput[record] = float4(
        structured,
        filtered,
        depth,
        max(structured - filtered - 0.01f, 0.0f));
    if (record == 0u) {
        ProbeOutput[128u] = float4(
            SunParams.z,
            GlareColor.r,
            GlareColor.g,
            GlareColor.b);
    }
}
