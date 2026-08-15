cbuffer ReactiveMaskConstants : register(b0)
{
    uint2 RenderDimensions;
    uint2 SourceOrigin;
};

Texture2D<float4> TaaMask : register(t0);
RWTexture2D<unorm float> BiasCurrentColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= RenderDimensions.x ||
        dispatchThreadId.y >= RenderDimensions.y)
    {
        return;
    }

    const uint2 destination = dispatchThreadId.xy;
    const float taaHistoryMask =
        TaaMask.Load(int3(destination + SourceOrigin, 0)).r;

    // FO4VR's exact TAA shader writes its history/rejection signal to the
    // red channel of OM.RTV0. Keep the conservative Open Shaders weighting:
    // this nudges DLSS toward current color without discarding stable history.
    BiasCurrentColor[destination] = saturate(taaHistoryMask * 0.1);
}
