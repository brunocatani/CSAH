#include "Features/ibl/IblSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <system_error>

namespace community_shaders::ibl
{
    namespace
    {
        constexpr auto kSection = L"ImageBasedLighting";
        constexpr auto kEnabledKey = L"bEnabled";

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

        wchar_t value[64]{};
        const auto count = GetPrivateProfileStringW(
            kSection,
            kEnabledKey,
            L"",
            value,
            static_cast<DWORD>(std::size(value)),
            path.c_str());
        if (count == 0) {
            return defaults;
        }
        const auto parsed = parseBoolean(
            std::wstring_view(value, static_cast<std::size_t>(count)));
        return { .enabled = parsed.value_or(defaults.enabled) };
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Image Based Lighting settings loaded from '{}'; enabled={}.",
            path.string(),
            result.enabled);
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
        return WritePrivateProfileStringW(
                   kSection,
                   kEnabledKey,
                   settings.enabled ? L"1" : L"0",
                   path.c_str()) != FALSE;
    }
}
