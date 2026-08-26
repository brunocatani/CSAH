#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace community_shaders::diagnostics_settings
{
    inline constexpr auto kSection = L"Diagnostics";
    inline constexpr auto kGpuProfilingModeKey = L"iGpuProfilingMode";

    struct Settings final
    {
        std::uint32_t gpuProfilingGroups{};

        [[nodiscard]] bool operator==(
            const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings load(
        const std::filesystem::path& path) noexcept
    {
        if (path.empty()) {
            return {};
        }
        const auto value = static_cast<int>(GetPrivateProfileIntW(
            kSection,
            kGpuProfilingModeKey,
            0,
            path.c_str()));
        return {
            .gpuProfilingGroups = static_cast<std::uint32_t>(
                std::clamp(value, 0, 3)),
        };
    }
}
