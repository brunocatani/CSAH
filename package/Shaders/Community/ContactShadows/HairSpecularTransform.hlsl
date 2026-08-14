Texture2D<float4> GBufferMaterial : register(t0);
Texture2D<float> SurfaceClass : register(t47);

cbuffer HairSpecularSettings : register(b10)
{
    // x=enabled, y=specular multiplier, z=hair code, w=code tolerance.
    float4 HairSpecularParams;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    float3 VanillaHairSpecular : TEXCOORD0;
};

float3 PSMain(PixelInput input) : SV_Target0
{
    const int2 pixel = int2(input.Position.xy);
    const float surfaceClass = SurfaceClass.Load(int3(pixel, 0));
    const float isHair =
        abs(surfaceClass - HairSpecularParams.z) <= HairSpecularParams.w;
    const float active = isHair * saturate(HairSpecularParams.x);

    // FO4VR's directional DFLight already evaluates its tangent-space hair
    // branch before this transform. Preserve that verified anisotropic lobe,
    // then reshape its weak response and add a broad, albedo-tinted secondary
    // lobe. The square-root term lifts small strand highlights without
    // clipping the HDR peak or illuminating hair where the native lobe is
    // absent.
    const float3 vanilla = max(input.VanillaHairSpecular, 0.0f);
    const float multiplier = max(HairSpecularParams.y, 0.0f);
    const float lobeEnergy = dot(
        vanilla, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 albedoTint = sqrt(max(
        saturate(GBufferMaterial.Load(int3(pixel, 0)).rgb), 0.02f));
    const float secondaryStrength =
        0.14f * max(multiplier - 1.0f, 0.0f);
    const float3 enhanced = vanilla * multiplier +
        albedoTint * sqrt(max(lobeEnergy, 0.0f)) * secondaryStrength;
    return lerp(input.VanillaHairSpecular, enhanced, active);
}
