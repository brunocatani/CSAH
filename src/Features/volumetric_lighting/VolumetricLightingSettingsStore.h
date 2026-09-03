#pragma once

#include "Features/volumetric_lighting/VolumetricLightingSettings.h"

#include <filesystem>

namespace community_shaders::volumetric_lighting
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
