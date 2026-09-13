#include "Features/dlaa/DlaaSettingsStore.h"

#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <array>
#include <cwchar>
#include <string>

namespace csah::dlaa
{
    namespace
    {
        constexpr wchar_t kSection[] = L"DLAA";

        [[nodiscard]] bool readBoolean(
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

        [[nodiscard]] std::uint32_t readUnsigned(
            const std::filesystem::path& path,
            const wchar_t* key,
            std::uint32_t fallback) noexcept
        {
            return static_cast<std::uint32_t>(GetPrivateProfileIntW(
                kSection,
                key,
                static_cast<int>(fallback),
                path.c_str()));
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            std::array<wchar_t, 64> text{};
            wchar_t fallbackText[64]{};
            _snwprintf_s(
                fallbackText,
                std::size(fallbackText),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(fallback));
            const auto count = GetPrivateProfileStringW(
                kSection,
                key,
                fallbackText,
                text.data(),
                static_cast<DWORD>(text.size()),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            const auto value = std::wcstof(text.data(), &end);
            return end && end != text.data() ? value : fallback;
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
            .mode = static_cast<Mode>(readUnsigned(
                path,
                L"iMode",
                static_cast<std::uint32_t>(defaults.mode))),
            .modelPreset = static_cast<ModelPreset>(readUnsigned(
                path,
                L"iModelPreset",
                static_cast<std::uint32_t>(defaults.modelPreset))),
            .motionVectorRepair = readBoolean(
                path,
                L"bMotionVectorRepair",
                defaults.motionVectorRepair),
            .sharpening = readBoolean(
                path,
                L"bPostSharpening",
                defaults.sharpening),
            .sharpness = readFloat(
                path,
                L"fSharpness",
                defaults.sharpness),
            .centerWidth = readFloat(
                path,
                L"fCenterWidth",
                defaults.centerWidth),
            .centerHeight = readFloat(
                path,
                L"fCenterHeight",
                defaults.centerHeight),
            .centerFeatherPixels = readFloat(
                path,
                L"fCenterFeatherPixels",
                defaults.centerFeatherPixels),
            .visualizeCenter = readBoolean(
                path,
                L"bVisualizeCenter",
                defaults.visualizeCenter),
            .hardResetOnLoad = readBoolean(
                path,
                L"bHardResetOnLoad",
                defaults.hardResetOnLoad),
            .verboseDiagnostics = readBoolean(
                path,
                L"bVerboseDiagnostics",
                defaults.verboseDiagnostics),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto settings = loadSettings(path);
        logging::info(
            "Upscaling settings loaded from '{}'; enabled={}, mode={}, modelPreset={}, motionVectorRepair={}, CAS={}, sharpness={}, center={}x{}, feather={}px, visualizeCenter={}, hardResetOnLoad={}, diagnostics={}.",
            path.string(),
            settings.enabled,
            modeName(settings.mode),
            static_cast<std::uint32_t>(settings.modelPreset),
            settings.motionVectorRepair,
            settings.sharpening,
            settings.sharpness,
            settings.centerWidth,
            settings.centerHeight,
            settings.centerFeatherPixels,
            settings.visualizeCenter,
            settings.hardResetOnLoad,
            settings.verboseDiagnostics);
        return settings;
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
        wchar_t sharpness[64]{};
        wchar_t centerWidth[64]{};
        wchar_t centerHeight[64]{};
        wchar_t centerFeather[64]{};
        wchar_t mode[32]{};
        wchar_t modelPreset[32]{};
        _snwprintf_s(
            sharpness,
            std::size(sharpness),
            _TRUNCATE,
            L"%.9g",
            static_cast<double>(safe.sharpness));
        _snwprintf_s(centerWidth, std::size(centerWidth), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.centerWidth));
        _snwprintf_s(centerHeight, std::size(centerHeight), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.centerHeight));
        _snwprintf_s(centerFeather, std::size(centerFeather), _TRUNCATE, L"%.9g",
            static_cast<double>(safe.centerFeatherPixels));
        _snwprintf_s(
            mode,
            std::size(mode),
            _TRUNCATE,
            L"%u",
            static_cast<std::uint32_t>(safe.mode));
        _snwprintf_s(
            modelPreset,
            std::size(modelPreset),
            _TRUNCATE,
            L"%u",
            static_cast<std::uint32_t>(safe.modelPreset));
        auto success = writeValue(
            path,
            L"bEnabled",
            safe.enabled ? L"1" : L"0");
        success = writeValue(path, L"iMode", mode) && success;
        success = writeValue(path, L"iModelPreset", modelPreset) && success;
        success = writeValue(
                      path,
                      L"bMotionVectorRepair",
                      safe.motionVectorRepair ? L"1" : L"0") &&
            success;
        success = writeValue(
                      path,
                      L"bPostSharpening",
                      safe.sharpening ? L"1" : L"0") &&
            success;
        success = writeValue(path, L"fSharpness", sharpness) && success;
        success = writeValue(path, L"fCenterWidth", centerWidth) && success;
        success = writeValue(path, L"fCenterHeight", centerHeight) && success;
        success = writeValue(path, L"fCenterFeatherPixels", centerFeather) &&
            success;
        success = writeValue(
                      path,
                      L"bVisualizeCenter",
                      safe.visualizeCenter ? L"1" : L"0") &&
            success;
        success = writeValue(
                      path,
                      L"bHardResetOnLoad",
                      safe.hardResetOnLoad ? L"1" : L"0") &&
            success;
        success = writeValue(
                      path,
                      L"bVerboseDiagnostics",
                      safe.verboseDiagnostics ? L"1" : L"0") &&
            success;
        return success;
    }
}
