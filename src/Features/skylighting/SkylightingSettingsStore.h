#pragma once

#include "Features/skylighting/SkylightingSettings.h"

#include <filesystem>

namespace csah::skylighting
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
