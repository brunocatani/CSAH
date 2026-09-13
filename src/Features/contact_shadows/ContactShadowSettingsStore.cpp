#include "Features/contact_shadows/ContactShadowSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"
#include "settings/MasterSettings.h"

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <system_error>

namespace csah::contact_shadows
{
    namespace
    {
        constexpr auto kSection = L"ContactShadows";

        [[nodiscard]] std::wstring normalized(std::wstring_view text)
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

        [[nodiscard]] std::uint32_t readUnsigned(
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
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) ||
            error) {
            return defaults;
        }
        return master_settings::gate(path, sanitize({
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .foveated = readBoolean(
                path,
                L"bFoveated",
                defaults.foveated),
            .strength = readFloat(path, L"fStrength", defaults.strength),
            .maxDistance = readFloat(
                path,
                L"fMaxDistance",
                defaults.maxDistance),
            .fadeDistance = readFloat(
                path,
                L"fFadeDistance",
                defaults.fadeDistance),
            .thickness = readFloat(
                path,
                L"fThickness",
                defaults.thickness),
            .sampleCount = readUnsigned(
                path,
                L"iSampleCount",
                defaults.sampleCount),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Contact Shadows settings loaded from '{}'; enabled={}, foveated={}, strength={}, ray distance={}, fade distance={}, thickness={}, samples={}.",
            path.string(),
            result.enabled,
            result.foveated,
            result.strength,
            result.maxDistance,
            result.fadeDistance,
            result.thickness,
            result.sampleCount);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            logging::error(
                "CSAH settings directory is unavailable for '{}'.",
                path.string());
            return false;
        }
        const auto safe = sanitize(settings);
        wchar_t strength[64]{};
        wchar_t distance[64]{};
        wchar_t fadeDistance[64]{};
        wchar_t thickness[64]{};
        wchar_t samples[64]{};
        _snwprintf_s(strength, std::size(strength), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.strength));
        _snwprintf_s(distance, std::size(distance), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.maxDistance));
        _snwprintf_s(
            fadeDistance,
            std::size(fadeDistance),
            _TRUNCATE,
            L"%.9g",
            static_cast<double>(safe.fadeDistance));
        _snwprintf_s(thickness, std::size(thickness), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.thickness));
        _snwprintf_s(samples, std::size(samples), _TRUNCATE, L"%u",
            safe.sampleCount);
        auto success = writeValue(
            path,
            L"bEnabled",
            safe.enabled ? L"1" : L"0");
        success = writeValue(
                      path,
                      L"bFoveated",
                      safe.foveated ? L"1" : L"0") &&
            success;
        success = writeValue(path, L"fStrength", strength) && success;
        success = writeValue(path, L"fMaxDistance", distance) && success;
        success = writeValue(path, L"fFadeDistance", fadeDistance) && success;
        success = writeValue(path, L"fThickness", thickness) && success;
        success = writeValue(path, L"iSampleCount", samples) && success;
        return success;
    }
}
