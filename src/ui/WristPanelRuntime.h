#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"
#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "Features/complex_materials/ComplexParallaxSettings.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"
#include "Features/hair_specular/HairSpecularSettings.h"
#include "Features/linear_lighting/LinearLightingSettings.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettings.h"
#include "Features/vanilla_fixes/VanillaFixesSettings.h"
#include "Features/wrapped_grass/WrappedGrassSettings.h"

namespace community_shaders::ui
{
    // Stores the startup settings before the optional provider/UI layer exists.
    void setInitialSettings(
        const linear_lighting::Settings& settings) noexcept;
    void setInitialFilmicTonemappingSettings(
        const filmic_tonemapping::Settings& settings) noexcept;
    void setInitialComplexParallaxSettings(
        const complex_materials::Settings& settings) noexcept;
    void setInitialWrappedGrassSettings(
        const wrapped_grass::Settings& settings) noexcept;
    void setInitialHairSpecularSettings(
        const hair_specular::Settings& settings) noexcept;
    void setInitialSubsurfaceScatteringSettings(
        const subsurface_scattering::Settings& settings) noexcept;
    void setInitialBasicWetnessSettings(
        const basic_wetness::Settings& settings) noexcept;
    void setInitialCloudShadowSettings(
        const cloud_shadows::Settings& settings) noexcept;
    void setInitialVanillaFixesSettings(
        const vanilla_fixes::Settings& settings) noexcept;

    // Called by the shared-INI controller after an external writer changes
    // runtime state. Prisma work is always marshalled onto the game task queue.
    void notifyExternalSettingsReload() noexcept;
    void schedulePrismaPanelSettingReload() noexcept;

    // Optional initialization. Missing PrismaUI_F4 or ROCK leaves the renderer
    // and INI owner fully operational without a compatibility fallback.
    void onGameDataReady() noexcept;
    void onGameSessionReady() noexcept;
    void shutdown() noexcept;
}
