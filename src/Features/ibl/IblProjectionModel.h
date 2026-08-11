#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace community_shaders::ibl
{
    enum class DiffuseSHState
    {
        invalid,
        black,
        usable,
    };

    struct DiffuseSH
    {
        std::array<std::array<float, 4>, 3> rgb{};
    };

    static_assert(sizeof(DiffuseSH) == sizeof(float) * 12);

    [[nodiscard]] inline bool validDiffuseSH(
        const DiffuseSH& coefficients) noexcept
    {
        constexpr float kMaximumMagnitude = 1.0e6f;
        for (const auto& channel : coefficients.rgb) {
            for (const auto coefficient : channel) {
                if (!std::isfinite(coefficient) ||
                    std::abs(coefficient) > kMaximumMagnitude) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] inline DiffuseSHState classifyDiffuseSH(
        const DiffuseSH& coefficients) noexcept
    {
        if (!validDiffuseSH(coefficients)) {
            return DiffuseSHState::invalid;
        }

        // The source texture is unsigned R11G11B10_FLOAT radiance. Its L0
        // terms therefore cannot be materially negative, and a first-order
        // directional term cannot exceed sqrt(3) times L0 for non-negative
        // radiance. A small margin covers finite sampling error.
        constexpr float kNumericalTolerance = 1.0e-5f;
        constexpr float kMaximumDirectionalRatio = 1.75f;
        constexpr float kMinimumUsableIntegratedRadiance = 1.0e-5f;
        float integratedRadiance{};
        for (const auto& channel : coefficients.rgb) {
            const auto l0 = channel[0];
            if (l0 < -kNumericalTolerance) {
                return DiffuseSHState::invalid;
            }
            const auto nonNegativeL0 = std::max(0.0f, l0);
            const auto directionalLimit = std::max(
                kNumericalTolerance,
                nonNegativeL0 * kMaximumDirectionalRatio);
            for (std::size_t coefficient = 1; coefficient < channel.size();
                 ++coefficient) {
                if (std::abs(channel[coefficient]) > directionalLimit) {
                    return DiffuseSHState::invalid;
                }
            }
            integratedRadiance += nonNegativeL0;
        }
        return integratedRadiance > kMinimumUsableIntegratedRadiance ?
            DiffuseSHState::usable :
            DiffuseSHState::black;
    }
}
