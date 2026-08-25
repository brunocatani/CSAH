#include "Features/sky_sync/SkySyncSettingsStore.h"

#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <iterator>
#include <string_view>
#include <system_error>

namespace community_shaders::sky_sync
{
    namespace
    {
        constexpr auto kSection = L"SkySync";

        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            bool fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value,
                static_cast<DWORD>(std::size(value)),
                path.c_str());
            return count == 0 ? fallback :
                contact_shadows::parseBoolean(
                    std::wstring_view(value, count)).value_or(fallback);
        }
    }

    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        const Settings defaults{};
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) ||
            error) {
            return defaults;
        }
        return master_settings::gate(path, Settings{
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Sky Sync settings loaded from '{}'; enabled={}.",
            path.string(),
            result.enabled);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        return WritePrivateProfileStringW(
                   kSection,
                   L"bEnabled",
                   settings.enabled ? L"1" : L"0",
                   path.c_str()) != FALSE;
    }
}
