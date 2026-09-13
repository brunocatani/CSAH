#pragma once

#include "Features/pbr/PbrSettings.h"

#include <filesystem>

namespace csah::pbr
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
