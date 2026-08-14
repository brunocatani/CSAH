#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"

#include <filesystem>

namespace community_shaders::basic_wetness
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
