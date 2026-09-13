#pragma once

#include "Features/bloom_glare/BloomGlareSettings.h"

#include <filesystem>

namespace csah::bloom_glare
{
    [[nodiscard]] Settings loadSettings(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
    [[nodiscard]] bool saveSettings(const Settings& settings) noexcept;
}
