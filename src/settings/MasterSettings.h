#pragma once

#include <Windows.h>

#include <filesystem>

namespace community_shaders::master_settings
{
    inline constexpr auto kSection = L"CommunityShaders";
    inline constexpr auto kEnabledKey = L"bEnabled";

    [[nodiscard]] inline bool enabled(
        const std::filesystem::path& path) noexcept
    {
        if (path.empty()) {
            return true;
        }
        return GetPrivateProfileIntW(
                   kSection,
                   kEnabledKey,
                   1,
                   path.c_str()) != 0;
    }

    template <class Settings>
    [[nodiscard]] Settings gate(
        const std::filesystem::path& path,
        Settings settings) noexcept
    {
        if (!enabled(path)) {
            settings.enabled = false;
        }
        return settings;
    }
}
