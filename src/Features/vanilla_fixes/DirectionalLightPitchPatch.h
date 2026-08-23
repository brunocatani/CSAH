#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    // Removes only FO4VR's high-power peripheral radial confidence blend from
    // the exact active deferred directional-light pixel shader. The incoming
    // evaluated cascade result is retained directly. Cascade selection, PCF,
    // split blending, final-cascade cutoff, atlas ownership, per-eye light
    // data, normals, and BRDF lighting remain byte-for-byte stock.
    [[nodiscard]] bool patchStockDirectionalLightRadialFade(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
