#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <array>
#include <cwctype>
#include <string>
#include <string_view>
#include <system_error>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr auto kSection = L"VanillaFixes";

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
                    std::wstring_view(value.data(), count),
                    fallback);
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

        [[nodiscard]] DirectionalLightDiagnosticMode readDiagnosticMode(
            const std::filesystem::path& path,
            const DirectionalLightDiagnosticMode fallback) noexcept
        {
            const auto raw = GetPrivateProfileIntW(
                kSection,
                L"iDirectionalLightDiagnosticMode",
                static_cast<int>(fallback),
                path.c_str());
            if (raw > static_cast<UINT>(
                    DirectionalLightDiagnosticMode::shadowVisibility)) {
                return fallback;
            }
            return static_cast<DirectionalLightDiagnosticMode>(raw);
        }

        [[nodiscard]] bool writeDiagnosticMode(
            const std::filesystem::path& path,
            const DirectionalLightDiagnosticMode mode) noexcept
        {
            constexpr std::array values{ L"0", L"1", L"2", L"3" };
            const auto index = static_cast<std::size_t>(mode);
            if (index >= values.size()) {
                return false;
            }
            return WritePrivateProfileStringW(
                       kSection,
                       L"iDirectionalLightDiagnosticMode",
                       values[index],
                       path.c_str()) != FALSE;
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
        return {
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .precipitationOcclusion = readBoolean(
                path,
                L"bPrecipitationOcclusion",
                defaults.precipitationOcclusion),
            .imageSpaceModifiers = readBoolean(
                path,
                L"bAllowImageSpaceModifiers",
                defaults.imageSpaceModifiers),
            .sao = readBoolean(path, L"bVrAllowSAO", defaults.sao),
            .screenSpaceReflections = readBoolean(
                path,
                L"bVrAllowScreenSpaceReflections",
                defaults.screenSpaceReflections),
            .screenSpaceSubsurfaceScattering = readBoolean(
                path,
                L"bVrAllowScreenSpaceSubsurfaceScattering",
                defaults.screenSpaceSubsurfaceScattering),
            .lensFlare = readBoolean(
                path,
                L"bLensFlareVr",
                defaults.lensFlare),
            .focusShadows = readBoolean(
                path,
                L"bVrAllowFocusShadows",
                defaults.focusShadows),
            .sunbeams = readBoolean(
                path,
                L"bUseSunbeams",
                defaults.sunbeams),
            .directionalLightDiagnosticMode = readDiagnosticMode(
                path,
                defaults.directionalLightDiagnosticMode),
        };
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto settings = loadSettings(path);
        logging::info(
            "Vanilla Fixes settings loaded from '{}'; enabled={}, precipitation={}, imageModifiers={}, SAO={}, SSR={}, screenSpaceSSS={}, lensFlare={}, focusShadows={}, sunbeams={}, exclusiveDirectionalDiagnostic={}.",
            path.string(),
            settings.enabled,
            settings.precipitationOcclusion,
            settings.imageSpaceModifiers,
            settings.sao,
            settings.screenSpaceReflections,
            settings.screenSpaceSubsurfaceScattering,
            settings.lensFlare,
            settings.focusShadows,
            settings.sunbeams,
            static_cast<unsigned>(settings.directionalLightDiagnosticMode));
        return settings;
    }

    bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept
    {
        if (path.empty() || !settings_path::ensureParentDirectory(path)) {
            logging::error(
                "Community Shaders settings directory is unavailable for '{}'.",
                path.string());
            return false;
        }
        auto success = writeBoolean(path, L"bEnabled", settings.enabled);
        success = writeBoolean(
                      path,
                      L"bPrecipitationOcclusion",
                      settings.precipitationOcclusion) &&
            success;
        success = writeBoolean(
                      path,
                      L"bAllowImageSpaceModifiers",
                      settings.imageSpaceModifiers) &&
            success;
        success = writeBoolean(path, L"bVrAllowSAO", settings.sao) && success;
        success = writeBoolean(
                      path,
                      L"bVrAllowScreenSpaceReflections",
                      settings.screenSpaceReflections) &&
            success;
        success = writeBoolean(
                      path,
                      L"bVrAllowScreenSpaceSubsurfaceScattering",
                      settings.screenSpaceSubsurfaceScattering) &&
            success;
        success = writeBoolean(path, L"bLensFlareVr", settings.lensFlare) &&
            success;
        success = writeBoolean(
                      path,
                      L"bVrAllowFocusShadows",
                      settings.focusShadows) &&
            success;
        success = writeBoolean(path, L"bUseSunbeams", settings.sunbeams) &&
            success;
        success = writeDiagnosticMode(
                      path,
                      settings.directionalLightDiagnosticMode) &&
            success;
        return success;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        return saveSettings(settings_path::resolveIniPath(), settings);
    }
}
