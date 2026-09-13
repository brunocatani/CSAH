#pragma once

#include "Features/native_shadows/NativeShadowSettings.h"

#include <filesystem>

namespace csah::native_shadows
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
