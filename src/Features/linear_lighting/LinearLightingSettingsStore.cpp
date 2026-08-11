#include "Features/linear_lighting/LinearLightingSettingsStore.h"

#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <limits>
#include <system_error>
#include <string_view>

namespace community_shaders::linear_lighting
{
    namespace
    {
        constexpr auto kSection = L"LinearLighting";

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
            if (errno == ERANGE || end == valueText || *end != L'\0' ||
                !std::isfinite(value)) {
                return fallback;
            }
            return value;
        }

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

    std::filesystem::path settingsPath() noexcept
    {
        HMODULE module{};
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&settingsPath),
                &module)) {
            return {};
        }
        std::array<wchar_t, 32768> buffer{};
        const auto length = GetModuleFileNameW(
            module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size()) {
            return {};
        }
        return std::filesystem::path(
                   std::wstring_view(buffer.data(), length))
            .parent_path() /
            L"FO4VRCommunityShaders.ini";
    }

    Settings loadSettings() noexcept
    {
        const Settings defaults{};
        auto result = defaults;
        const auto path = settingsPath();
        std::error_code pathError;
        if (path.empty() ||
            !std::filesystem::is_regular_file(path, pathError) ||
            pathError) {
            logging::info(
                "Linear Lighting settings file is absent; using disabled defaults.");
            return result;
        }

        result.enabled = readBool(path, L"bEnabled", defaults.enabled);
        result.lightGamma = readFloat(path, L"fLightGamma", defaults.lightGamma);
        result.colorGamma = readFloat(path, L"fColorGamma", defaults.colorGamma);
        result.emitColorGamma = readFloat(path, L"fEmitColorGamma", defaults.emitColorGamma);
        result.glowmapGamma = readFloat(path, L"fGlowmapGamma", defaults.glowmapGamma);
        result.ambientGamma = readFloat(path, L"fAmbientGamma", defaults.ambientGamma);
        result.fogGamma = readFloat(path, L"fFogGamma", defaults.fogGamma);
        result.fogAlphaGamma = readFloat(path, L"fFogAlphaGamma", defaults.fogAlphaGamma);
        result.effectGamma = readFloat(path, L"fEffectGamma", defaults.effectGamma);
        result.effectAlphaGamma = readFloat(path, L"fEffectAlphaGamma", defaults.effectAlphaGamma);
        result.skyGamma = readFloat(path, L"fSkyGamma", defaults.skyGamma);
        result.waterGamma = readFloat(path, L"fWaterGamma", defaults.waterGamma);
        result.volumetricLightingGamma = readFloat(
            path,
            L"fVolumetricLightingGamma",
            defaults.volumetricLightingGamma);
        result.vanillaDiffuseColorMultiplier = readFloat(
            path,
            L"fVanillaDiffuseColorMultiplier",
            defaults.vanillaDiffuseColorMultiplier);
        result.directionalLightMultiplier = readFloat(
            path,
            L"fDirectionalLightMultiplier",
            defaults.directionalLightMultiplier);
        result.pointLightMultiplier = readFloat(
            path,
            L"fPointLightMultiplier",
            defaults.pointLightMultiplier);
        result.ambientMultiplier = readFloat(
            path,
            L"fAmbientMultiplier",
            defaults.ambientMultiplier);
        result.emitColorMultiplier = readFloat(
            path,
            L"fEmitColorMultiplier",
            defaults.emitColorMultiplier);
        result.glowmapMultiplier = readFloat(
            path,
            L"fGlowmapMultiplier",
            defaults.glowmapMultiplier);
        result.effectLightingMultiplier = readFloat(
            path,
            L"fEffectLightingMultiplier",
            defaults.effectLightingMultiplier);
        result.membraneEffectMultiplier = readFloat(
            path,
            L"fMembraneEffectMultiplier",
            defaults.membraneEffectMultiplier);
        result.otherEffectMultiplier = readFloat(
            path,
            L"fOtherEffectMultiplier",
            defaults.otherEffectMultiplier);

        result = sanitize(result);
        logging::info(
            "Linear Lighting settings loaded from '{}'; enabled={}.",
            path.string(),
            result.enabled);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settingsPath();
        if (path.empty()) {
            return false;
        }
        const auto safe = sanitize(settings);
        bool success = writeValue(path, L"bEnabled", safe.enabled ? L"1" : L"0");
#define WRITE_SETTING(KEY, FIELD) \
        success = writeFloat(path, KEY, safe.FIELD) && success
        WRITE_SETTING(L"fLightGamma", lightGamma);
        WRITE_SETTING(L"fColorGamma", colorGamma);
        WRITE_SETTING(L"fEmitColorGamma", emitColorGamma);
        WRITE_SETTING(L"fGlowmapGamma", glowmapGamma);
        WRITE_SETTING(L"fAmbientGamma", ambientGamma);
        WRITE_SETTING(L"fFogGamma", fogGamma);
        WRITE_SETTING(L"fFogAlphaGamma", fogAlphaGamma);
        WRITE_SETTING(L"fEffectGamma", effectGamma);
        WRITE_SETTING(L"fEffectAlphaGamma", effectAlphaGamma);
        WRITE_SETTING(L"fSkyGamma", skyGamma);
        WRITE_SETTING(L"fWaterGamma", waterGamma);
        WRITE_SETTING(L"fVolumetricLightingGamma", volumetricLightingGamma);
        WRITE_SETTING(L"fVanillaDiffuseColorMultiplier", vanillaDiffuseColorMultiplier);
        WRITE_SETTING(L"fDirectionalLightMultiplier", directionalLightMultiplier);
        WRITE_SETTING(L"fPointLightMultiplier", pointLightMultiplier);
        WRITE_SETTING(L"fAmbientMultiplier", ambientMultiplier);
        WRITE_SETTING(L"fEmitColorMultiplier", emitColorMultiplier);
        WRITE_SETTING(L"fGlowmapMultiplier", glowmapMultiplier);
        WRITE_SETTING(L"fEffectLightingMultiplier", effectLightingMultiplier);
        WRITE_SETTING(L"fMembraneEffectMultiplier", membraneEffectMultiplier);
        WRITE_SETTING(L"fOtherEffectMultiplier", otherEffectMultiplier);
#undef WRITE_SETTING
        return success;
    }
}
