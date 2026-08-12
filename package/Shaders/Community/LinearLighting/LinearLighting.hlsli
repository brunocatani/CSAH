#ifndef FO4VR_COMMUNITY_SHADERS_LINEAR_LIGHTING_HLSLI
#define FO4VR_COMMUNITY_SHADERS_LINEAR_LIGHTING_HLSLI

// These registers are only valid for replacement shaders whose original
// reflection contract proves that b5 and b8 are unused.
cbuffer LinearLightingFrame : register(b5)
{
    uint enableLinearLighting;
    uint isDirLightLinear;
    float dirLightMult;
    float lightGamma;

    float colorGamma;
    float emitColorGamma;
    float glowmapGamma;
    float ambientGamma;

    float fogGamma;
    float fogAlphaGamma;
    float effectGamma;
    float effectAlphaGamma;

    float skyGamma;
    float waterGamma;
    float vlGamma;
    float vanillaDiffuseColorMult;

    float directionalLightMult;
    float pointLightMult;
    float ambientMult;
    float emitColorMult;

    float glowmapMult;
    float effectLightingMult;
    float membraneEffectMult;
    float bloodEffectMult;

    float projectedEffectMult;
    float deferredEffectMult;
    float otherEffectMult;
    uint linearLightingPad0;
};

cbuffer LinearLightingGeometry : register(b8)
{
    float emissiveMult;
    float3 linearLightingGeometryPad0;
};

float LinearLightingSkyrimGammaToLinear(float color)
{
    return pow(abs(color), 1.6f);
}

float3 LinearLightingSkyrimGammaToLinear(float3 color)
{
    return pow(abs(color), 1.6f);
}

float LinearLightingLinearToSkyrimGamma(float color)
{
    return pow(abs(color), 1.0f / 1.6f);
}

float3 LinearLightingLinearToSkyrimGamma(float3 color)
{
    return pow(abs(color), 1.0f / 1.6f);
}

float3 LinearLightingDiffuse(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(abs(color), colorGamma) * vanillaDiffuseColorMult : color;
}

float3 LinearLightingDecodedDiffuse(float3 color)
{
    return enableLinearLighting != 0u ?
        color * vanillaDiffuseColorMult : color;
}

float3 LinearLightingLight(float3 color, bool isLinear)
{
    return enableLinearLighting != 0u && !isLinear ?
        pow(abs(color), lightGamma) : color;
}

float3 LinearLightingDirectionalLight(float3 color, bool isLinear)
{
    return LinearLightingLight(color, isLinear) *
        ((enableLinearLighting != 0u && !isLinear) ?
            directionalLightMult : 1.0f);
}

float3 LinearLightingPointLight(float3 color, bool isLinear)
{
    return LinearLightingLight(color, isLinear) *
        ((enableLinearLighting != 0u && !isLinear) ?
            pointLightMult : 1.0f);
}

float3 LinearLightingEmitColor(float3 color)
{
    if (enableLinearLighting == 0u) {
        return color;
    }

    const float safeEmissiveMult = max(emissiveMult, 1e-5f);
    return pow(abs(color / safeEmissiveMult), emitColorGamma) *
        emissiveMult * emitColorMult;
}

float3 LinearLightingGlowmap(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(abs(color), glowmapGamma) * glowmapMult : color;
}

float3 LinearLightingAmbient(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(abs(color), ambientGamma) * ambientMult : color;
}

float3 LinearLightingFog(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), fogGamma) : color;
}

float LinearLightingFogAlpha(float alpha)
{
    return enableLinearLighting != 0u ? pow(abs(alpha), fogAlphaGamma) : alpha;
}

float3 LinearLightingEffect(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), effectGamma) : color;
}

float LinearLightingEffectAlpha(float alpha)
{
    return enableLinearLighting != 0u ? pow(abs(alpha), effectAlphaGamma) : alpha;
}

float3 LinearLightingSky(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), skyGamma) : color;
}

float3 LinearLightingWater(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), waterGamma) : color;
}

float3 LinearLightingVolumetricLighting(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), vlGamma) : color;
}

float3 LinearLightingColorToLinear(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), colorGamma) : color;
}

float3 LinearLightingRadianceToLinear(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float LinearLightingIrradianceToLinear(float color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float3 LinearLightingIrradianceToLinear(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float LinearLightingIrradianceToGamma(float color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingLinearToSkyrimGamma(color);
}

float3 LinearLightingIrradianceToGamma(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingLinearToSkyrimGamma(color);
}

#endif
