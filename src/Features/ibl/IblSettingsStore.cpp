#include "Features/ibl/IblSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <system_error>

namespace community_shaders::ibl
{
    namespace
    {
        constexpr auto kIblSection = L"ImageBasedLighting";
        constexpr auto kDynamicCubemapsSection = L"DynamicCubemaps";
        constexpr auto kEnabledKey = L"bEnabled";
        constexpr auto kDynamicCubemapsEnabledKey = L"bEnabled";
        constexpr auto kDiffuseEnabledKey = L"bDiffuseEnabled";
        constexpr auto kDiffuseLevelKey = L"fDiffuseLevel";

        [[nodiscard]] std::wstring normalized(
            std::wstring_view text)
        {
            const auto first = std::ranges::find_if_not(
                text,
                [](wchar_t value) { return std::iswspace(value) != 0; });
            const auto last = std::find_if_not(
                text.rbegin(),
                text.rend(),
                [](wchar_t value) { return std::iswspace(value) != 0; })
                                  .base();
            if (first >= last) {
                return {};
            }
            std::wstring result(first, last);
            std::ranges::transform(
                result,
                result.begin(),
                [](wchar_t value) { return std::towlower(value); });
            return result;
        }


        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            bool fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                section,
                key,
                L"",
                value,
                static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            return parseBoolean(std::wstring_view(value, count)).value_or(
                fallback);
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kIblSection,
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
                parsed :
                fallback;
        }

        [[nodiscard]] bool writeValue(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* value) noexcept
        {
            return WritePrivateProfileStringW(
                section,
                key,
                value,
                path.c_str()) != FALSE;
        }
    }

    std::optional<bool> parseBoolean(std::wstring_view text) noexcept
    {
        try {
            const auto value = normalized(text);
            if (value == L"1" || value == L"true" || value == L"yes" ||
                value == L"on") {
                return true;
            }
            if (value == L"0" || value == L"false" || value == L"no" ||
                value == L"off") {
                return false;
            }
        } catch (...) {
        }
        return std::nullopt;
    }

    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        const Settings defaults{};
        std::error_code pathError;
        if (path.empty() ||
            !std::filesystem::is_regular_file(path, pathError) || pathError) {
            return defaults;
        }

        return sanitize({
            .enabled = readBoolean(
                path,
                kIblSection,
                kEnabledKey,
                defaults.enabled),
            .dynamicCubemapsEnabled = readBoolean(
                path,
                kDynamicCubemapsSection,
                kDynamicCubemapsEnabledKey,
                defaults.dynamicCubemapsEnabled),
            .diffuseEnabled = readBoolean(
                path,
                kIblSection,
                kDiffuseEnabledKey,
                defaults.diffuseEnabled),
            .diffuseLevel = readFloat(
                path,
                kDiffuseLevelKey,
                defaults.diffuseLevel),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Image Based Lighting settings loaded from '{}'; enabled={}, Dynamic Cubemaps enabled={}, diffuse enabled={}, diffuse level={}.",
            path.string(),
            result.enabled,
            result.dynamicCubemapsEnabled,
            result.diffuseEnabled,
            result.diffuseLevel);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            logging::error(
                "Community Shaders settings directory is unavailable for '{}'.",
                path.string());
            return false;
        }
        const auto safe = sanitize(settings);
        wchar_t diffuseLevel[64]{};
        _snwprintf_s(
            diffuseLevel,
            std::size(diffuseLevel),
            _TRUNCATE,
            L"%.9g",
            static_cast<double>(safe.diffuseLevel));
        auto success = writeValue(
            path,
            kIblSection,
            kEnabledKey,
            safe.enabled ? L"1" : L"0");
        success = writeValue(
                      path,
                      kDynamicCubemapsSection,
                      kDynamicCubemapsEnabledKey,
                      safe.dynamicCubemapsEnabled ? L"1" : L"0") &&
            success;
        success = writeValue(
                      path,
                      kIblSection,
                      kDiffuseEnabledKey,
                      safe.diffuseEnabled ? L"1" : L"0") &&
            success;
        success = writeValue(
                      path,
                      kIblSection,
                      kDiffuseLevelKey,
                      diffuseLevel) &&
            success;
        return success;
    }
}
