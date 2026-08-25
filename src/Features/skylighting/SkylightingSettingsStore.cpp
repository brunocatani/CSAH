#include "Features/skylighting/SkylightingSettingsStore.h"

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

namespace community_shaders::skylighting
{
    namespace
    {
        constexpr auto kSection = L"Skylighting";

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

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value,
                static_cast<DWORD>(std::size(value)),
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

        [[nodiscard]] std::uint32_t readInteger(
            const std::filesystem::path& path,
            const wchar_t* key,
            std::uint32_t fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value,
                static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstoul(value, &end, 10);
            return errno != ERANGE && end != value && *end == L'\0' ?
                static_cast<std::uint32_t>(parsed) : fallback;
        }

        [[nodiscard]] bool writeValue(
            const std::filesystem::path& path,
            const wchar_t* key,
            const wchar_t* value) noexcept
        {
            return WritePrivateProfileStringW(
                       kSection,
                       key,
                       value,
                       path.c_str()) != FALSE;
        }

        [[nodiscard]] bool writeFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text,
                std::size(text),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(value));
            return writeValue(path, key, text);
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
            .quality = sanitizeQuality(readInteger(
                path,
                L"iQuality",
                static_cast<std::uint32_t>(defaults.quality))),
            .minimumDiffuseVisibility = readFloat(
                path,
                L"fMinDiffuseVisibility",
                defaults.minimumDiffuseVisibility),
            .minimumSpecularVisibility = readFloat(
                path,
                L"fMinSpecularVisibility",
                defaults.minimumSpecularVisibility),
            .maximumZenithDegrees = readFloat(
                path,
                L"fMaxZenithDegrees",
                defaults.maximumZenithDegrees),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Skylighting settings loaded from '{}'; enabled={}, quality={}, minimum diffuse visibility={}, minimum specular visibility={}, maximum zenith={} degrees.",
            path.string(),
            result.enabled,
            qualityName(result.quality),
            result.minimumDiffuseVisibility,
            result.minimumSpecularVisibility,
            result.maximumZenithDegrees);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        const auto safe = sanitize(settings);
        wchar_t quality[16]{};
        _snwprintf_s(
            quality,
            std::size(quality),
            _TRUNCATE,
            L"%u",
            static_cast<std::uint32_t>(safe.quality));
        auto success = writeValue(
            path,
            L"bEnabled",
            safe.enabled ? L"1" : L"0");
        success = writeValue(path, L"iQuality", quality) && success;
        success = writeFloat(
                      path,
                      L"fMinDiffuseVisibility",
                      safe.minimumDiffuseVisibility) &&
            success;
        success = writeFloat(
                      path,
                      L"fMinSpecularVisibility",
                      safe.minimumSpecularVisibility) &&
            success;
        success = writeFloat(
                      path,
                      L"fMaxZenithDegrees",
                      safe.maximumZenithDegrees) &&
            success;
        return success;
    }
}
