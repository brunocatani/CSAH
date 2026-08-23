#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    // Replaces only the final outputs of FO4VR's exact active deferred
    // directional-light pixel shader with an RGB ownership payload:
    // red = saturated N.L, green = saturated N.V, blue = final cascaded-shadow
    // visibility. Receiver reconstruction, normal decoding, BRDF evaluation,
    // cascade selection, PCF, split blending, radial confidence, atlas
    // ownership, and per-eye inputs remain byte-for-byte stock. Target zero is
    // divided by three because DFComposite restores that accumulation scale;
    // target one is not bound during the private diagnostic draw, so no
    // specular path can contaminate the payload.
    [[nodiscard]] bool patchStockDirectionalLightOwnershipDiagnostic(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
