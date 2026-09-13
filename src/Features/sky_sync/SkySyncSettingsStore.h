#pragma once

#include "Features/sky_sync/SkySyncSettings.h"

#include <filesystem>

namespace csah::sky_sync
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
