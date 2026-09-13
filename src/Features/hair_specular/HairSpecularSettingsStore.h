#pragma once

#include "Features/hair_specular/HairSpecularSettings.h"

#include <filesystem>

namespace csah::hair_specular
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
