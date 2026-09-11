#include "support/SettingsPath.h"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <system_error>

namespace community_shaders::settings_path
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
                L"Mods_Config" / L"FO4VRCommunityShaders" /
                L"FO4VRCommunityShaders.ini";
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
