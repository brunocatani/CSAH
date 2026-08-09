#pragma once

#include <cstdint>

namespace community_shaders::render
{
    enum class GeometryWalkStage : std::uint32_t
    {
        none = 0,
        pass = 1,
        geometry = 2,
        property = 3,
        emissiveMultiplier = 4,
    };

    struct GeometryHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        std::uint64_t calls{};
        std::uint64_t acceptedUpdates{};
        std::uint64_t rejectedWalks{};
        GeometryWalkStage deepestStage{};
    };

    // Installs a process-lifetime patch on the verified Fallout4VR.exe 1.2.72
    // surface-lighting vtable. The exact cell target and function bytes are
    // checked before the write. No flat-FO4 address or CommonLib relocation is
    // used here.
    [[nodiscard]] bool installBSLightingGeometryHook() noexcept;
    [[nodiscard]] GeometryHookSnapshot geometryHookSnapshot() noexcept;
}
