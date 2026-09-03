Texture3D<float2> RawVolume : register(t0);
RWTexture3D<float2> IntegratedVolume : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    uint depth;
    IntegratedVolume.GetDimensions(width, height, depth);
    if (dispatchId.x >= width || dispatchId.y >= height) {
        return;
    }
    float2 accumulated = 0.0f.xx;
    [loop]
    for (uint slice = 0u; slice < depth; ++slice) {
        const uint3 coordinate = uint3(dispatchId.xy, slice);
        accumulated += max(RawVolume.Load(int4(coordinate, 0)), 0.0f.xx);
        accumulated.x = min(accumulated.x, accumulated.y);
        IntegratedVolume[coordinate] = accumulated;
    }
}
