#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"
#include "Features/bloom_glare/BloomGlareSettings.h"
#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "Features/complex_materials/ComplexParallaxSettings.h"
#include "Features/contact_shadows/ContactShadowSettings.h"
#include "Features/dlaa/DlaaSettings.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"
#include "Features/hair_specular/HairSpecularSettings.h"
#include "Features/ibl/IblSettings.h"
#include "Features/linear_lighting/LinearLightingSettings.h"
#include "Features/native_shadows/NativeShadowSettings.h"
#include "Features/skylighting/SkylightingSettings.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettings.h"
#include "Features/vanilla_fixes/VanillaFixesSettings.h"
#include "Features/wrapped_grass/WrappedGrassSettings.h"

#include <cstddef>

namespace community_shaders::shared_settings
{
    struct Snapshot final
    {
        linear_lighting::Settings linearLighting{};
        dlaa::Settings dlaa{};
        filmic_tonemapping::Settings filmicTonemapping{};
        bloom_glare::Settings bloomGlare{};
        ibl::Settings ibl{};
        complex_materials::Settings complexMaterials{};
        contact_shadows::Settings contactShadows{};
        wrapped_grass::Settings wrappedGrass{};
        hair_specular::Settings hairSpecular{};
        subsurface_scattering::Settings subsurfaceScattering{};
        basic_wetness::Settings basicWetness{};
        cloud_shadows::Settings cloudShadows{};
        vanilla_fixes::Settings vanillaFixes{};
        native_shadows::Settings nativeShadows{};
        skylighting::Settings skylighting{};

        [[nodiscard]] bool operator==(const Snapshot&) const noexcept = default;
    };

    struct ChangeSet final
    {
        bool linearLighting{};
        bool dlaa{};
        bool filmicTonemapping{};
        bool bloomGlare{};
        bool ibl{};
        bool complexMaterials{};
        bool contactShadows{};
        bool wrappedGrass{};
        bool hairSpecular{};
        bool subsurfaceScattering{};
        bool basicWetness{};
        bool cloudShadows{};
        bool vanillaFixes{};
        bool nativeShadows{};
        bool skylighting{};

        [[nodiscard]] bool any() const noexcept
        {
            return liveFeatureCount() != 0 || nativeShadows;
        }

        [[nodiscard]] std::size_t liveFeatureCount() const noexcept
        {
            return static_cast<std::size_t>(linearLighting) +
                static_cast<std::size_t>(dlaa) +
                static_cast<std::size_t>(filmicTonemapping) +
                static_cast<std::size_t>(bloomGlare) +
                static_cast<std::size_t>(ibl) +
                static_cast<std::size_t>(complexMaterials) +
                static_cast<std::size_t>(contactShadows) +
                static_cast<std::size_t>(wrappedGrass) +
                static_cast<std::size_t>(hairSpecular) +
                static_cast<std::size_t>(subsurfaceScattering) +
                static_cast<std::size_t>(basicWetness) +
                static_cast<std::size_t>(cloudShadows) +
                static_cast<std::size_t>(vanillaFixes) +
                static_cast<std::size_t>(skylighting);
        }
    };

    [[nodiscard]] inline ChangeSet diff(
        const Snapshot& previous,
        const Snapshot& next) noexcept
    {
        return {
            .linearLighting =
                previous.linearLighting != next.linearLighting,
            .dlaa = previous.dlaa != next.dlaa,
            .filmicTonemapping = previous.filmicTonemapping !=
                next.filmicTonemapping,
            .bloomGlare = previous.bloomGlare != next.bloomGlare,
            .ibl = previous.ibl != next.ibl,
            .complexMaterials =
                previous.complexMaterials != next.complexMaterials,
            .contactShadows =
                previous.contactShadows != next.contactShadows,
            .wrappedGrass = previous.wrappedGrass != next.wrappedGrass,
            .hairSpecular = previous.hairSpecular != next.hairSpecular,
            .subsurfaceScattering = previous.subsurfaceScattering !=
                next.subsurfaceScattering,
            .basicWetness = previous.basicWetness != next.basicWetness,
            .cloudShadows = previous.cloudShadows != next.cloudShadows,
            .vanillaFixes = previous.vanillaFixes != next.vanillaFixes,
            .nativeShadows = previous.nativeShadows != next.nativeShadows,
            .skylighting = previous.skylighting != next.skylighting,
        };
    }

    // Starts one bounded monitor for the shared Community Shaders INI. The
    // controller is process-lifetime owned, performs no work on render hooks,
    // and coalesces multi-key writers before publishing settings.
    [[nodiscard]] bool startMonitor() noexcept;
}
