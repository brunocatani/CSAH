#include "Features/bloom_glare/BloomGlareSettingsStore.h"

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

namespace csah::bloom_glare
{
    namespace
    {
        constexpr auto kBloomSection = L"Bloom";
        constexpr auto kGlareSection = L"PhysicalGlare";

        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            bool fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                section, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            return count == 0 ? fallback :
                contact_shadows::parseBoolean(
                    std::wstring_view(value, count)).value_or(fallback);
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                section, key, L"", value, static_cast<DWORD>(std::size(value)),
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
            const wchar_t* section,
            const wchar_t* key,
            std::uint32_t fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                section, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0 || value[0] == L'-') {
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
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* value) noexcept
        {
            return WritePrivateProfileStringW(
                       section, key, value, path.c_str()) != FALSE;
        }

        [[nodiscard]] bool writeFloat(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            float value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text, std::size(text), _TRUNCATE, L"%.9g",
                static_cast<double>(value));
            return writeValue(path, section, key, text);
        }

        [[nodiscard]] bool writeUnsigned(
            const std::filesystem::path& path,
            const wchar_t* section,
            const wchar_t* key,
            std::uint32_t value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text, std::size(text), _TRUNCATE, L"%u", value);
            return writeValue(path, section, key, text);
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
        auto result = sanitize({
            .bloom = {
                .enabled = readBoolean(
                    path, kBloomSection, L"bEnabled", defaults.bloom.enabled),
                .thresholdEV = readFloat(
                    path,
                    kBloomSection,
                    L"fThresholdEV",
                    defaults.bloom.thresholdEV),
                .intensity = readFloat(
                    path,
                    kBloomSection,
                    L"fIntensity",
                    defaults.bloom.intensity),
                .radius = readFloat(
                    path,
                    kBloomSection,
                    L"fRadius",
                    defaults.bloom.radius),
            },
            .glare = {
                .enabled = readBoolean(
                    path, kGlareSection, L"bEnabled", defaults.glare.enabled),
                .thresholdEV = readFloat(
                    path,
                    kGlareSection,
                    L"fThresholdEV",
                    defaults.glare.thresholdEV),
                .intensity = readFloat(
                    path,
                    kGlareSection,
                    L"fIntensity",
                    defaults.glare.intensity),
                .fftResolution = readUnsigned(
                    path,
                    kGlareSection,
                    L"iFftResolution",
                    defaults.glare.fftResolution),
                .paddingRatio = readFloat(
                    path,
                    kGlareSection,
                    L"fPaddingRatio",
                    defaults.glare.paddingRatio),
                .apertureMode = readUnsigned(
                    path,
                    kGlareSection,
                    L"iApertureMode",
                    defaults.glare.apertureMode),
                .apertureBlades = readUnsigned(
                    path,
                    kGlareSection,
                    L"iApertureBlades",
                    defaults.glare.apertureBlades),
                .apertureRotationDegrees = readFloat(
                    path,
                    kGlareSection,
                    L"fApertureRotationDegrees",
                    defaults.glare.apertureRotationDegrees),
                .fStop = readFloat(
                    path,
                    kGlareSection,
                    L"fFStop",
                    defaults.glare.fStop),
                .fresnelExponent = readFloat(
                    path,
                    kGlareSection,
                    L"fFresnelExponent",
                    defaults.glare.fresnelExponent),
                .sphericalAberration = readFloat(
                    path,
                    kGlareSection,
                    L"fSphericalAberration",
                    defaults.glare.sphericalAberration),
                .chromaticSpread = readFloat(
                    path,
                    kGlareSection,
                    L"fChromaticSpread",
                    defaults.glare.chromaticSpread),
                .kernelScale = readFloat(
                    path,
                    kGlareSection,
                    L"fKernelScale",
                    defaults.glare.kernelScale),
                .psfSharpness = readFloat(
                    path,
                    kGlareSection,
                    L"fPsfSharpness",
                    defaults.glare.psfSharpness),
                .psfNoiseFloor = readFloat(
                    path,
                    kGlareSection,
                    L"fPsfNoiseFloor",
                    defaults.glare.psfNoiseFloor),
            },
        });
        if (!master_settings::enabled(path)) {
            result.bloom.enabled = false;
            result.glare.enabled = false;
        }
        return result;
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Bloom/Glare settings loaded from '{}'; bloom={} thresholdEV={} intensity={} radius={}, glare={} thresholdEV={} intensity={} FFT={} apertureMode={} blades={}.",
            path.string(),
            result.bloom.enabled,
            result.bloom.thresholdEV,
            result.bloom.intensity,
            result.bloom.radius,
            result.glare.enabled,
            result.glare.thresholdEV,
            result.glare.intensity,
            result.glare.fftResolution,
            result.glare.apertureMode,
            result.glare.apertureBlades);
        return result;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        const auto path = settings_path::resolveIniPath();
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        const auto safe = sanitize(settings);
        auto success = writeValue(
            path,
            kBloomSection,
            L"bEnabled",
            safe.bloom.enabled ? L"1" : L"0");
        success = writeFloat(
                      path,
                      kBloomSection,
                      L"fThresholdEV",
                      safe.bloom.thresholdEV) && success;
        success = writeFloat(
                      path,
                      kBloomSection,
                      L"fIntensity",
                      safe.bloom.intensity) && success;
        success = writeFloat(
                      path,
                      kBloomSection,
                      L"fRadius",
                      safe.bloom.radius) && success;
        success = writeValue(
                      path,
                      kGlareSection,
                      L"bEnabled",
                      safe.glare.enabled ? L"1" : L"0") && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fThresholdEV",
                      safe.glare.thresholdEV) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fIntensity",
                      safe.glare.intensity) && success;
        success = writeUnsigned(
                      path,
                      kGlareSection,
                      L"iFftResolution",
                      safe.glare.fftResolution) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fPaddingRatio",
                      safe.glare.paddingRatio) && success;
        success = writeUnsigned(
                      path,
                      kGlareSection,
                      L"iApertureMode",
                      safe.glare.apertureMode) && success;
        success = writeUnsigned(
                      path,
                      kGlareSection,
                      L"iApertureBlades",
                      safe.glare.apertureBlades) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fApertureRotationDegrees",
                      safe.glare.apertureRotationDegrees) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fFStop",
                      safe.glare.fStop) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fFresnelExponent",
                      safe.glare.fresnelExponent) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fSphericalAberration",
                      safe.glare.sphericalAberration) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fChromaticSpread",
                      safe.glare.chromaticSpread) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fKernelScale",
                      safe.glare.kernelScale) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fPsfSharpness",
                      safe.glare.psfSharpness) && success;
        success = writeFloat(
                      path,
                      kGlareSection,
                      L"fPsfNoiseFloor",
                      safe.glare.psfNoiseFloor) && success;
        return success;
    }
}
