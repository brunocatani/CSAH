#pragma once

#include "Features/cloud_shadows/CloudShadowSettings.h"

#include <filesystem>

namespace csah::cloud_shadows
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
