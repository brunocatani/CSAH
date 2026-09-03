Texture3D<float> RawVolume : register(t0);
RWTexture3D<float> IntegratedVolume : register(u0);

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
    float accumulated = 0.0f;
    [loop]
    for (uint slice = 0u; slice < depth; ++slice) {
        const uint3 coordinate = uint3(dispatchId.xy, slice);
        accumulated += max(RawVolume.Load(int4(coordinate, 0)), 0.0f);
        IntegratedVolume[coordinate] = accumulated;
    }
}
