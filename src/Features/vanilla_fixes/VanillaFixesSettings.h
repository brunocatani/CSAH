#pragma once

namespace community_shaders::vanilla_fixes
{
    struct Settings final
    {
        bool enabled{ true };
        bool precipitationOcclusion{ true };
        bool imageSpaceModifiers{ true };
        bool sao{ true };
        bool screenSpaceReflections{ true };
        bool screenSpaceSubsurfaceScattering{ true };
        bool lensFlare{ true };
        bool focusShadows{ true };
        bool sunbeams{ true };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
}
