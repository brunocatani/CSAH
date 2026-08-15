// FO4VR ImageSpace[128] / BSImagespaceShaderBlurH replacement.
// Preserve the stock five-tap offsets and pixel shader. Only horizontal
// coordinates that would enter the other packed eye are clamped to the
// originating eye's final texel center.

cbuffer SslrBlurParameters : register(b0)
{
    float4 ViewportSize;
};

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 samples[5] : TEXCOORD0;
};

float eyeSafeX(float sampleX, float centerX, float inverseWidth)
{
    const float halfTexel = 0.5f * inverseWidth;
    return centerX >= 0.5f ?
        max(sampleX, 0.5f + halfTexel) :
        min(sampleX, 0.5f - halfTexel);
}
float2 eyeSafeSample(float2 center, float offset, float inverseWidth)
{
    float2 sampleUv = center + float2(offset * inverseWidth, 0.0f);
    sampleUv.x = eyeSafeX(sampleUv.x, center.x, inverseWidth);
    return sampleUv;
}

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);

    const float inverseWidth = rcp(ViewportSize.x);
    output.samples[0] = eyeSafeSample(input.uv, -3.294215f, inverseWidth);
    output.samples[1] = eyeSafeSample(input.uv, -1.407333f, inverseWidth);
    output.samples[2] = input.uv;
    output.samples[3] = eyeSafeSample(input.uv, 1.407333f, inverseWidth);
    output.samples[4] = eyeSafeSample(input.uv, 3.294215f, inverseWidth);
    return output;
}
