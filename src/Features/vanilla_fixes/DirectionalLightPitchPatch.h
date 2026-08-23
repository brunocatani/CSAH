#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    // Removes only FO4VR's camera-forward final-cascade eligibility plane
    // from the exact active deferred directional-light pixel shader. The
    // comparison remains present, but its immediately following branch is
    // made unconditionally true. Cascade selection, PCF, split blending,
    // radial distance fade, atlas ownership, and per-eye light data remain
    // byte-for-byte stock.
    [[nodiscard]] bool patchStockDirectionalLightPitchCutoff(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
