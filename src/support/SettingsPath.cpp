#include "support/SettingsPath.h"
#include "settings/FirstRunPreset.h"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <system_error>

namespace csah::settings_path
{
    namespace
    {
        struct PendingIni
        {
            std::array<wchar_t, MAX_PATH> path{};
            HANDLE file{ INVALID_HANDLE_VALUE };
            ~PendingIni()
            {
                if (file != INVALID_HANDLE_VALUE) {
                    CloseHandle(file);
                }
                if (path[0]) {
                    DeleteFileW(path.data());
                }
            }
        };

        SetupResult createFirstRunIni(
            const std::filesystem::path& iniPath,
            std::error_code& error)
        {
            std::filesystem::create_directories(iniPath.parent_path(), error);
            if (error) {
                return SetupResult::failed;
            }
            PendingIni pending;
            if (!GetTempFileNameW(
                    iniPath.parent_path().c_str(), L"CSH", 0, pending.path.data())) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                return SetupResult::failed;
            }
            pending.file = CreateFileW(pending.path.data(), GENERIC_WRITE, 0,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pending.file == INVALID_HANDLE_VALUE) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                return SetupResult::failed;
            }
            DWORD written{};
            if (!WriteFile(pending.file, kFirstRunIni.data(),
                    static_cast<DWORD>(kFirstRunIni.size()), &written, nullptr) ||
                written != kFirstRunIni.size() || !FlushFileBuffers(pending.file)) {
                const auto code = GetLastError();
                error = std::error_code(static_cast<int>(code ? code : ERROR_WRITE_FAULT),
                    std::system_category());
                return SetupResult::failed;
            }
            if (!CloseHandle(pending.file)) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                pending.file = INVALID_HANDLE_VALUE;
                return SetupResult::failed;
            }
            pending.file = INVALID_HANDLE_VALUE;
            // Publish only a complete file and never replace settings created
            // by another process while this startup was preparing its defaults.
            if (!MoveFileExW(pending.path.data(), iniPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                return SetupResult::failed;
            }
            return SetupResult::created;
        }
    }

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

    SetupResult initializeIni(
        const std::filesystem::path& iniPath,
        std::error_code& error) noexcept
    {
        error.clear();
        try {
            if (iniPath.empty()) {
                error = std::make_error_code(std::errc::invalid_argument);
                return SetupResult::failed;
            }
            if (std::filesystem::exists(iniPath, error)) {
                if (std::filesystem::is_regular_file(iniPath, error)) {
                    return SetupResult::existing;
                }
                if (!error) {
                    error = std::make_error_code(std::errc::invalid_argument);
                }
                return SetupResult::failed;
            }
            if (error) {
                return SetupResult::failed;
            }
            const auto legacyPath = iniPath.parent_path().parent_path() /
                L"FO4VRCommunityShaders" / L"FO4VRCommunityShaders.ini";
            if (!std::filesystem::exists(legacyPath, error)) {
                return error ? SetupResult::failed : createFirstRunIni(iniPath, error);
            }
            if (!std::filesystem::is_regular_file(legacyPath, error)) {
                if (!error) {
                    error = std::make_error_code(std::errc::invalid_argument);
                }
                return SetupResult::failed;
            }
            std::filesystem::create_directories(iniPath.parent_path(), error);
            if (error) {
                return SetupResult::failed;
            }
            // Preserve the complete file, including encoding and comments.
            // MoveFileW also refuses to overwrite a concurrently created INI.
            if (!MoveFileW(legacyPath.c_str(), iniPath.c_str())) {
                error = std::error_code(
                    static_cast<int>(GetLastError()), std::system_category());
                return SetupResult::failed;
            }
            return SetupResult::migrated;
        } catch (...) {
            error = std::make_error_code(std::errc::io_error);
            return SetupResult::failed;
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
