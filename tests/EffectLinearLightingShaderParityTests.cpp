#include "Features/linear_lighting/LinearLightingSettings.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using community_shaders::linear_lighting::FrameData;
    using community_shaders::linear_lighting::Settings;
    using community_shaders::linear_lighting::makeFrameData;

    using Pixel = std::array<float, 4>;

    constexpr float kTolerance = 1.0e-4F;
    constexpr Pixel kBaseColor{ 0.42F, 0.63F, 0.31F, 0.77F };
    constexpr Pixel kPropertyColor{ 0.8F, 0.45F, 0.7F, 0.9F };
    constexpr Pixel kFogParam{ 0.65F, 0.8F, 0.45F, 0.7F };
    constexpr Pixel kTextureColor{ 0.2F, 0.4F, 0.6F, 0.3F };
    constexpr Pixel kDepthTexture{ 1.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kDepthTestTexturePass{ 1.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kDepthTestTextureFail{ 0.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kPipboyTexture{ 0.25F, 0.5F, 0.75F, 0.65F };
    constexpr Pixel kPipboyControlsConverted{ 1.0F, 0.0F, 0.8F, 0.45F };
    constexpr Pixel kPipboyControlsRaw{ 0.0F, 1.0F, 0.6F, 0.4F };
    constexpr std::array<Pixel, 16> kGrayscaleTexture{ {
        { 0.11F, 0.21F, 0.31F, 0.17F },
        { 0.14F, 0.24F, 0.34F, 0.27F },
        { 0.18F, 0.28F, 0.38F, 0.37F },
        { 0.22F, 0.32F, 0.42F, 0.47F },
        { 0.26F, 0.36F, 0.46F, 0.23F },
        { 0.30F, 0.40F, 0.50F, 0.33F },
        { 0.34F, 0.44F, 0.54F, 0.43F },
        { 0.38F, 0.48F, 0.58F, 0.53F },
        { 0.42F, 0.52F, 0.62F, 0.29F },
        { 0.46F, 0.56F, 0.66F, 0.39F },
        { 0.50F, 0.60F, 0.70F, 0.49F },
        { 0.54F, 0.64F, 0.74F, 0.59F },
        { 0.58F, 0.68F, 0.78F, 0.35F },
        { 0.62F, 0.72F, 0.82F, 0.45F },
        { 0.66F, 0.76F, 0.86F, 0.55F },
        { 0.70F, 0.80F, 0.90F, 0.65F },
    } };
    constexpr Pixel kDepthParameters{ 0.0F, 1.0F, 1.0F, 0.0F };
    constexpr Pixel kUIMaskControls{ 1.0F, 4.0F, 0.0F, 0.75F };
    constexpr Pixel kUIMaskRectangle{ -0.6F, 0.4F, 0.8F, 0.8F };
    constexpr Pixel kUIMaskColor{ 0.25F, 0.5F, 0.75F, 0.0F };
    constexpr Pixel kModelPosition{ 0.2F, -0.1F, 0.3F, 0.0F };
    constexpr std::array<Pixel, 2> kPointLightPositionX{ {
        { 1.0F, -0.8F, 0.4F, 1.5F },
        { -0.5F, 0.9F, -1.2F, 0.1F },
    } };
    constexpr std::array<Pixel, 2> kPointLightPositionY{ {
        { 0.3F, -1.0F, 0.7F, -0.6F },
        { 1.1F, -0.4F, 0.2F, -1.3F },
    } };
    constexpr std::array<Pixel, 2> kPointLightPositionZ{ {
        { -0.2F, 0.8F, 1.4F, 0.1F },
        { 0.6F, -0.9F, 1.0F, 1.7F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionX{ {
        { -0.8F, 0.6F, -0.2F, -0.4F },
        { 0.4F, -0.7F, 0.8F, 0.1F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionY{ {
        { 0.1F, -0.7F, 0.5F, 0.8F },
        { -0.6F, 0.2F, -0.1F, 0.9F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionZ{ {
        { 0.6F, 0.4F, -0.84F, -0.45F },
        { 0.69F, 0.68F, 0.59F, -0.42F },
    } };
    constexpr Pixel kSpotLightExponent{ 1.5F, 0.0F, 2.0F, 0.5F };
    constexpr Pixel kSpotLightCosHalfAngle{ 0.2F, 0.0F, 0.5F, -0.2F };
    constexpr Pixel kPointLightInverseRadius{ 0.4F, 0.5F, 0.35F, 0.25F };
    constexpr Pixel kPointLightColorR{ 0.35F, 0.15F, 0.55F, 0.25F };
    constexpr Pixel kPointLightColorG{ 0.1F, 0.45F, 0.2F, 0.6F };
    constexpr Pixel kPointLightColorB{ 0.5F, 0.3F, 0.12F, 0.4F };
    constexpr Pixel kDirectionalLightColor{ 0.12F, 0.08F, 0.18F, 0.0F };
    constexpr Pixel kVertexColor{ 0.55F, 0.75F, 0.35F, 0.6F };
    constexpr float kLightingInfluence = 0.35F;
    constexpr float kSoftDepthScale = 1.0F;
    constexpr float kSoftParticleDepth = 0.75F;
    constexpr float kDepthTestValue = 0.25F;
    constexpr float kGrayscaleInput = 0.65F;
    constexpr float kBaseColorScale = 0.8F;
    static_assert(kDepthTestTexturePass[0] >= kDepthTestValue);
    static_assert(kDepthTestTextureFail[0] < kDepthTestValue);

    struct EffectContract
    {
        const char* name;
        std::uint32_t descriptor;

        [[nodiscard]] constexpr bool vertexColored() const noexcept
        {
            return (descriptor & 0x1U) != 0;
        }

        [[nodiscard]] constexpr bool textured() const noexcept
        {
            return (descriptor & 0x4U) != 0;
        }

        [[nodiscard]] constexpr bool additive() const noexcept
        {
            return (descriptor & 0x20U) != 0;
        }

        [[nodiscard]] constexpr bool multiplyBlend() const noexcept
        {
            return (descriptor & 0x40U) != 0;
        }

        [[nodiscard]] constexpr bool particle() const noexcept
        {
            return (descriptor & 0x80U) != 0;
        }

        [[nodiscard]] constexpr bool soft() const noexcept
        {
            return (descriptor & 0x1000U) != 0;
        }

        [[nodiscard]] constexpr bool grayscaleColor() const noexcept
        {
            return (descriptor & 0x2000U) != 0;
        }

        [[nodiscard]] constexpr bool grayscaleAlpha() const noexcept
        {
            return (descriptor & 0x4000U) != 0;
        }

        [[nodiscard]] constexpr bool usesBaseTextureAlpha() const noexcept
        {
            return textured() && !grayscaleAlpha();
        }

        [[nodiscard]] constexpr bool depthTested() const noexcept
        {
            return (descriptor & 0x01000000U) != 0;
        }

        [[nodiscard]] constexpr bool pipboy() const noexcept
        {
            return (descriptor & 0x00100000U) != 0;
        }

        [[nodiscard]] constexpr bool uiMaskRects() const noexcept
        {
            return (descriptor & 0x08000000U) != 0;
        }

        [[nodiscard]] constexpr bool lighting() const noexcept
        {
            return (descriptor & 0x00000400U) != 0;
        }

        [[nodiscard]] constexpr bool premultipliedAlpha() const noexcept
        {
            return (descriptor & 0x40000000U) != 0;
        }

        [[nodiscard]] constexpr bool needsParticleData() const noexcept
        {
            return particle() || soft();
        }
    };

    constexpr std::array<EffectContract, 205> kEffectContracts{ {
        { "EffectDefault_00000000", 0x00000000U },
        { "EffectVertexColor_00000001", 0x00000001U },
        { "EffectTextured_00000004", 0x00000004U },
        { "EffectVertexColorTextured_00000005", 0x00000005U },
        { "EffectAdditive_00000020", 0x00000020U },
        { "EffectVertexColorAdditive_00000021", 0x00000021U },
        { "EffectTexturedAdditive_00000024", 0x00000024U },
        { "EffectVertexColorTexturedAdditive_00000025", 0x00000025U },
        { "EffectMultiplyBlend_00000040", 0x00000040U },
        { "EffectVertexColorMultiplyBlend_00000041", 0x00000041U },
        { "EffectTexturedMultiplyBlend_00000044", 0x00000044U },
        { "EffectVertexColorTexturedMultiplyBlend_00000045", 0x00000045U },
        { "EffectVertexColorParticle_00000081", 0x00000081U },
        { "EffectVertexColorTexturedParticle_00000085", 0x00000085U },
        { "EffectTexturedParticle_0000008C", 0x0000008CU },
        { "EffectVertexColorTexturedAdditiveParticle_000000A5", 0x000000A5U },
        { "EffectSoft_00001000", 0x00001000U },
        { "EffectVertexColorSoft_00001001", 0x00001001U },
        { "EffectTexturedSoft_00001004", 0x00001004U },
        { "EffectVertexColorTexturedSoft_00001005", 0x00001005U },
        { "EffectAdditiveSoft_00001020", 0x00001020U },
        { "EffectVertexColorAdditiveSoft_00001021", 0x00001021U },
        { "EffectTexturedAdditiveSoft_00001024", 0x00001024U },
        { "EffectVertexColorTexturedAdditiveSoft_00001025", 0x00001025U },
        { "EffectVertexColorMultiplyBlendSoft_00001041", 0x00001041U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoft_000010CD", 0x000010CDU },
        { "EffectPipboy_00100000", 0x00100000U },
        { "EffectVertexColorPipboy_00100001", 0x00100001U },
        { "EffectTexturedPipboy_00100004", 0x00100004U },
        { "EffectVertexColorTexturedPipboy_00100005", 0x00100005U },
        { "EffectTexturedAdditivePipboy_00100024", 0x00100024U },
        { "EffectVertexColorTexturedAdditivePipboy_00100025", 0x00100025U },
        { "EffectDepthTest_01000000", 0x01000000U },
        { "EffectTexturedAdditiveDepthTest_01000024", 0x01000024U },
        { "EffectPremultipliedAlpha_40000000", 0x40000000U },
        { "EffectVertexColorPremultipliedAlpha_40000001", 0x40000001U },
        { "EffectTexturedPremultipliedAlpha_40000004", 0x40000004U },
        { "EffectVertexColorTexturedPremultipliedAlpha_40000005", 0x40000005U },
        { "EffectAdditivePremultipliedAlpha_40000020", 0x40000020U },
        { "EffectVertexColorAdditivePremultipliedAlpha_40000021", 0x40000021U },
        { "EffectTexturedAdditivePremultipliedAlpha_40000024", 0x40000024U },
        { "EffectVertexColorTexturedAdditivePremultipliedAlpha_40000025", 0x40000025U },
        { "EffectVertexColorTexturedMultiplyBlendPremultipliedAlpha_40000045", 0x40000045U },
        { "EffectVertexColorTexturedParticlePremultipliedAlpha_40000085", 0x40000085U },
        { "EffectVertexColorTexturedAdditiveParticlePremultipliedAlpha_400000A5", 0x400000A5U },
        { "EffectSoftPremultipliedAlpha_40001000", 0x40001000U },
        { "EffectTexturedSoftPremultipliedAlpha_40001004", 0x40001004U },
        { "EffectVertexColorTexturedSoftPremultipliedAlpha_40001005", 0x40001005U },
        { "EffectTexturedAdditiveSoftPremultipliedAlpha_40001024", 0x40001024U },
        { "EffectVertexColorTexturedAdditiveSoftPremultipliedAlpha_40001025", 0x40001025U },
        { "EffectTexturedPipboyPremultipliedAlpha_40100004", 0x40100004U },
        { "EffectVertexColorTexturedPipboyPremultipliedAlpha_40100005", 0x40100005U },
        { "EffectTexturedMultiplyBlendPremultipliedAlpha_50000044", 0x50000044U },
        { "EffectTexturedGrayscaleColor_00002004", 0x00002004U },
        { "EffectVertexColorTexturedGrayscaleColor_00002005", 0x00002005U },
        { "EffectTexturedAdditiveGrayscaleColor_00002024", 0x00002024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColor_00002025", 0x00002025U },
        { "EffectTexturedMultiplyBlendGrayscaleColor_00002044", 0x00002044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColor_00002045", 0x00002045U },
        { "EffectVertexColorTexturedParticleGrayscaleColor_00002085", 0x00002085U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColor_000020A5", 0x000020A5U },
        { "EffectTexturedSoftGrayscaleColor_00003004", 0x00003004U },
        { "EffectVertexColorTexturedSoftGrayscaleColor_00003005", 0x00003005U },
        { "EffectTexturedAdditiveSoftGrayscaleColor_00003024", 0x00003024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColor_00003025", 0x00003025U },
        { "EffectTexturedGrayscaleAlpha_00004004", 0x00004004U },
        { "EffectVertexColorTexturedGrayscaleAlpha_00004005", 0x00004005U },
        { "EffectTexturedAdditiveGrayscaleAlpha_00004024", 0x00004024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlpha_00004025", 0x00004025U },
        { "EffectTexturedMultiplyBlendGrayscaleAlpha_00004044", 0x00004044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleAlpha_00004045", 0x00004045U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlpha_000040A5", 0x000040A5U },
        { "EffectTexturedSoftGrayscaleAlpha_00005004", 0x00005004U },
        { "EffectVertexColorTexturedSoftGrayscaleAlpha_00005005", 0x00005005U },
        { "EffectVertexColorAdditiveSoftGrayscaleAlpha_00005021", 0x00005021U },
        { "EffectTexturedAdditiveSoftGrayscaleAlpha_00005024", 0x00005024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlpha_00005025", 0x00005025U },
        { "EffectTexturedGrayscaleColorAlpha_00006004", 0x00006004U },
        { "EffectVertexColorTexturedGrayscaleColorAlpha_00006005", 0x00006005U },
        { "EffectTexturedAdditiveGrayscaleColorAlpha_00006024", 0x00006024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlpha_00006025", 0x00006025U },
        { "EffectTexturedMultiplyBlendGrayscaleColorAlpha_00006044", 0x00006044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColorAlpha_00006045", 0x00006045U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlpha_00006085", 0x00006085U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlpha_000060A5", 0x000060A5U },
        { "EffectTexturedSoftGrayscaleColorAlpha_00007004", 0x00007004U },
        { "EffectVertexColorTexturedSoftGrayscaleColorAlpha_00007005", 0x00007005U },
        { "EffectVertexColorAdditiveSoftGrayscaleColorAlpha_00007021", 0x00007021U },
        { "EffectTexturedAdditiveSoftGrayscaleColorAlpha_00007024", 0x00007024U },
        { "EffectVertexColorTexturedGrayscaleColorPremultipliedAlpha_40002005", 0x40002005U },
        { "EffectTexturedAdditiveGrayscaleColorPremultipliedAlpha_40002024", 0x40002024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorPremultipliedAlpha_40002025", 0x40002025U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorPremultipliedAlpha_400020A5", 0x400020A5U },
        { "EffectVertexColorTexturedSoftGrayscaleColorPremultipliedAlpha_40003005", 0x40003005U },
        { "EffectTexturedAdditiveSoftGrayscaleColorPremultipliedAlpha_40003024", 0x40003024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorPremultipliedAlpha_40003025", 0x40003025U },
        { "EffectTexturedGrayscaleAlphaPremultipliedAlpha_40004004", 0x40004004U },
        { "EffectVertexColorTexturedGrayscaleAlphaPremultipliedAlpha_40004005", 0x40004005U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlphaPremultipliedAlpha_40004025", 0x40004025U },
        { "EffectTexturedSoftGrayscaleAlphaPremultipliedAlpha_40005004", 0x40005004U },
        { "EffectTexturedAdditiveSoftGrayscaleAlphaPremultipliedAlpha_40005024", 0x40005024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaPremultipliedAlpha_40005025", 0x40005025U },
        { "EffectTexturedAdditiveGrayscaleColorAlphaPremultipliedAlpha_40006024", 0x40006024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlphaPremultipliedAlpha_40006025", 0x40006025U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColorAlphaPremultipliedAlpha_40006045", 0x40006045U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorAlphaPremultipliedAlpha_40007025", 0x40007025U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleAlphaPremultipliedAlpha_50004045", 0x50004045U },
        { "EffectVertexColorTexturedMultiplyBlendParticle_000000CD", 0x000000CDU },
        { "EffectVertexColorTexturedMultiplyBlendParticlePremultipliedAlpha_400000CD", 0x400000CDU },
        { "EffectTexturedUIMaskRects_08000004", 0x08000004U },
        { "EffectTexturedPipboyUIMaskRects_08100004", 0x08100004U },
        { "EffectTexturedUIMaskRectsPremultipliedAlpha_48000004", 0x48000004U },
        { "EffectLighting_00000400", 0x00000400U },
        { "EffectVertexColorLighting_00000401", 0x00000401U },
        { "EffectTexturedLighting_00000404", 0x00000404U },
        { "EffectVertexColorTexturedLighting_00000405", 0x00000405U },
        { "EffectAdditiveLighting_00000420", 0x00000420U },
        { "EffectVertexColorAdditiveLighting_00000421", 0x00000421U },
        { "EffectTexturedAdditiveLighting_00000424", 0x00000424U },
        { "EffectVertexColorTexturedAdditiveLighting_00000425", 0x00000425U },
        { "EffectMultiplyBlendLighting_00000440", 0x00000440U },
        { "EffectVertexColorMultiplyBlendLighting_00000441", 0x00000441U },
        { "EffectTexturedMultiplyBlendLighting_00000444", 0x00000444U },
        { "EffectVertexColorTexturedMultiplyBlendLighting_00000445", 0x00000445U },
        { "EffectVertexColorTexturedParticleLighting_00000485", 0x00000485U },
        { "EffectVertexColorTexturedAdditiveParticleLighting_000004A5", 0x000004A5U },
        { "EffectVertexColorTexturedMultiplyBlendParticleLighting_000004CD", 0x000004CDU },
        { "EffectTexturedLightingDepthTest_01000404", 0x01000404U },
        { "EffectTexturedAdditiveLightingDepthTest_01000424", 0x01000424U },
        { "EffectLightingPremultipliedAlpha_40000400", 0x40000400U },
        { "EffectTexturedLightingPremultipliedAlpha_40000404", 0x40000404U },
        { "EffectVertexColorTexturedLightingPremultipliedAlpha_40000405", 0x40000405U },
        { "EffectTexturedAdditiveLightingPremultipliedAlpha_40000424", 0x40000424U },
        { "EffectVertexColorTexturedAdditiveLightingPremultipliedAlpha_40000425", 0x40000425U },
        { "EffectVertexColorTexturedParticleLightingPremultipliedAlpha_4000048D", 0x4000048DU },
        { "EffectVertexColorTexturedAdditiveParticleLightingPremultipliedAlpha_400004A5", 0x400004A5U },
        { "EffectTexturedAdditiveLightingDepthTestPremultipliedAlpha_41000424", 0x41000424U },
        { "EffectSoftLighting_00001400", 0x00001400U },
        { "EffectVertexColorSoftLighting_00001401", 0x00001401U },
        { "EffectTexturedSoftLighting_00001404", 0x00001404U },
        { "EffectVertexColorTexturedSoftLighting_00001405", 0x00001405U },
        { "EffectVertexColorAdditiveSoftLighting_00001421", 0x00001421U },
        { "EffectTexturedAdditiveSoftLighting_00001424", 0x00001424U },
        { "EffectVertexColorTexturedAdditiveSoftLighting_00001425", 0x00001425U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoftLighting_000014CD", 0x000014CDU },
        { "EffectSoftLightingPremultipliedAlpha_40001400", 0x40001400U },
        { "EffectVertexColorTexturedSoftLightingPremultipliedAlpha_40001405", 0x40001405U },
        { "EffectVertexColorTexturedAdditiveSoftLightingPremultipliedAlpha_40001425", 0x40001425U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoftLightingPremultipliedAlpha_400014CD", 0x400014CDU },
        { "EffectTexturedGrayscaleColorLighting_00002404", 0x00002404U },
        { "EffectVertexColorTexturedGrayscaleColorLighting_00002405", 0x00002405U },
        { "EffectAdditiveGrayscaleColorLighting_00002420", 0x00002420U },
        { "EffectTexturedAdditiveGrayscaleColorLighting_00002424", 0x00002424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorLighting_00002425", 0x00002425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorLighting_0000248D", 0x0000248DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorLighting_000024AD", 0x000024ADU },
        { "EffectTexturedGrayscaleColorLightingPremultipliedAlpha_40002404", 0x40002404U },
        { "EffectVertexColorTexturedGrayscaleColorLightingPremultipliedAlpha_40002405", 0x40002405U },
        { "EffectTexturedAdditiveGrayscaleColorLightingPremultipliedAlpha_40002424", 0x40002424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorLightingPremultipliedAlpha_40002425", 0x40002425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorLightingPremultipliedAlpha_4000248D", 0x4000248DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorLightingPremultipliedAlpha_400024AD", 0x400024ADU },
        { "EffectTexturedSoftGrayscaleColorLighting_00003404", 0x00003404U },
        { "EffectVertexColorTexturedSoftGrayscaleColorLighting_00003405", 0x00003405U },
        { "EffectTexturedAdditiveSoftGrayscaleColorLighting_00003424", 0x00003424U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorLighting_00003425", 0x00003425U },
        { "EffectVertexColorTexturedSoftGrayscaleColorLightingPremultipliedAlpha_40003405", 0x40003405U },
        { "EffectVertexColorTexturedAdditiveParticleSoftGrayscaleColorLightingPremultipliedAlpha_400034A5", 0x400034A5U },
        { "EffectTexturedGrayscaleAlphaLighting_00004404", 0x00004404U },
        { "EffectVertexColorTexturedGrayscaleAlphaLighting_00004405", 0x00004405U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlphaLighting_00004425", 0x00004425U },
        { "EffectVertexColorTexturedParticleGrayscaleAlphaLighting_0000448D", 0x0000448DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaLighting_000044AD", 0x000044ADU },
        { "EffectVertexColorTexturedGrayscaleAlphaLightingPremultipliedAlpha_40004405", 0x40004405U },
        { "EffectVertexColorTexturedParticleGrayscaleAlphaLightingPremultipliedAlpha_4000448D", 0x4000448DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaLightingPremultipliedAlpha_400044AD", 0x400044ADU },
        { "EffectTexturedSoftGrayscaleAlphaLighting_00005404", 0x00005404U },
        { "EffectVertexColorTexturedSoftGrayscaleAlphaLighting_00005405", 0x00005405U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaLighting_00005425", 0x00005425U },
        { "EffectTexturedSoftGrayscaleAlphaLightingPremultipliedAlpha_40005404", 0x40005404U },
        { "EffectVertexColorTexturedSoftGrayscaleAlphaLightingPremultipliedAlpha_40005405", 0x40005405U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaLightingPremultipliedAlpha_40005425", 0x40005425U },
        { "EffectTexturedGrayscaleColorAlphaLighting_00006404", 0x00006404U },
        { "EffectVertexColorTexturedGrayscaleColorAlphaLighting_00006405", 0x00006405U },
        { "EffectTexturedAdditiveGrayscaleColorAlphaLighting_00006424", 0x00006424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlphaLighting_00006425", 0x00006425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaLighting_0000648D", 0x0000648DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaLighting_000064AD", 0x000064ADU },
        { "EffectTexturedGrayscaleColorAlphaLightingPremultipliedAlpha_40006404", 0x40006404U },
        { "EffectVertexColorTexturedGrayscaleColorAlphaLightingPremultipliedAlpha_40006405", 0x40006405U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaLightingPremultipliedAlpha_4000648D", 0x4000648DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaLightingPremultipliedAlpha_400064AD", 0x400064ADU },
        { "EffectVertexColorSoftGrayscaleColorAlphaLighting_00007401", 0x00007401U },
        { "EffectTexturedSoftGrayscaleColorAlphaLighting_00007404", 0x00007404U },
        { "EffectTexturedAdditiveSoftGrayscaleColorAlphaLighting_00007424", 0x00007424U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorAlphaLighting_00007425", 0x00007425U },
        { "EffectVertexColorTexturedSoftGrayscaleColorAlphaLightingPremultipliedAlpha_40007405", 0x40007405U },
        { "EffectVertexColorTexturedAdditiveParticleSoftGrayscaleColorAlphaLightingPremultipliedAlpha_400074AD", 0x400074ADU },
        { "EffectVertexColorTexturedParticleGrayscaleAlpha_0000408D", 0x0000408DU },
        { "EffectVertexColorTexturedParticleGrayscaleColorPremultipliedAlpha_4000208D", 0x4000208DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaPremultipliedAlpha_400040AD", 0x400040ADU },
        { "EffectVertexColorTexturedParticleSoftGrayscaleAlphaPremultipliedAlpha_4000508D", 0x4000508DU },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaPremultipliedAlpha_4000608D", 0x4000608DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaPremultipliedAlpha_400060AD", 0x400060ADU },
        { "EffectVertexColorTexturedParticleSoftGrayscaleColorAlphaPremultipliedAlpha_4000708D", 0x4000708DU },
    } };

    struct alignas(16) EffectPerTechnique
    {
        Pixel depthParameters{};
        Pixel uiMaskControls{};
        std::array<Pixel, 16> uiMaskRectangles{};
        Pixel uiMaskColor{};
    };
    static_assert(sizeof(EffectPerTechnique) == 19 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskControls) == sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskRectangles) == 2 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskColor) == 18 * sizeof(Pixel));

    struct alignas(16) EffectPerMaterial
    {
        Pixel baseColor{};
        Pixel baseColorScale{};
        Pixel lightingInfluence{};
        Pixel unusedDepthTest{};
        Pixel depthTestParameters{};
    };

    struct alignas(16) EffectPerGeometry
    {
        std::array<Pixel, 2> pointLightPositionX{};
        std::array<Pixel, 2> pointLightPositionY{};
        std::array<Pixel, 2> pointLightPositionZ{};
        std::array<Pixel, 2> spotLightDirectionX{};
        std::array<Pixel, 2> spotLightDirectionY{};
        std::array<Pixel, 2> spotLightDirectionZ{};
        Pixel pipboyControls{};
        Pixel spotLightExponent{};
        Pixel spotLightCosHalfAngle{};
        Pixel pointLightInverseRadius{};
        Pixel pointLightColorR{};
        Pixel pointLightColorG{};
        Pixel pointLightColorB{};
        Pixel directionalLightColor{};
        Pixel propertyColor{};
        Pixel alphaTest{};
    };
    static_assert(sizeof(EffectPerGeometry) == 22 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, spotLightDirectionX) == 6 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, pipboyControls) == 12 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, pointLightInverseRadius) ==
        15 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, directionalLightColor) ==
        19 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, propertyColor) == 20 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, alphaTest) == 21 * sizeof(Pixel));

    struct RenderTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11Texture2D> staging;
    };

    void require(HRESULT result, const char* operation)
    {
        if (FAILED(result)) {
            throw std::runtime_error(
                std::string(operation) + " failed with HRESULT " +
                std::to_string(static_cast<std::uint32_t>(result)));
        }
    }

    std::vector<std::byte> readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("could not open " + path.string());
        }
        const auto size = stream.tellg();
        if (size <= 0) {
            throw std::runtime_error("empty shader " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("could not read " + path.string());
        }
        return bytes;
    }

    template <class T>
    ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device* device,
        const T& value)
    {
        static_assert(sizeof(T) % 16 == 0);
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(T));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{ &value, 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        require(
            device->CreateBuffer(&description, &initial, buffer.GetAddressOf()),
            "CreateBuffer");
        return buffer;
    }

    ComPtr<ID3D11VertexShader> createVertexShader(
        ID3D11Device* device,
        bool vertexColored,
        bool needsParticleData,
        bool depthTested,
        bool pipboy,
        bool lighting,
        bool rightEye)
    {
        constexpr char source[] = R"(
struct VSOutput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#ifdef EFFECT_PIPBOY
    float4 pipboyTexCoord : TEXCOORD4;
#endif
#ifdef EFFECT_DEPTH_TEST
    float4 depthTestData : TEXCOORD3;
#endif
#ifdef EFFECT_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float4 color : COLOR1;
#ifdef EFFECT_PIPBOY
    float3 pipboyData : TEXCOORD1;
#endif
#ifdef EFFECT_LIGHTING
    float3 modelPosition : TEXCOORD6;
#endif
#ifdef EFFECT_PARTICLE
    float3 particleData : TEXCOORD5;
#endif
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    output.texCoord = float4(0.5, 0.5, 0.65, 0.0);
#ifdef EFFECT_PIPBOY
    output.pipboyTexCoord = 0.0.xxxx;
    output.pipboyData = 0.0.xxx;
#endif
#ifdef EFFECT_LIGHTING
    output.modelPosition = float3(0.2, -0.1, 0.3);
#endif
#ifdef EFFECT_DEPTH_TEST
    output.depthTestData = float4(0.0, 0.0, 0.25, 0.0);
#endif
#ifdef EFFECT_VERTEX_COLOR
    output.vertexColor = float4(0.55, 0.75, 0.35, 0.6);
#endif
    output.color = float4(0.65, 0.8, 0.45, 0.7);
#ifdef EFFECT_PARTICLE
    output.particleData = float3(0.2, 0.4, 0.75);
#endif
#ifdef EFFECT_RIGHT_EYE
    output.eyeIndex = 1;
#else
    output.eyeIndex = 0;
#endif
    output.cullDistance = 1.0;
    output.clipDistance = 1.0;
    return output;
}
)";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        std::array<D3D_SHADER_MACRO, 7> macros{};
        std::size_t macroCount = 0;
        if (vertexColored) {
            macros[macroCount++] = { "EFFECT_VERTEX_COLOR", "1" };
        }
        if (needsParticleData) {
            macros[macroCount++] = { "EFFECT_PARTICLE", "1" };
        }
        if (depthTested) {
            macros[macroCount++] = { "EFFECT_DEPTH_TEST", "1" };
        }
        if (pipboy) {
            macros[macroCount++] = { "EFFECT_PIPBOY", "1" };
        }
        if (lighting) {
            macros[macroCount++] = { "EFFECT_LIGHTING", "1" };
        }
        if (rightEye) {
            macros[macroCount++] = { "EFFECT_RIGHT_EYE", "1" };
        }
        const auto result = D3DCompile(
            source,
            sizeof(source) - 1,
            "EffectLinearLightingParityVS",
            macroCount != 0 ? macros.data() : nullptr,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(result)) {
            const std::string detail = errors ?
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()) :
                "no compiler diagnostics";
            throw std::runtime_error("vertex shader compile failed: " + detail);
        }
        ComPtr<ID3D11VertexShader> shader;
        require(
            device->CreateVertexShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                shader.GetAddressOf()),
            "CreateVertexShader");
        return shader;
    }

    ComPtr<ID3D11PixelShader> createPixelShader(
        ID3D11Device* device,
        const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytes.data(), bytes.size(), nullptr, shader.GetAddressOf()),
            "CreatePixelShader");
        return shader;
    }

    ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device* device,
        const Pixel& pixel)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ pixel.data(), sizeof(Pixel), 0 };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView");
        return view;
    }

    ComPtr<ID3D11ShaderResourceView> createGrayscaleTexture(
        ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 4;
        description.Height = 4;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{
            kGrayscaleTexture.data(),
            4 * sizeof(Pixel),
            0
        };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(grayscale shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView(grayscale)");
        return view;
    }

    RenderTarget createRenderTarget(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;

        RenderTarget target;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.texture.GetAddressOf()),
            "CreateTexture2D(render target)");
        require(
            device->CreateRenderTargetView(
                target.texture.Get(), nullptr, target.view.GetAddressOf()),
            "CreateRenderTargetView");

        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.staging.GetAddressOf()),
            "CreateTexture2D(staging)");
        return target;
    }

    Pixel render(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* techniqueBuffer,
        ID3D11Buffer* materialBuffer,
        ID3D11Buffer* geometryBuffer,
        ID3D11Buffer* frameBuffer,
        ID3D11ShaderResourceView* texture,
        ID3D11ShaderResourceView* depthTexture,
        ID3D11ShaderResourceView* depthTestTexture,
        ID3D11ShaderResourceView* grayscaleTexture,
        ID3D11ShaderResourceView* pipboyTexture,
        ID3D11SamplerState* sampler)
    {
        auto target = createRenderTarget(device);
        const float clear[4]{};
        context->ClearRenderTargetView(target.view.Get(), clear);
        ID3D11RenderTargetView* renderTarget = target.view.Get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        const D3D11_VIEWPORT viewport{ 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &techniqueBuffer);
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        context->PSSetConstantBuffers(2, 1, &geometryBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->PSSetShaderResources(0, 1, &texture);
        context->PSSetShaderResources(3, 1, &depthTexture);
        context->PSSetShaderResources(4, 1, &grayscaleTexture);
        context->PSSetShaderResources(6, 1, &pipboyTexture);
        context->PSSetShaderResources(8, 1, &depthTestTexture);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetSamplers(4, 1, &sampler);
        context->PSSetSamplers(6, 1, &sampler);
        context->Draw(3, 0);

        context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(
            context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(render target)");
        Pixel result{};
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(target.staging.Get(), 0);

        ID3D11ShaderResourceView* nullTexture{};
        context->PSSetShaderResources(0, 1, &nullTexture);
        context->PSSetShaderResources(3, 1, &nullTexture);
        context->PSSetShaderResources(4, 1, &nullTexture);
        context->PSSetShaderResources(6, 1, &nullTexture);
        context->PSSetShaderResources(8, 1, &nullTexture);
        ID3D11RenderTargetView* nullTarget{};
        context->OMSetRenderTargets(1, &nullTarget, nullptr);
        return result;
    }

    bool nearValue(float actual, float expected)
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <=
                kTolerance * std::max(1.0F, std::abs(expected));
    }

    bool compare(
        const Pixel& actual,
        const Pixel& expected,
        const std::string& label)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel) {
            if (!nearValue(actual[channel], expected[channel])) {
                std::cerr << label << " channel " << channel << " expected "
                          << expected[channel] << " but got " << actual[channel]
                          << '\n';
                return false;
            }
        }
        return true;
    }

    float expectedSoftFade()
    {
        const auto deviceDepth = 1.0F - kDepthTexture[0];
        const auto sceneDepth =
            deviceDepth * kDepthParameters[2] + kDepthParameters[1];
        const auto cameraFadeDepth =
            kDepthParameters[1] * kDepthParameters[2] +
            kDepthParameters[1];
        const auto intersectionFade = std::clamp(
            kSoftDepthScale / sceneDepth - kSoftParticleDepth,
            0.0F,
            1.0F);
        auto cameraFade = std::clamp(
            kSoftParticleDepth - kSoftDepthScale / cameraFadeDepth,
            0.0F,
            1.0F);
        cameraFade = std::clamp(
            (cameraFade - 0.075F) * 2.352941176470588F,
            0.0F,
            1.0F);
        cameraFade =
            cameraFade * cameraFade * (3.0F - 2.0F * cameraFade);
        return intersectionFade * cameraFade;
    }

    Pixel sampleGrayscaleTexture(float u, float v)
    {
        const auto texel = [](float coordinate) {
            return std::min(
                3U,
                static_cast<unsigned>(
                    std::floor(std::max(0.0F, coordinate) * 4.0F)));
        };
        return kGrayscaleTexture[texel(v) * 4U + texel(u)];
    }

    Pixel grayscaleColorSample(const EffectContract& contract)
    {
        auto v = std::pow(kBaseColor[0], 1.0F / 2.2F) *
            kGrayscaleInput;
        if (contract.vertexColored()) {
            v *= kVertexColor[0];
        }
        if (contract.soft()) {
            v *= expectedSoftFade();
        }
        return sampleGrayscaleTexture(
            std::pow(kTextureColor[1], 1.0F / 2.2F),
            v);
    }

    float grayscaleAlphaSample(const EffectContract& contract)
    {
        auto v = std::pow(kBaseColor[3], 1.0F / 2.2F) *
            kGrayscaleInput *
            std::pow(kPropertyColor[3], 1.0F / 2.2F);
        if (contract.vertexColored()) {
            v *= kVertexColor[3];
        }
        if (contract.soft()) {
            v *= expectedSoftFade();
        }
        return sampleGrayscaleTexture(kTextureColor[3], v)[3];
    }

    float expectedUIMaskFactor()
    {
        constexpr float u = 0.5F;
        constexpr float v = 0.5F;
        const auto upperX = std::abs(kUIMaskRectangle[0]) - u;
        const auto upperY = kUIMaskRectangle[1] - v;
        const auto lowerX = u - kUIMaskRectangle[2];
        const auto lowerY = v - kUIMaskRectangle[3];
        const auto distance = std::max(
            std::max(upperX, lowerX),
            std::max(upperY, lowerY));
        const auto rectangleMask = std::clamp(
            1.0F - kUIMaskControls[1] * distance,
            0.0F,
            1.0F);
        auto verticalRamp = 1.0F;
        if (kUIMaskRectangle[0] < 0.0F &&
            v >= kUIMaskRectangle[1] - 0.0025F &&
            v <= kUIMaskRectangle[3] + 0.0025F) {
            verticalRamp = (v - kUIMaskRectangle[1]) /
                (kUIMaskRectangle[3] - kUIMaskRectangle[1]);
        }
        return rectangleMask * verticalRamp * kUIMaskControls[3];
    }

    Pixel expectedLightingColor(
        std::uint32_t eyeIndex,
        const Settings* enabledSettings)
    {
        if (eyeIndex >= kPointLightPositionX.size()) {
            throw std::runtime_error("invalid Effect lighting eye index");
        }

        Pixel attenuation{};
        for (std::size_t light = 0; light < attenuation.size(); ++light) {
            const auto deltaX =
                kModelPosition[0] - kPointLightPositionX[eyeIndex][light];
            const auto deltaY =
                kModelPosition[1] - kPointLightPositionY[eyeIndex][light];
            const auto deltaZ =
                kModelPosition[2] - kPointLightPositionZ[eyeIndex][light];
            const auto distance = std::sqrt(
                deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            const auto scaledDistance = std::clamp(
                distance * kPointLightInverseRadius[light],
                0.0F,
                1.0F);
            auto distanceFade =
                1.0F - scaledDistance * scaledDistance;
            if (enabledSettings == nullptr) {
                distanceFade = std::pow(distanceFade, 2.2F);
            }

            const auto safeDistance = std::max(distance, 0.001F);
            const auto spotCosine = std::clamp(
                deltaX / safeDistance *
                    kSpotLightDirectionX[eyeIndex][light] +
                deltaY / safeDistance *
                    kSpotLightDirectionY[eyeIndex][light] +
                deltaZ / safeDistance *
                    kSpotLightDirectionZ[eyeIndex][light],
                0.0F,
                1.0F);
            const auto coneFade = std::clamp(
                1.0F -
                    (1.0F - spotCosine) /
                        (1.0F - kSpotLightCosHalfAngle[light]),
                0.0F,
                1.0F);
            const auto spotFade = kSpotLightExponent[light] != 0.0F ?
                std::min(
                    std::pow(coneFade, kSpotLightExponent[light]),
                    1.0F) :
                1.0F;
            attenuation[light] = distanceFade * spotFade;
        }

        Pixel result = kDirectionalLightColor;
        constexpr float pi = 3.14159265358979323846F;
        const std::array<const Pixel*, 3> packedColors{
            &kPointLightColorR,
            &kPointLightColorG,
            &kPointLightColorB,
        };
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (enabledSettings != nullptr) {
                result[channel] *=
                    enabledSettings->effectLightingMultiplier;
            }
            for (std::size_t light = 0; light < attenuation.size(); ++light) {
                auto lightColor = (*packedColors[channel])[light];
                if (enabledSettings != nullptr) {
                    lightColor = std::pow(
                        std::abs(lightColor),
                        enabledSettings->lightGamma) *
                        pi * enabledSettings->pointLightMultiplier *
                        enabledSettings->effectLightingMultiplier;
                }
                result[channel] += attenuation[light] * lightColor;
            }
        }
        return result;
    }

    Pixel expectedVanilla(
        const EffectContract& contract,
        const Pixel& pipboyControls = kPipboyControlsConverted,
        bool usePipboyAlpha = true,
        bool uiMaskColorIsLinear = false,
        std::uint32_t eyeIndex = 0)
    {
        if (contract.uiMaskRects()) {
            auto alpha = kTextureColor[3] *
                kBaseColor[3] * kPropertyColor[3];
            if (contract.pipboy() && usePipboyAlpha) {
                auto pipboyAlpha = kPipboyTexture[3];
                if (pipboyControls[1] == 0.0F) {
                    pipboyAlpha = std::pow(pipboyAlpha, 2.2F);
                }
                alpha = pipboyAlpha;
                if (pipboyControls[0] != 0.0F) {
                    alpha *= kBaseColor[3];
                }
                alpha *= pipboyControls[2];
            }
            alpha *= expectedUIMaskFactor();

            Pixel result{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                result[channel] = uiMaskColorIsLinear ?
                    kUIMaskColor[channel] :
                    std::pow(kUIMaskColor[channel], 2.2F);
                if (contract.premultipliedAlpha()) {
                    result[channel] *= alpha;
                }
            }
            result[3] = alpha;
            return result;
        }

        Pixel result{};
        auto alpha = contract.grayscaleAlpha() ?
            grayscaleAlphaSample(contract) :
            kBaseColor[3] *
                (contract.vertexColored() ?
                        std::pow(kVertexColor[3], 2.2F) :
                        1.0F) *
                (contract.usesBaseTextureAlpha() ? kTextureColor[3] : 1.0F) *
                kPropertyColor[3];
        if (contract.soft() && !contract.grayscaleAlpha()) {
            alpha *= expectedSoftFade();
        }
        auto pipboyColor = kPipboyTexture;
        if (contract.pipboy() && pipboyControls[1] == 0.0F) {
            for (auto& channel : pipboyColor) {
                channel = std::pow(channel, 2.2F);
            }
        }
        auto outputAlpha = alpha;
        if (contract.pipboy()) {
            outputAlpha = usePipboyAlpha ? pipboyColor[3] : alpha;
            if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                outputAlpha *= kBaseColor[3];
            }
            outputAlpha *= usePipboyAlpha ? pipboyControls[2] : 1.0F;
        }
        const auto grayscaleColor = grayscaleColorSample(contract);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto baseColor = contract.grayscaleColor() ?
                grayscaleColor[channel] * kBaseColorScale :
                kBaseColor[channel] *
                    (contract.vertexColored() ?
                            std::pow(kVertexColor[channel], 2.2F) :
                            1.0F) *
                    (contract.textured() ?
                            kTextureColor[channel] :
                            1.0F);
            const auto propertyColor = contract.lighting() ?
                expectedLightingColor(eyeIndex, nullptr)[channel] :
                kPropertyColor[channel];
            const auto lightColor = baseColor +
                kLightingInfluence *
                    (propertyColor * baseColor - baseColor);
            if (contract.additive()) {
                result[channel] = lightColor * (1.0F - kFogParam[3]);
            } else if (contract.multiplyBlend()) {
                const auto fogFactor =
                    std::clamp(1.5F * kFogParam[3], 0.0F, 1.0F);
                const auto foggedColor =
                    lightColor + fogFactor * (1.0F - lightColor);
                result[channel] = 1.0F + alpha * (foggedColor - 1.0F);
            } else {
                result[channel] = lightColor +
                    kFogParam[3] * (kFogParam[channel] - lightColor);
            }
            if (contract.pipboy()) {
                result[channel] +=
                    pipboyColor[channel] * pipboyControls[3];
                if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                    result[channel] *= kBaseColor[3];
                }
            }
            if (contract.premultipliedAlpha()) {
                result[channel] *= outputAlpha;
            }
        }
        result[3] = outputAlpha;
        return result;
    }

    Pixel expectedEnabled(
        const EffectContract& contract,
        const Settings& settings,
        const Pixel& pipboyControls = kPipboyControlsConverted,
        bool usePipboyAlpha = true,
        bool uiMaskColorIsLinear = false,
        std::uint32_t eyeIndex = 0)
    {
        if (contract.uiMaskRects()) {
            auto alpha = kTextureColor[3] *
                kBaseColor[3] * kPropertyColor[3];
            if (contract.pipboy() && usePipboyAlpha) {
                alpha = kPipboyTexture[3];
                if (pipboyControls[0] != 0.0F) {
                    alpha *= kBaseColor[3];
                }
                alpha *= pipboyControls[2];
            }
            alpha = std::pow(
                std::abs(alpha * expectedUIMaskFactor()),
                settings.effectAlphaGamma);

            Pixel result{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                result[channel] = (uiMaskColorIsLinear ?
                    kUIMaskColor[channel] :
                    std::pow(
                        std::abs(kUIMaskColor[channel]),
                        settings.effectGamma)) *
                    settings.otherEffectMultiplier;
                if (contract.premultipliedAlpha()) {
                    result[channel] *= alpha;
                }
            }
            result[3] = alpha;
            return result;
        }

        Pixel result{};
        auto rawAlpha = contract.grayscaleAlpha() ?
            grayscaleAlphaSample(contract) :
            kBaseColor[3] *
                (contract.vertexColored() ? kVertexColor[3] : 1.0F) *
                (contract.usesBaseTextureAlpha() ? kTextureColor[3] : 1.0F) *
                kPropertyColor[3];
        if (contract.soft() && !contract.grayscaleAlpha()) {
            rawAlpha *= expectedSoftFade();
        }
        auto pipboyColor = kPipboyTexture;
        if (contract.pipboy() && pipboyControls[1] == 0.0F) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                pipboyColor[channel] = std::pow(
                    std::abs(pipboyColor[channel]),
                    settings.effectGamma);
            }
        }
        auto compositeRawAlpha = rawAlpha;
        if (contract.pipboy()) {
            compositeRawAlpha = usePipboyAlpha ? pipboyColor[3] : rawAlpha;
            if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                compositeRawAlpha *= kBaseColor[3];
            }
            compositeRawAlpha *=
                usePipboyAlpha ? pipboyControls[2] : 1.0F;
        }
        const auto outputAlpha =
            std::pow(
                std::abs(compositeRawAlpha),
                settings.effectAlphaGamma);
        const auto fogFactor =
            std::pow(std::abs(kFogParam[3]), settings.fogAlphaGamma);
        const auto grayscaleColor = grayscaleColorSample(contract);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            float base{};
            if (contract.grayscaleColor()) {
                base = std::pow(
                    std::abs(grayscaleColor[channel] * kBaseColorScale),
                    settings.effectGamma);
            } else {
                base = std::pow(
                    std::abs(kBaseColor[channel]),
                    settings.effectGamma);
                if (contract.vertexColored()) {
                    base *= std::pow(
                        std::abs(kVertexColor[channel]),
                        settings.effectGamma);
                }
                if (contract.textured()) {
                    base *= std::pow(
                        std::abs(kTextureColor[channel]),
                        settings.effectGamma);
                }
            }
            const auto property = contract.lighting() ?
                expectedLightingColor(eyeIndex, &settings)[channel] :
                std::pow(
                    std::abs(kPropertyColor[channel]),
                    settings.effectGamma);
            const auto lightColor =
                (base + kLightingInfluence * (property * base - base)) *
                settings.otherEffectMultiplier;
            if (contract.additive()) {
                result[channel] = lightColor * (1.0F - fogFactor);
            } else if (contract.multiplyBlend()) {
                const auto multiplyFogFactor =
                    std::clamp(1.5F * fogFactor, 0.0F, 1.0F);
                const auto foggedColor = lightColor +
                    multiplyFogFactor * (1.0F - lightColor);
                result[channel] =
                    1.0F + outputAlpha * (foggedColor - 1.0F);
            } else {
                const auto fogColor =
                    std::pow(std::abs(kFogParam[channel]), settings.fogGamma);
                result[channel] = lightColor +
                    fogFactor * (fogColor - lightColor);
            }
            if (contract.pipboy()) {
                result[channel] += pipboyColor[channel] *
                    pipboyControls[3] * settings.otherEffectMultiplier;
                if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                    result[channel] *= kBaseColor[3];
                }
            }
            if (contract.premultipliedAlpha()) {
                result[channel] *= outputAlpha;
            }
        }
        result[3] = outputAlpha;
        return result;
    }

    int run(const std::filesystem::path& root)
    {
        D3D_FEATURE_LEVEL featureLevel{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &featureLevel,
                context.GetAddressOf()),
            "D3D11CreateDevice(WARP)");
        if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
            throw std::runtime_error("WARP did not provide feature level 11");
        }

        EffectPerTechnique techniqueConstants{};
        techniqueConstants.depthParameters = kDepthParameters;
        techniqueConstants.uiMaskControls = kUIMaskControls;
        techniqueConstants.uiMaskRectangles[0] = kUIMaskRectangle;
        techniqueConstants.uiMaskColor = kUIMaskColor;
        auto linearUIMaskTechniqueConstants = techniqueConstants;
        linearUIMaskTechniqueConstants.uiMaskControls[2] = 1.0F;
        EffectPerMaterial materialConstants{};
        materialConstants.baseColor = kBaseColor;
        materialConstants.baseColorScale[0] = kBaseColorScale;
        materialConstants.lightingInfluence[0] = kLightingInfluence;
        materialConstants.lightingInfluence[1] = kSoftDepthScale;
        materialConstants.depthTestParameters[0] = 1.0F;
        EffectPerGeometry geometryConstants{};
        geometryConstants.pointLightPositionX = kPointLightPositionX;
        geometryConstants.pointLightPositionY = kPointLightPositionY;
        geometryConstants.pointLightPositionZ = kPointLightPositionZ;
        geometryConstants.spotLightDirectionX = kSpotLightDirectionX;
        geometryConstants.spotLightDirectionY = kSpotLightDirectionY;
        geometryConstants.spotLightDirectionZ = kSpotLightDirectionZ;
        geometryConstants.pipboyControls = kPipboyControlsConverted;
        geometryConstants.spotLightExponent = kSpotLightExponent;
        geometryConstants.spotLightCosHalfAngle = kSpotLightCosHalfAngle;
        geometryConstants.pointLightInverseRadius = kPointLightInverseRadius;
        geometryConstants.pointLightColorR = kPointLightColorR;
        geometryConstants.pointLightColorG = kPointLightColorG;
        geometryConstants.pointLightColorB = kPointLightColorB;
        geometryConstants.directionalLightColor = kDirectionalLightColor;
        geometryConstants.propertyColor = kPropertyColor;
        geometryConstants.alphaTest = { 0.001F, 0.8F, 0.0F, 1.0F };
        auto rawPipboyGeometryConstants = geometryConstants;
        rawPipboyGeometryConstants.pipboyControls = kPipboyControlsRaw;
        rawPipboyGeometryConstants.alphaTest[3] = 0.0F;
        const auto techniqueBuffer =
            createConstantBuffer(device.Get(), techniqueConstants);
        const auto linearUIMaskTechniqueBuffer =
            createConstantBuffer(device.Get(), linearUIMaskTechniqueConstants);
        const auto materialBuffer =
            createConstantBuffer(device.Get(), materialConstants);
        const auto geometryBuffer =
            createConstantBuffer(device.Get(), geometryConstants);
        const auto rawPipboyGeometryBuffer =
            createConstantBuffer(device.Get(), rawPipboyGeometryConstants);

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        enabledSettings.effectGamma = 1.65F;
        enabledSettings.effectAlphaGamma = 1.3F;
        enabledSettings.lightGamma = 1.7F;
        enabledSettings.fogGamma = 1.85F;
        enabledSettings.fogAlphaGamma = 1.45F;
        enabledSettings.pointLightMultiplier = 0.85F;
        enabledSettings.effectLightingMultiplier = 0.4F;
        enabledSettings.otherEffectMultiplier = 1.25F;
        const FrameData disabledFrame =
            makeFrameData(disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame =
            makeFrameData(enabledSettings, true, false, 1.0F);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);

        const auto texture = createTexture(device.Get(), kTextureColor);
        const auto depthTexture = createTexture(device.Get(), kDepthTexture);
        const auto depthTestTexturePass =
            createTexture(device.Get(), kDepthTestTexturePass);
        const auto depthTestTextureFail =
            createTexture(device.Get(), kDepthTestTextureFail);
        const auto grayscaleTexture = createGrayscaleTexture(device.Get());
        const auto pipboyTexture = createTexture(device.Get(), kPipboyTexture);
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device->CreateSamplerState(
                &samplerDescription, sampler.GetAddressOf()),
            "CreateSamplerState");

        const auto defaultVertexShader =
            createVertexShader(
                device.Get(), false, false, false, false, false, false);
        const auto vertexColorVertexShader =
            createVertexShader(
                device.Get(), true, false, false, false, false, false);
        const auto particleVertexShader =
            createVertexShader(
                device.Get(), false, true, false, false, false, false);
        const auto vertexColorParticleVertexShader =
            createVertexShader(
                device.Get(), true, true, false, false, false, false);
        const auto depthTestVertexShader =
            createVertexShader(
                device.Get(), false, false, true, false, false, false);
        const auto pipboyVertexShader =
            createVertexShader(
                device.Get(), false, false, false, true, false, false);
        const auto vertexColorPipboyVertexShader =
            createVertexShader(
                device.Get(), true, false, false, true, false, false);
        const auto lightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, false);
        const auto rightEyeLightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, true);
        const auto vertexColorLightingVertexShader = createVertexShader(
            device.Get(), true, false, false, false, true, false);
        const auto rightEyeVertexColorLightingVertexShader = createVertexShader(
            device.Get(), true, false, false, false, true, true);
        const auto particleLightingVertexShader = createVertexShader(
            device.Get(), false, true, false, false, true, false);
        const auto rightEyeParticleLightingVertexShader = createVertexShader(
            device.Get(), false, true, false, false, true, true);
        const auto vertexColorParticleLightingVertexShader = createVertexShader(
            device.Get(), true, true, false, false, true, false);
        const auto rightEyeVertexColorParticleLightingVertexShader =
            createVertexShader(
                device.Get(), true, true, false, false, true, true);
        const auto depthTestLightingVertexShader = createVertexShader(
            device.Get(), false, false, true, false, true, false);
        const auto rightEyeDepthTestLightingVertexShader = createVertexShader(
            device.Get(), false, false, true, false, true, true);
        const auto selectVertexShader = [&](const EffectContract& contract,
                                            bool rightEye) {
            if (contract.lighting()) {
                if (contract.depthTested()) {
                    return rightEye ?
                        rightEyeDepthTestLightingVertexShader.Get() :
                        depthTestLightingVertexShader.Get();
                }
                if (contract.needsParticleData()) {
                    if (contract.vertexColored()) {
                        return rightEye ?
                            rightEyeVertexColorParticleLightingVertexShader.Get() :
                            vertexColorParticleLightingVertexShader.Get();
                    }
                    return rightEye ?
                        rightEyeParticleLightingVertexShader.Get() :
                        particleLightingVertexShader.Get();
                }
                if (contract.vertexColored()) {
                    return rightEye ?
                        rightEyeVertexColorLightingVertexShader.Get() :
                        vertexColorLightingVertexShader.Get();
                }
                return rightEye ?
                    rightEyeLightingVertexShader.Get() :
                    lightingVertexShader.Get();
            }
            if (contract.depthTested()) {
                return depthTestVertexShader.Get();
            }
            if (contract.pipboy()) {
                return contract.vertexColored() ?
                    vertexColorPipboyVertexShader.Get() :
                    pipboyVertexShader.Get();
            }
            if (contract.needsParticleData()) {
                return contract.vertexColored() ?
                    vertexColorParticleVertexShader.Get() :
                    particleVertexShader.Get();
            }
            return contract.vertexColored() ?
                vertexColorVertexShader.Get() :
                defaultVertexShader.Get();
        };
        bool passed = true;
        for (const auto& contract : kEffectContracts) {
            auto* vertexShader = selectVertexShader(contract, false);
            const auto vanillaShader = createPixelShader(
                device.Get(),
                root /
                    "package/Shaders/Community/VerifiedEffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));
            const auto replacementShader = createPixelShader(
                device.Get(),
                root / "package/Shaders/Community/EffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));

            const auto vanilla = render(
                device.Get(), context.Get(), vertexShader,
                vanillaShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
            const auto disabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
            const auto enabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), enabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());

            const std::string label = contract.name;
            passed &= compare(
                vanilla,
                expectedVanilla(contract),
                label + " vanilla model");
            passed &= compare(
                disabled,
                vanilla,
                label + " disabled parity");
            passed &= compare(
                enabled,
                expectedEnabled(contract, enabledSettings),
                label + " enabled model");
            if (contract.lighting()) {
                auto* rightEyeVertexShader =
                    selectVertexShader(contract, true);
                const auto rightEyeVanilla = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto rightEyeDisabled = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto rightEyeEnabled = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                passed &= compare(
                    rightEyeVanilla,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        false,
                        1),
                    label + " right-eye vanilla model");
                passed &= compare(
                    rightEyeDisabled,
                    rightEyeVanilla,
                    label + " right-eye disabled parity");
                passed &= compare(
                    rightEyeEnabled,
                    expectedEnabled(
                        contract,
                        enabledSettings,
                        kPipboyControlsConverted,
                        true,
                        false,
                        1),
                    label + " right-eye enabled model");
            }
            if (contract.uiMaskRects()) {
                const auto vanillaLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto disabledLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto enabledLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                passed &= compare(
                    vanillaLinearColor,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        true),
                    label + " linear UI-mask color vanilla model");
                passed &= compare(
                    disabledLinearColor,
                    vanillaLinearColor,
                    label + " linear UI-mask color disabled parity");
                passed &= compare(
                    enabledLinearColor,
                    expectedEnabled(
                        contract,
                        enabledSettings,
                        kPipboyControlsConverted,
                        true,
                        true),
                    label + " linear UI-mask color enabled model");
            }
            if (contract.depthTested()) {
                const auto vanillaDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTextureFail.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto replacementDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTextureFail.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                constexpr Pixel discarded{};
                passed &= compare(
                    vanillaDiscard,
                    discarded,
                    label + " vanilla depth discard");
                passed &= compare(
                    replacementDiscard,
                    vanillaDiscard,
                    label + " disabled depth discard parity");
            }
            if (contract.pipboy()) {
                const auto vanillaRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto disabledRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                const auto enabledRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get());
                passed &= compare(
                    vanillaRawPipboy,
                    expectedVanilla(
                        contract,
                        kPipboyControlsRaw,
                        false),
                    label + " raw Pipboy vanilla model");
                passed &= compare(
                    disabledRawPipboy,
                    vanillaRawPipboy,
                    label + " raw Pipboy disabled parity");
                passed &= compare(
                    enabledRawPipboy,
                    expectedEnabled(
                        contract,
                        enabledSettings,
                        kPipboyControlsRaw,
                        false),
                    label + " raw Pipboy enabled model");
            }
        }
        if (!passed) {
            return 1;
        }
        std::cout <<
            "All 205 Effect Linear Lighting parity and enabled model tests passed.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr <<
            "usage: EffectLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "Effect Linear Lighting parity test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
