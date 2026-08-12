#pragma once

#include "Features/ibl/IblSettings.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace community_shaders::ibl
{
    [[nodiscard]] std::optional<bool> parseBoolean(
        std::wstring_view text) noexcept;
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;

    // Updates only the ImageBasedLighting-owned key and preserves every
    // unrelated INI section and value.
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
