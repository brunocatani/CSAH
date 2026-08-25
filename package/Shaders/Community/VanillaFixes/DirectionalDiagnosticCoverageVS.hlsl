// Stereo directional diagnostic coverage shader. Exact eye rectangles avoid
// the native SV_ClipDistance/SV_CullDistance path without cross-eye overlap.
struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint eye : EYEINDEX;
};

VertexOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 kRectangle[6] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, 1.0f),
        float2(-1.0f, -1.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f),
    };

    const uint eye = instanceId & 1u;
    const float eyeCenter = eye == 0u ? -0.5f : 0.5f;
    const float2 localPosition = kRectangle[vertexId];

    VertexOutput output;
    output.position = float4(
        localPosition.x * 0.5f + eyeCenter,
        localPosition.y,
        0.0f,
        1.0f);
    output.eye = eye;
    return output;
}
