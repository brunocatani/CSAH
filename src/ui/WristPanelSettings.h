#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace community_shaders::ui::wrist_panel_settings
{
    inline constexpr bool kDefaultPrismaPanelEnabled = true;

    struct LoadResult
    {
        bool enabled{ kDefaultPrismaPanelEnabled };
        bool keyPresent{};
        bool valueValid{ true };
    };

    [[nodiscard]] std::optional<bool> parseBoolean(
        std::wstring_view text) noexcept;

    [[nodiscard]] LoadResult load(
        const std::filesystem::path& iniPath) noexcept;
}
