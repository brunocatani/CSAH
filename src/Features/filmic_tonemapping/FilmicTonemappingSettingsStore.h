#pragma once

#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"

#include <filesystem>

namespace csah::filmic_tonemapping
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
