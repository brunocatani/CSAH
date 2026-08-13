#include "Features/complex_materials/ComplexParallaxSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>

namespace community_shaders::complex_materials
{
    namespace
    {
        constexpr auto kSection = L"ComplexMaterials";

        [[nodiscard]] bool readBool(
            const std::filesystem::path& path,
            const wchar_t* key,
            bool fallback) noexcept
        {
            return GetPrivateProfileIntW(
                       kSection,
                       key,
                       fallback ? 1 : 0,
                       path.c_str()) != 0;
        }

        [[nodiscard]] std::int32_t readInt(
            const std::filesystem::path& path,
            const wchar_t* key,
            std::int32_t fallback) noexcept
        {
            return static_cast<std::int32_t>(GetPrivateProfileIntW(
                kSection,
                key,
                fallback,
                path.c_str()));
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t fallbackText[64]{};
            _snwprintf_s(
                fallbackText,
                _countof(fallbackText),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(fallback));
            wchar_t valueText[64]{};
            GetPrivateProfileStringW(
                kSection,
                key,
                fallbackText,
                valueText,
                static_cast<DWORD>(_countof(valueText)),
                path.c_str());
            wchar_t* end{};
            errno = 0;
            const auto value = std::wcstof(valueText, &end);
            return errno == ERANGE || end == valueText || *end != L'\0' ||
                    !std::isfinite(value) ?
                fallback :
                value;
        }

        [[nodiscard]] bool writeValue(
            const std::filesystem::path& path,
            const wchar_t* key,
            std::wstring_view value) noexcept
        {
            return WritePrivateProfileStringW(
                       kSection,
                       key,
                       value.data(),
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
                _countof(text),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(value));
            return writeValue(path, key, text);
        }
    }

    Settings loadSettings() noexcept
    {
        const Settings defaults{};
        auto result = defaults;
        const auto path = settings_path::resolveIniPath();
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) ||
            error) {
            logging::info(
                "Complex Materials settings file is absent; using parallax-enabled defaults.");
            return result;
        }

        result.parallaxEnabled = readBool(
            path, L"bEnableParallax", defaults.parallaxEnabled);
        result.parallaxQuality = readInt(
            path, L"iParallaxQuality", defaults.parallaxQuality);
        result.parallaxDepth = readFloat(
            path, L"fParallaxDepth", defaults.parallaxDepth);
        result.grazingClamp = readFloat(
            path, L"fParallaxGrazingClamp", defaults.grazingClamp);
        result.fadeStart = readFloat(
            path, L"fParallaxFadeStart", defaults.fadeStart);
        result.fadeEnd = readFloat(
            path, L"fParallaxFadeEnd", defaults.fadeEnd);
        result = sanitize(result);
        logging::info(
            "Complex Materials settings loaded from '{}'; parallax enabled={}, quality={}.",
            path.string(),
            result.parallaxEnabled,
            result.parallaxQuality);
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
        bool success = writeValue(
            path,
            L"bEnableParallax",
            safe.parallaxEnabled ? L"1" : L"0");
        success = writeValue(
                      path,
                      L"iParallaxQuality",
                      std::to_wstring(safe.parallaxQuality)) &&
            success;
        success = writeFloat(
                      path, L"fParallaxDepth", safe.parallaxDepth) &&
            success;
        success = writeFloat(
                      path, L"fParallaxGrazingClamp", safe.grazingClamp) &&
            success;
        success = writeFloat(
                      path, L"fParallaxFadeStart", safe.fadeStart) &&
            success;
        success = writeFloat(
                      path, L"fParallaxFadeEnd", safe.fadeEnd) &&
            success;
        return success;
    }
}
