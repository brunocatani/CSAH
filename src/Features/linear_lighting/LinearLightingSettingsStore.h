#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

#include <filesystem>

namespace csah::linear_lighting
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;

    // Updates owned keys individually and preserves unrelated INI content.
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
