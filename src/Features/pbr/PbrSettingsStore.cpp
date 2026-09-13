#include "Features/pbr/PbrSettingsStore.h"

#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <system_error>

namespace csah::pbr
{
    namespace
    {
        constexpr auto kSection = L"PBR";

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

        [[nodiscard]] bool writeBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            bool value) noexcept
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
            float value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text,
                std::size(text),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(value));
            return WritePrivateProfileStringW(
                       kSection,
                       key,
                       text,
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
        return master_settings::gate(path, sanitize({
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .legacyMaterials = readBoolean(
                path, L"bLegacyMaterials", defaults.legacyMaterials),
            .directGgx = readBoolean(
                path, L"bDirectGGX", defaults.directGgx),
            .grassGgx = readBoolean(
                path, L"bGrassGGX", defaults.grassGgx),
            .environmentFresnel = readBoolean(
                path,
                L"bEnvironmentFresnel",
                defaults.environmentFresnel),
            .energyConservation = readBoolean(
                path,
                L"bEnergyConservation",
                defaults.energyConservation),
            .multiscatterCompensation = readBoolean(
                path,
                L"bMultiscatterCompensation",
                defaults.multiscatterCompensation),
            .specularOcclusion = readBoolean(
                path,
                L"bSpecularOcclusion",
                defaults.specularOcclusion),
            .roughnessMultiplier = readFloat(
                path,
                L"fRoughnessMultiplier",
                defaults.roughnessMultiplier),
            .specularRoughnessBlend = readFloat(
                path,
                L"fSpecularRoughnessBlend",
                defaults.specularRoughnessBlend),
            .baseF0Multiplier = readFloat(
                path, L"fBaseF0Multiplier", defaults.baseF0Multiplier),
            .minimumF0 = readFloat(
                path, L"fMinimumF0", defaults.minimumF0),
            .cubemapToF0Multiplier = readFloat(
                path,
                L"fCubemapToF0Multiplier",
                defaults.cubemapToF0Multiplier),
            .complexMaterialF0Multiplier = readFloat(
                path,
                L"fComplexMaterialF0Multiplier",
                defaults.complexMaterialF0Multiplier),
            .directLightingScale = readFloat(
                path,
                L"fDirectLightingScale",
                defaults.directLightingScale),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "PBR settings loaded from '{}'; enabled={}, legacy={}, directGGX={}, environmentFresnel={}, roughnessMultiplier={}, baseF0Multiplier={}.",
            path.string(),
            result.enabled,
            result.legacyMaterials,
            result.directGgx,
            result.environmentFresnel,
            result.roughnessMultiplier,
            result.baseF0Multiplier);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        const auto safe = sanitize(settings);
        auto success = writeBoolean(path, L"bEnabled", safe.enabled);
        success = writeBoolean(
                      path, L"bLegacyMaterials", safe.legacyMaterials) &&
            success;
        success = writeBoolean(path, L"bDirectGGX", safe.directGgx) &&
            success;
        success = writeBoolean(path, L"bGrassGGX", safe.grassGgx) &&
            success;
        success = writeBoolean(
                      path,
                      L"bEnvironmentFresnel",
                      safe.environmentFresnel) &&
            success;
        success = writeBoolean(
                      path,
                      L"bEnergyConservation",
                      safe.energyConservation) &&
            success;
        success = writeBoolean(
                      path,
                      L"bMultiscatterCompensation",
                      safe.multiscatterCompensation) &&
            success;
        success = writeBoolean(
                      path,
                      L"bSpecularOcclusion",
                      safe.specularOcclusion) &&
            success;
        success = writeFloat(
                      path,
                      L"fRoughnessMultiplier",
                      safe.roughnessMultiplier) &&
            success;
        success = writeFloat(
                      path,
                      L"fSpecularRoughnessBlend",
                      safe.specularRoughnessBlend) &&
            success;
        success = writeFloat(
                      path,
                      L"fBaseF0Multiplier",
                      safe.baseF0Multiplier) &&
            success;
        success = writeFloat(path, L"fMinimumF0", safe.minimumF0) &&
            success;
        success = writeFloat(
                      path,
                      L"fCubemapToF0Multiplier",
                      safe.cubemapToF0Multiplier) &&
            success;
        success = writeFloat(
                      path,
                      L"fComplexMaterialF0Multiplier",
                      safe.complexMaterialF0Multiplier) &&
            success;
        success = writeFloat(
                      path,
                      L"fDirectLightingScale",
                      safe.directLightingScale) &&
            success;
        return success;
    }
}
