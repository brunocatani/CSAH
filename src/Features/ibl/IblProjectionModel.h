#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace community_shaders::ibl
{
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
}
