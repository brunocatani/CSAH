#include "Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h"

#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"
#include "settings/MasterSettings.h"

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <system_error>

namespace csah::subsurface_scattering
{
    namespace
    {
        constexpr auto kSection = L"SubsurfaceScattering";

        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            bool fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            return count == 0 ? fallback :
                contact_shadows::parseBoolean(
                    std::wstring_view(value, count)).value_or(fallback);
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstof(value, &end);
            return errno != ERANGE && end != value && *end == L'\0' &&
                    std::isfinite(parsed) ?
                parsed : fallback;
        }

        [[nodiscard]] bool writeFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text, std::size(text), _TRUNCATE, L"%.9g",
                static_cast<double>(value));
            return WritePrivateProfileStringW(
                       kSection, key, text, path.c_str()) != FALSE;
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
        return master_settings::gate(path, sanitize({
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .strength = readFloat(path, L"fStrength", defaults.strength),
            .radiusPixels = readFloat(
                path, L"fRadiusPixels", defaults.radiusPixels),
            .depthRejection = readFloat(
                path, L"fDepthRejection", defaults.depthRejection),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Subsurface Scattering settings loaded from '{}'; enabled={}, strength={}, radius={}, depthRejection={}.",
            path.string(), result.enabled, result.strength, result.radiusPixels,
            result.depthRejection);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        const auto safe = sanitize(settings);
        auto success = WritePrivateProfileStringW(
                           kSection, L"bEnabled", safe.enabled ? L"1" : L"0",
                           path.c_str()) != FALSE;
        success = writeFloat(path, L"fStrength", safe.strength) && success;
        success = writeFloat(path, L"fRadiusPixels", safe.radiusPixels) &&
            success;
        success = writeFloat(
                      path, L"fDepthRejection", safe.depthRejection) &&
            success;
        return success;
    }
}
