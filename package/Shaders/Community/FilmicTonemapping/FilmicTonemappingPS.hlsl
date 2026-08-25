struct PixelInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

SamplerState BloomSampler : register(s0);
SamplerState SceneSampler : register(s1);
SamplerState AdaptedLuminanceSampler : register(s2);
SamplerState BypassMaskSampler : register(s3);
SamplerState BloomGlareSampler : register(s4);

Texture2D<float4> BloomTexture : register(t0);
Texture2D<float4> SceneTexture : register(t1);
Texture2D<float4> AdaptedLuminanceTexture : register(t2);
Texture2D<float4> BypassMaskTexture : register(t3);
Texture2D<float4> EnhancedBloomTexture : register(t4);
Texture2D<float4> PhysicalGlareTexture : register(t5);

cbuffer NativeHdrBlend : register(b2)
{
    float4 NativeFlags : packoffset(c0);
    float4 NativeExposure : packoffset(c1);
    float4 NativeCinematic : packoffset(c2);
    float4 NativeTint : packoffset(c3);
    float4 NativeBloomUv : packoffset(c4);
    float4 NativeFade : packoffset(c5);
};

cbuffer FilmicSettings : register(b12)
{
    // x = exp2(exposure EV), y = native adaptation weight,
    // z = hue-preserving filmic strength, w = 5.6 white-point scale.
    float4 FilmicParameters;
};

cbuffer BloomGlareSettings : register(b13)
{
    // x=enhanced bloom ready, y=physical glare ready.
    float4 BloomGlareComposite;
};

static const float3 LuminanceWeights =
    float3(0.2125, 0.7154, 0.0721);
static const float NativeWhitePoint = 5.6;

float3 NativeHableUnnormalized(float3 color, float curveE)
{
    const float3 doubled = color * 2.0;
    const float3 numerator =
        doubled * (0.3 * color + 0.05) + 0.2 * curveE;
    const float3 denominator =
        doubled * (0.3 * color + 0.5) + 0.06;
    return numerator / denominator - curveE / 0.3;
}

float3 TonemapFilmic(float3 hdrColor, float curveE)
{
    const float whitePoint =
        NativeWhitePoint * clamp(FilmicParameters.w, 0.5, 2.0);
    const float whiteResponse = max(
        NativeHableUnnormalized(whitePoint.xxx, curveE).x,
        1.0e-5);
    const float3 nativeMapped =
        NativeHableUnnormalized(hdrColor, curveE) / whiteResponse;

    const float luminance = max(dot(hdrColor, LuminanceWeights), 0.0);
    const float mappedLuminance =
        NativeHableUnnormalized(luminance.xxx, curveE).x / whiteResponse;
    const float3 huePreserving = luminance > 1.0e-5 ?
        hdrColor * (mappedLuminance / luminance) : 0.0;
    return lerp(
        nativeMapped,
        huePreserving,
        saturate(FilmicParameters.z));
}

float4 PSMain(PixelInput input) : SV_Target0
{
    const float2 uv = input.texCoord;
    const float3 scene = SceneTexture.Sample(SceneSampler, uv).xyz;
    const float bypassMask =
        BypassMaskTexture.Sample(BypassMaskSampler, uv).x;
    const bool bypass = abs(bypassMask * 255.0 - 4.0) < 0.25;
    const float2 bloomUv = uv * NativeBloomUv.zw;
    float3 bloom = BloomTexture.Sample(BloomSampler, bloomUv).xyz;
    if (BloomGlareComposite.x > 0.5) {
        bloom += EnhancedBloomTexture.SampleLevel(
            BloomGlareSampler, uv, 0.0).xyz;
    }
    if (BloomGlareComposite.y > 0.5) {
        bloom += PhysicalGlareTexture.SampleLevel(
            BloomGlareSampler, uv, 0.0).xyz;
    }
    if (bypass) {
        return float4(scene, 1.0);
    }

    const float adaptedLuminance = AdaptedLuminanceTexture.Sample(
        AdaptedLuminanceSampler, uv).x;
    const float nativeExposure = clamp(
        NativeExposure.z / (adaptedLuminance + 0.001),
        NativeExposure.y,
        NativeExposure.x);
    const float exposure = lerp(
        1.0,
        nativeExposure,
        saturate(FilmicParameters.y)) * FilmicParameters.x;
    const float3 hdrColor = max((scene + bloom) * exposure, 0.0);
    const float3 mapped = TonemapFilmic(hdrColor, NativeExposure.w);

    const float luminance = dot(mapped, LuminanceWeights);
    float4 graded = float4(mapped, 0.0);
    graded = lerp(luminance.xxxx, graded, NativeCinematic.x);
    graded = lerp(
        graded,
        luminance * NativeTint,
        NativeTint.w);
    graded *= NativeCinematic.w;
    graded = lerp(
        adaptedLuminance.xxxx,
        graded,
        NativeCinematic.z);
    return lerp(graded, NativeFade, NativeFade.w);
}
