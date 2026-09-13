#pragma once

#include "Features/complex_materials/ComplexParallaxSettings.h"

#include <filesystem>

namespace csah::complex_materials
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
