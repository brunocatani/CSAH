Texture2D<float> ContactShadowMask : register(t46);

cbuffer ContactShadowSettings : register(b13)
{
    // x=strength. The remaining values are consumed by the compute pass.
    float4 ContactParams0;
    float4 ContactParams1;
    float4 ContactParams2;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    const float rawVisibility = saturate(
        ContactShadowMask.Load(int3(int2(input.Position.xy), 0)));
    const float visibility = lerp(
        1.0f,
        rawVisibility,
        saturate(ContactParams0.x));
    return visibility.xxxx;
}
