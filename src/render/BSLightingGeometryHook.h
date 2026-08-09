#pragma once

#include <cstdint>

namespace community_shaders::render
{
    enum class GeometrySourceStage : std::uint32_t
    {
        none = 0,
        lightingState = 1,
        emissiveMultiplier = 2,
    };

    struct GeometryHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        std::uint64_t calls{};
        std::uint64_t acceptedUpdates{};
        std::uint64_t rejectedSources{};
        GeometrySourceStage deepestStage{};
        float lastSourceEmissiveMultiplier{};
    };

    // Installs a process-lifetime patch on the verified Fallout4VR.exe 1.2.72
    // active VR-extended surface-lighting vtable. The exact cell target and
    // function bytes are checked before the write. No flat-FO4 address or
    // CommonLib relocation is used here.
    [[nodiscard]] bool installBSLightingGeometryHook() noexcept;
    [[nodiscard]] GeometryHookSnapshot geometryHookSnapshot() noexcept;
}
