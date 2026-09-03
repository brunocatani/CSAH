struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.position = float4(
        output.uv.x * 2.0f - 1.0f,
        1.0f - output.uv.y * 2.0f,
        0.0f,
        1.0f);
    return output;
}
