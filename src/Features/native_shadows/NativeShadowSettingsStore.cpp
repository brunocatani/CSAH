#include "Features/native_shadows/NativeShadowSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace csah::native_shadows
{
    namespace
    {
        constexpr auto kSection = L"NativeShadows";

        [[nodiscard]] bool parseBoolean(
            const std::wstring_view source,
            const bool fallback) noexcept
        {
            try {
                std::wstring value;
                value.reserve(source.size());
                for (const auto character : source) {
                    if (std::iswspace(character) == 0) {
                        value.push_back(
                            static_cast<wchar_t>(std::towlower(character)));
                    }
                }
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
            return fallback;
        }

        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            const bool fallback) noexcept
        {
            std::array<wchar_t, 64> value{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value.data(),
                static_cast<DWORD>(value.size()),
                path.c_str());
            return count == 0 ? fallback :
                parseBoolean(
                    std::wstring_view(value.data(), count), fallback);
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            const float fallback) noexcept
        {
            std::array<wchar_t, 64> value{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value.data(),
                static_cast<DWORD>(value.size()),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstof(value.data(), &end);
            return errno != ERANGE && end != value.data() &&
                    end == value.data() + count && std::isfinite(parsed) ?
                parsed : fallback;
        }

        [[nodiscard]] std::uint32_t readUnsigned(
            const std::filesystem::path& path,
            const wchar_t* key,
            const std::uint32_t fallback) noexcept
        {
            std::array<wchar_t, 64> value{};
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                L"",
                value.data(),
                static_cast<DWORD>(value.size()),
                path.c_str());
            if (count == 0 || value[0] == L'-') {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstoul(value.data(), &end, 10);
            return errno != ERANGE && end != value.data() &&
                    end == value.data() + count &&
                    parsed <=
                        (std::numeric_limits<std::uint32_t>::max)() ?
                static_cast<std::uint32_t>(parsed) : fallback;
        }

        [[nodiscard]] bool writeBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            const bool value) noexcept
        {
            return WritePrivateProfileStringW(
                       kSection,
                       key,
                       value ? L"1" : L"0",
                       path.c_str()) != FALSE;
        }

        [[nodiscard]] bool writeFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            const float value) noexcept
        {
            std::array<wchar_t, 64> text{};
            _snwprintf_s(
                text.data(),
                text.size(),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(value));
            return WritePrivateProfileStringW(
                       kSection, key, text.data(), path.c_str()) != FALSE;
        }

        [[nodiscard]] bool writeUnsigned(
            const std::filesystem::path& path,
            const wchar_t* key,
            const std::uint32_t value) noexcept
        {
            std::array<wchar_t, 64> text{};
            _snwprintf_s(
                text.data(), text.size(), _TRUNCATE, L"%u", value);
            return WritePrivateProfileStringW(
                       kSection, key, text.data(), path.c_str()) != FALSE;
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
        return sanitize({
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .extendedDirectionalCascades = readBoolean(
                path,
                L"bExtendedDirectionalCascades",
                defaults.extendedDirectionalCascades),
            .tiledDeferredLighting = readBoolean(
                path,
                L"bTiledDeferredLighting",
                defaults.tiledDeferredLighting),
            .directionalShadowDistance = readFloat(
                path,
                L"fDirectionalShadowDistance",
                defaults.directionalShadowDistance),
            .cascadeBlendDistance = readFloat(
                path,
                L"fCascadeBlendDistance",
                defaults.cascadeBlendDistance),
            .orthographicShadowFilter = readUnsigned(
                path,
                L"iOrthographicShadowFilter",
                defaults.orthographicShadowFilter),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Native Shadows settings loaded from '{}'; enabled={}, fourCascades={}, tiledDeferredLighting={}, fixedDistance={}, cascadeBlend={}, orthoFilter={}. No FPS or adaptive-quality controller is present.",
            path.string(),
            result.enabled,
            result.extendedDirectionalCascades,
            result.tiledDeferredLighting,
            result.directionalShadowDistance,
            result.cascadeBlendDistance,
            result.orthographicShadowFilter);
        return result;
    }

    bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept
    {
        if (path.empty() || !settings_path::ensureParentDirectory(path)) {
            logging::error(
                "CSAH settings directory is unavailable for '{}'.",
                path.string());
            return false;
        }
        const auto safe = sanitize(settings);
        auto success = writeBoolean(path, L"bEnabled", safe.enabled);
        success = writeBoolean(
                      path,
                      L"bExtendedDirectionalCascades",
                      safe.extendedDirectionalCascades) &&
            success;
        success = writeBoolean(
                      path,
                      L"bTiledDeferredLighting",
                      safe.tiledDeferredLighting) &&
            success;
        success = writeFloat(
                      path,
                      L"fDirectionalShadowDistance",
                      safe.directionalShadowDistance) &&
            success;
        success = writeFloat(
                      path,
                      L"fCascadeBlendDistance",
                      safe.cascadeBlendDistance) &&
            success;
        success = writeUnsigned(
                      path,
                      L"iOrthographicShadowFilter",
                      safe.orthographicShadowFilter) &&
            success;
        return success;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        return saveSettings(settings_path::resolveIniPath(), settings);
    }
}
