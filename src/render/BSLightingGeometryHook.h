#pragma once

#include <cstdint>

namespace community_shaders::render
{
    enum class GeometryWalkStage : std::uint32_t
    {
        none = 0,
        renderPass = 1,
        geometryLink = 2,
        geometry = 3,
        property = 4,
        emissiveMultiplier = 5,
    };

    struct GeometryHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        std::uint64_t calls{};
        std::uint64_t acceptedUpdates{};
        std::uint64_t rejectedWalks{};
        GeometryWalkStage deepestStage{};
        float lastSourceEmissiveMultiplier{};
    };

    // Installs a process-lifetime patch on the verified Fallout4VR.exe 1.2.72
    // active VR-extended surface-lighting vtable. The exact cell target and
    // function bytes are checked before the write. No flat-FO4 address or
    // CommonLib relocation is used here.
    [[nodiscard]] bool installBSLightingGeometryHook() noexcept;
    [[nodiscard]] GeometryHookSnapshot geometryHookSnapshot() noexcept;
}
