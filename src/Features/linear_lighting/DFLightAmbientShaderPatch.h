#pragma once

#include "Features/linear_lighting/DxbcChecksum.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace community_shaders::linear_lighting
{
    constexpr float kVanillaDFLightAmbientShaderGamma = 2.2f;
    constexpr std::size_t kDFLightAmbientGammaFloatCount = 6;
    constexpr std::size_t kDirectionalAmbientTransformFloatCount = 16;

    using DFLightAmbientGammaOffsets =
        std::array<std::uint32_t, kDFLightAmbientGammaFloatCount>;
    using DirectionalAmbientTransform =
        std::array<float, kDirectionalAmbientTransformFloatCount>;

    [[nodiscard]] inline bool patchDFLightAmbientGamma(
        std::span<std::byte> bytecode,
        const DFLightAmbientGammaOffsets& offsets,
        float gamma) noexcept
    {
        if (!std::isfinite(gamma) || gamma <= 0.0f) {
            return false;
        }

        constexpr auto vanillaBits = std::bit_cast<std::uint32_t>(
            kVanillaDFLightAmbientShaderGamma);
        const auto replacementBits = std::bit_cast<std::uint32_t>(gamma);
        for (const auto offset : offsets) {
            if (offset > bytecode.size() ||
                sizeof(std::uint32_t) > bytecode.size() - offset) {
                return false;
            }
            std::uint32_t observed{};
            std::memcpy(&observed, bytecode.data() + offset, sizeof(observed));
            if (observed != vanillaBits) {
                return false;
            }
        }
        for (const auto offset : offsets) {
            std::memcpy(
                bytecode.data() + offset,
                &replacementBits,
                sizeof(replacementBits));
        }
        return recomputeDxbcChecksum(bytecode);
    }

    [[nodiscard]] inline float directionalAmbientInputScale(
        float ambientMultiplier,
        float ambientGamma) noexcept
    {
        if (!std::isfinite(ambientMultiplier) || ambientMultiplier < 0.0f ||
            !std::isfinite(ambientGamma) || ambientGamma <= 0.0f) {
            return 1.0f;
        }
        const auto scale = std::pow(
            ambientMultiplier,
            1.0f / ambientGamma);
        return std::isfinite(scale) ? scale : 1.0f;
    }

    inline void scaleDirectionalAmbientTransform(
        DirectionalAmbientTransform& transform,
        float scale) noexcept
    {
        // FO4VR's six-color producer stores three RGB half-difference basis
        // vectors at rows 0..2 and the averaged/base RGB vector in row 3.
        // Each row's fourth component is padding except +0x3C, which is the
        // engine transform scale and must remain unchanged. Scaling the 12
        // RGB coefficients before the shader's pow is exactly equivalent to
        // multiplying its positive-domain result by scale^gamma.
        constexpr std::array<std::size_t, 12> coefficientIndices{
            0, 1, 2,
            4, 5, 6,
            8, 9, 10,
            12, 13, 14,
        };
        for (const auto index : coefficientIndices) {
            transform[index] *= scale;
        }
    }
}
