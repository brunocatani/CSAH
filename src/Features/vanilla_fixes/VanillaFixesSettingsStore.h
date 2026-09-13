#pragma once

#include "Features/vanilla_fixes/VanillaFixesSettings.h"

#include <filesystem>

namespace csah::vanilla_fixes
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
