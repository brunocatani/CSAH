#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace community_shaders::complex_materials
{
    #include "Features/complex_materials/GeneratedComplexEnvironmentContracts.inl"

    static_assert(kComplexEnvironmentProducerByLinearContract.size() == 288);
    static_assert(kComplexEnvironmentShaderContractCount == 33);

    inline constexpr float kComplexMaskEpsilon = 4.0F / 255.0F;
    inline constexpr float kComplexMetalTagRange = 0.5F;
    inline constexpr float kMinimumRetainedDiffuse = 1.0F / 255.0F;

    struct EnvironmentMaskSample
    {
        float red{};
        float green{};
        float blue{};
        float alpha{};
    };

    [[nodiscard]] inline bool isComplexEnvironmentMask(
        const EnvironmentMaskSample& terminalMip,
        bool drawEnabled) noexcept
    {
        if (!drawEnabled ||
            !std::isfinite(terminalMip.red) ||
            !std::isfinite(terminalMip.green) ||
            !std::isfinite(terminalMip.blue) ||
            !std::isfinite(terminalMip.alpha) ||
            terminalMip.alpha >= 1.0F - kComplexMaskEpsilon) {
            return false;
        }
        const auto grayscale =
            std::abs(terminalMip.red - terminalMip.green) <
                kComplexMaskEpsilon &&
            std::abs(terminalMip.red - terminalMip.blue) <
                kComplexMaskEpsilon &&
            std::abs(terminalMip.green - terminalMip.blue) <
                kComplexMaskEpsilon;
        const auto solidBlackHeight =
            terminalMip.red < kComplexMaskEpsilon &&
            terminalMip.green < kComplexMaskEpsilon &&
            terminalMip.blue < kComplexMaskEpsilon &&
            terminalMip.alpha > kComplexMaskEpsilon &&
            terminalMip.alpha < 1.0F - kComplexMaskEpsilon;
        return !grayscale || solidBlackHeight;
    }

    [[nodiscard]] inline float encodeMetalnessTag(
        float metalness) noexcept
    {
        const auto safe = std::isfinite(metalness) ?
            std::clamp(metalness, 0.0F, 1.0F) :
            0.0F;
        return 1.0F - kComplexMetalTagRange * safe;
    }

    [[nodiscard]] inline float decodeMetalnessTag(
        float encoded) noexcept
    {
        if (!std::isfinite(encoded)) {
            return 0.0F;
        }
        return std::clamp(
            (1.0F - encoded) / kComplexMetalTagRange,
            0.0F,
            1.0F);
    }

    [[nodiscard]] inline float retainedDiffuseScale(
        float metalness) noexcept
    {
        const auto safe = std::isfinite(metalness) ?
            std::clamp(metalness, 0.0F, 1.0F) :
            0.0F;
        return (std::max)(1.0F - safe, kMinimumRetainedDiffuse);
    }
}
