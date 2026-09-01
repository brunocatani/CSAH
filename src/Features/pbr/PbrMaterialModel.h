#pragma once

#include "Features/pbr/PbrSettings.h"

#include <algorithm>
#include <cmath>

namespace community_shaders::pbr
{
    constexpr float kMinimumPerceptualRoughness = 0.04f;
    constexpr float kMaximumKernelRoughnessSquared = 0.18f;

    [[nodiscard]] inline float filterRoughness(
        float roughness,
        float normalDerivativeXLengthSquared,
        float normalDerivativeYLengthSquared) noexcept
    {
        const auto safeRoughness = std::isfinite(roughness) ?
            std::clamp(roughness, kMinimumPerceptualRoughness, 1.0f) :
            1.0f;
        const auto derivativeX =
            std::isfinite(normalDerivativeXLengthSquared) ?
            (std::max)(normalDerivativeXLengthSquared, 0.0f) : 0.0f;
        const auto derivativeY =
            std::isfinite(normalDerivativeYLengthSquared) ?
            (std::max)(normalDerivativeYLengthSquared, 0.0f) : 0.0f;
        const auto variance = 0.5f * (derivativeX + derivativeY);
        const auto kernelRoughnessSquared = (std::min)(
            2.0f * variance,
            kMaximumKernelRoughnessSquared);
        return std::clamp(
            std::sqrt(
                safeRoughness * safeRoughness +
                kernelRoughnessSquared),
            kMinimumPerceptualRoughness,
            1.0f);
    }

    [[nodiscard]] inline float decodeComplexMetalness(
        float encodedMaterialTag) noexcept
    {
        if (!std::isfinite(encodedMaterialTag) ||
            encodedMaterialTag < 0.5f) {
            return 0.0f;
        }
        return std::clamp((1.0f - encodedMaterialTag) * 2.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float phongEncodedToRoughness(
        float encodedShininess) noexcept
    {
        if (!std::isfinite(encodedShininess)) {
            return 1.0f;
        }
        const auto exponent = std::exp2(
            std::clamp(encodedShininess, 0.0f, 1.0f) * 10.0f + 1.0f);
        return std::clamp(
            std::sqrt(std::sqrt(2.0f / (exponent + 2.0f))),
            kMinimumPerceptualRoughness,
            1.0f);
    }

    [[nodiscard]] inline float materialRoughness(
        float environmentLod,
        float encodedShininess,
        float encodedGlossiness,
        const Settings& settings,
        float maximumEnvironmentLod = 7.0f) noexcept
    {
        const auto safe = sanitize(settings);
        const auto lodRoughness = std::isfinite(environmentLod) &&
                std::isfinite(maximumEnvironmentLod) &&
                maximumEnvironmentLod > 0.0f ?
            std::clamp(environmentLod / maximumEnvironmentLod, 0.0f, 1.0f) :
            1.0f;
        const auto converted = phongEncodedToRoughness(encodedShininess);
        const auto glossiness = std::isfinite(encodedGlossiness) ?
            std::clamp(encodedGlossiness, 0.0f, 1.0f) : 0.0f;
        const auto conversionWeight = std::clamp(
            safe.specularRoughnessBlend * (1.0f - glossiness),
            0.0f,
            1.0f);
        return std::clamp(
            std::lerp(
                converted,
                lodRoughness,
                conversionWeight) *
                safe.roughnessMultiplier,
            kMinimumPerceptualRoughness,
            1.0f);
    }

    [[nodiscard]] inline float dielectricF0(
        float encodedSpecular,
        const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        const auto specular = std::isfinite(encodedSpecular) ?
            std::clamp(encodedSpecular, 0.0f, 1.0f) : 0.0f;
        return std::clamp(
            (std::max)(
                safe.minimumF0,
                specular * safe.baseF0Multiplier *
                    safe.cubemapToF0Multiplier),
            0.0f,
            1.0f);
    }
}
