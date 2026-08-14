Texture2D<float> SurfaceClass : register(t47);

cbuffer BasicWetnessSettings : register(b9)
{
    // x=enabled, y=wetness, z=diffuse darkening, w=specular multiplier.
    float4 BasicWetnessParams;
    // x=IBL roughness/LOD scale. Remaining values are reserved.
    float4 BasicWetnessMaterialParams;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    float3 VanillaSpecular : TEXCOORD0;
    float4 VanillaDiffuse : TEXCOORD1;
};

struct PixelOutput
{
    float4 Diffuse : SV_Target0;
    float4 Specular : SV_Target1;
};

float WettableSurface(float surfaceClass)
{
    const float code = surfaceClass * 255.0;
    const float ordinaryOrGrass = 1.0 - step(1.5, code);
    const float terrain = 1.0 - step(0.5, abs(code - 4.0));
    return saturate(ordinaryOrGrass + terrain);
}

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    const float surfaceClass = SurfaceClass.Load(
        int3(int2(input.Position.xy), 0));
    const float wetness = saturate(
        BasicWetnessParams.x * BasicWetnessParams.y *
        WettableSurface(surfaceClass));
    output.Diffuse = input.VanillaDiffuse;
    output.Diffuse.xyz *= 1.0 - saturate(BasicWetnessParams.z) * wetness;
    output.Specular = float4(
        input.VanillaSpecular * lerp(
            1.0,
            max(BasicWetnessParams.w, 0.0),
            wetness),
        0.0);
    return output;
}
