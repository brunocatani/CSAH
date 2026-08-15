#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    // Production transformation for the four exact active FO4VR DFComposite
    // permutations.
    // It reloads the packed-X-selected ordinary inverse projection after the
    // stock shader has destroyed its original register, reconstructs the live
    // eye-relative surface, and shifts it to the midpoint of the two captured
    // eye origins. The decoded G-buffer normal, shared inverse-view rotation,
    // array slice, LOD, cube sampling, and color processing remain active. The
    // IBL generator applies this transform before adding its own declarations
    // and response logic, so both corrections retain a single shader owner.
    [[nodiscard]] bool patchStockReflectionCompositeSurfaceAnchoredCubemap(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
