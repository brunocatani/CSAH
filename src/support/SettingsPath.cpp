#include "support/SettingsPath.h"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <system_error>

namespace csah::settings_path
{
    std::filesystem::path fromDocuments(
        const std::filesystem::path& documents) noexcept
    {
        try {
            if (documents.empty()) {
                return {};
            }
            return documents /
                L"My Games" /
                L"Fallout4VR" /
                L"Mods_Config" / L"CSAH" /
                L"CSAH.ini";
        } catch (...) {
            return {};
        }
    }

    std::filesystem::path resolveIniPath() noexcept
    {
        std::array<wchar_t, MAX_PATH> documents{};
        if (FAILED(SHGetFolderPathW(
                nullptr,
                CSIDL_MYDOCUMENTS,
                nullptr,
                SHGFP_TYPE_CURRENT,
                documents.data()))) {
            return {};
        }
        return fromDocuments(documents.data());
    }

    MigrationResult migrateLegacyIni(
        const std::filesystem::path& iniPath,
        std::error_code& error) noexcept
    {
        error.clear();
        try {
            if (iniPath.empty()) {
                error = std::make_error_code(std::errc::invalid_argument);
                return MigrationResult::failed;
            }
            if (std::filesystem::exists(iniPath, error)) {
                if (std::filesystem::is_regular_file(iniPath, error)) {
                    return MigrationResult::notNeeded;
                }
                if (!error) {
                    error = std::make_error_code(std::errc::invalid_argument);
                }
                return MigrationResult::failed;
            }
            if (error) {
                return MigrationResult::failed;
            }
            const auto legacyPath = iniPath.parent_path().parent_path() /
                L"FO4VRCommunityShaders" / L"FO4VRCommunityShaders.ini";
            if (!std::filesystem::exists(legacyPath, error)) {
                return error ? MigrationResult::failed : MigrationResult::notNeeded;
            }
            if (!std::filesystem::is_regular_file(legacyPath, error)) {
                if (!error) {
                    error = std::make_error_code(std::errc::invalid_argument);
                }
                return MigrationResult::failed;
            }
            std::filesystem::create_directories(iniPath.parent_path(), error);
            if (error) {
                return MigrationResult::failed;
            }
            // Preserve the complete file, including encoding and comments.
            // MoveFileW also refuses to overwrite a concurrently created INI.
            if (!MoveFileW(legacyPath.c_str(), iniPath.c_str())) {
                error = std::error_code(
                    static_cast<int>(GetLastError()), std::system_category());
                return MigrationResult::failed;
            }
            return MigrationResult::migrated;
        } catch (...) {
            error = std::make_error_code(std::errc::io_error);
            return MigrationResult::failed;
        }
    }

    bool ensureParentDirectory(
        const std::filesystem::path& iniPath) noexcept
    {
        try {
            if (iniPath.empty()) {
                return false;
            }
            const auto parent = iniPath.parent_path();
            if (parent.empty()) {
                return false;
            }

            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
            return std::filesystem::is_directory(parent, error) && !error;
        } catch (...) {
            return false;
        }
    }
}
