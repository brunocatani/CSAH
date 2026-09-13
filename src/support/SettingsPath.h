#pragma once

#include <filesystem>
#include <system_error>

namespace csah::settings_path
{
    [[nodiscard]] std::filesystem::path fromDocuments(
        const std::filesystem::path& documents) noexcept;

    [[nodiscard]] std::filesystem::path resolveIniPath() noexcept;

    enum class MigrationResult { notNeeded, migrated, failed };

    [[nodiscard]] MigrationResult migrateLegacyIni(
        const std::filesystem::path& iniPath,
        std::error_code& error) noexcept;

    [[nodiscard]] bool ensureParentDirectory(
        const std::filesystem::path& iniPath) noexcept;
}
