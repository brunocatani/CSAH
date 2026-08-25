#pragma once

#include <cstdint>

namespace community_shaders::vanilla_fixes
{
    enum class DirectionalLightDiagnosticMode : std::uint8_t
    {
        off = 0,
        normalLightDot = 1,
        normalViewDot = 2,
        shadowVisibility = 3,
        suppliedLightVector = 4,
        viewLightVector = 5,
        correctedNormalLightDot = 6,
        coverageNoDepthStencil = 7,
        coverageFullRaster = 8,
        coverageSyntheticProducer = 9,
        coverageSyntheticComposite = 10,
        finalOutputPresentation = 11,
    };

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
        DirectionalLightDiagnosticMode directionalLightDiagnosticMode{
            DirectionalLightDiagnosticMode::off
        };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
}
