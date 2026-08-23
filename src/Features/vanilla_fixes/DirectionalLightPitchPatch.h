#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    // Replaces only the final outputs of FO4VR's exact active deferred
    // directional-light pixel shader with an ownership diagnostic:
    // red = saturated N.L, green = saturated N.V, blue = final cascaded-shadow
    // visibility. Receiver reconstruction, normal decoding, BRDF evaluation,
    // cascade selection, PCF, split blending, radial confidence, atlas
    // ownership, and per-eye inputs remain byte-for-byte stock.
    [[nodiscard]] bool patchStockDirectionalLightOwnershipDiagnostic(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
