#pragma once

#include "Features/wrapped_grass/WrappedGrassSettings.h"

#include <filesystem>

namespace community_shaders::wrapped_grass
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
