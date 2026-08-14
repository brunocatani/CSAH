Texture2D<float> SurfaceClass : register(t47);

cbuffer WrappedGrassSettings : register(b11)
{
    // x=enabled, y=wrap amount, z=grass code, w=code tolerance.
    float4 WrappedGrassParams;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    float VanillaNdotL : TEXCOORD0;
};

float PSMain(PixelInput input) : SV_Target0
{
    const float surfaceClass = SurfaceClass.Load(
        int3(int2(input.Position.xy), 0));
    const float isGrass =
        abs(surfaceClass - WrappedGrassParams.z) <= WrappedGrassParams.w;
    const float wrapAmount = saturate(WrappedGrassParams.y);
    const float wrappedNdotL = saturate(
        input.VanillaNdotL + wrapAmount) / (1.0f + wrapAmount);
    return lerp(
        input.VanillaNdotL,
        wrappedNdotL,
        isGrass * saturate(WrappedGrassParams.x));
}
