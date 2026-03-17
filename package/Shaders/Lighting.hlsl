// FO4VR Community Shaders — Modified Lighting Shader
// This is a stub. Full implementation requires FXP decompilation.

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

// Vanilla FO4 PerMaterial CB (b1, 8 float4s)
cbuffer PerMaterial : register(b1) {
    float4 LODTexParams;
    float4 TintColor;
    float4 EnvmapData;
    float4 ParallaxOccData;    // x=heightScale, y=maxSteps
    float4 SpecularColor;
    float4 SparkleParams;
    float4 MultiLayerParallaxData;
    float4 LightingEffectParams; // x=subSurfaceLightRolloff
};

// LINEAR_LIGHTING feature injection:
// When active, diffuse textures are converted sRGB->linear before lighting math,
// and final output is converted linear->sRGB before writing to GBuffer.
//
// Injection points in the full shader:
//   1. After diffuse texture sample: color.rgb = SRGBToLinear(color.rgb);
//   2. Before GBuffer output: output.rgb = LinearToSRGB(output.rgb);
//
// The full shader reconstruction is tracked separately.
// This stub validates that the HLSL include chain compiles.

#ifdef LINEAR_LIGHTING
// Feature-specific constant buffer at b4
cbuffer LinearLightingCB : register(b4) {
    float LL_Gamma;
    float LL_UseExact;
    float2 LL_Pad;
};

float3 ApplyLinearInput(float3 color) {
    if (LL_UseExact > 0.5f)
        return SRGBToLinear(color);
    return SRGBToLinearFast(color);
}

float3 ApplyLinearOutput(float3 color) {
    if (LL_UseExact > 0.5f)
        return LinearToSRGB(color);
    return LinearToSRGBFast(color);
}
#endif
