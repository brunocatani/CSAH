#pragma once

#include <filesystem>

namespace community_shaders::settings_path
{
    [[nodiscard]] std::filesystem::path fromDocuments(
        const std::filesystem::path& documents) noexcept;

    [[nodiscard]] std::filesystem::path resolveIniPath() noexcept;

    [[nodiscard]] bool ensureParentDirectory(
        const std::filesystem::path& iniPath) noexcept;
}
