#pragma once

#include "Features/subsurface_scattering/SubsurfaceScatteringSettings.h"

#include <filesystem>

namespace community_shaders::subsurface_scattering
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
