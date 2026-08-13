#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace community_shaders::complex_materials
{
    inline constexpr std::array<std::size_t, 3>
        kLandscapeMaterialContractIndices{ 273u, 274u, 275u };

    [[nodiscard]] constexpr std::size_t landscapeParallaxSlot(
        std::size_t materialContract) noexcept
    {
        for (std::size_t slot = 0;
             slot < kLandscapeMaterialContractIndices.size();
             ++slot) {
            if (kLandscapeMaterialContractIndices[slot] == materialContract) {
                return slot;
            }
        }
        return kLandscapeMaterialContractIndices.size();
    }

    [[nodiscard]] constexpr bool shouldUseLandscapeParallax(
        bool parallaxEnabled,
        bool resourcesReady,
        std::size_t materialContract) noexcept
    {
        return parallaxEnabled && resourcesReady &&
            landscapeParallaxSlot(materialContract) <
            kLandscapeMaterialContractIndices.size();
    }

    // The paired FO4VR landscape VS exports tangent-to-view rows. Transforming
    // a view-space ray back into tangent space therefore uses the transpose.
    [[nodiscard]] inline std::array<float, 3>
        transformViewDirectionToTangentSpace(
            const std::array<std::array<float, 3>, 3>& tangentToViewRows,
            const std::array<float, 3>& viewDirection) noexcept
    {
        return {
            viewDirection[0] * tangentToViewRows[0][0] +
                viewDirection[1] * tangentToViewRows[1][0] +
                viewDirection[2] * tangentToViewRows[2][0],
            viewDirection[0] * tangentToViewRows[0][1] +
                viewDirection[1] * tangentToViewRows[1][1] +
                viewDirection[2] * tangentToViewRows[2][1],
            viewDirection[0] * tangentToViewRows[0][2] +
                viewDirection[1] * tangentToViewRows[1][2] +
                viewDirection[2] * tangentToViewRows[2][2],
        };
    }

    [[nodiscard]] inline float depthFromLandscapeHeight(float height) noexcept
    {
        return 1.0f - std::clamp(height, 0.0f, 1.0f);
    }
}
