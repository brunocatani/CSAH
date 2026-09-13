#pragma once

#include "Features/contact_shadows/ContactShadowSettings.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace csah::contact_shadows
{
    [[nodiscard]] std::optional<bool> parseBoolean(
        std::wstring_view text) noexcept;
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
