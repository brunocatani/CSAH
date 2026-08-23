#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    enum class DirectionalDiagnosticChannel : std::uint8_t
    {
        normalLightDot,
        normalViewDot,
        shadowVisibility,
    };

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

    // Exclusive diagnostic transformation for the same four exact
    // DFComposite permutations. It publishes one channel from the private
    // directional-light payload at t5 in its assigned diagnostic colour and
    // prevents albedo,
    // ambient, point lights, emissive, cubemaps, SSR, IBL, and material
    // modulation from contributing to the final composite output.
    [[nodiscard]] bool patchStockReflectionCompositeDirectionalDiagnostic(
        std::span<const std::byte> stockBytecode,
        DirectionalDiagnosticChannel channel,
        std::vector<std::byte>& patchedBytecode) noexcept;
}
