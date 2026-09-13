#pragma once

#include "Features/dlaa/DlaaSettings.h"

#include <filesystem>

namespace csah::dlaa
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
