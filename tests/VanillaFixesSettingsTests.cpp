#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Vanilla Fixes settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("CSAH-VanillaFixes-" +
                    std::to_string(
                        std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count()) +
                    ".ini"))
        {}

        ~TemporaryIni()
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        void write(const char* text) const
        {
            std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
            stream << text;
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };
}

int main()
{
    using csah::vanilla_fixes::Settings;
    using csah::vanilla_fixes::DirectionalLightDiagnosticMode;
    using csah::vanilla_fixes::loadSettings;
    using csah::vanilla_fixes::saveSettings;

    TemporaryIni ini;
    require(loadSettings(ini.path()) == Settings{}, "missing-file defaults");

    ini.write(
        "[CommunityShaders]\n"
        "bEnabled=0\n"
        "[VanillaFixes]\n"
        "bEnabled=1\n");
    require(
        loadSettings(ini.path()).enabled,
        "Community Shaders visual gate disabled independent Vanilla Fixes");

    ini.write(
        "[VanillaFixes]\n"
        "bEnabled=0\n"
        "bPrecipitationOcclusion=off\n"
        "bAllowImageSpaceModifiers=no\n"
        "bVrAllowSAO=false\n"
        "bVrAllowScreenSpaceReflections=0\n"
        "bNativeScreenSpaceMaterialPipeline=OFF\n"
        "bLensFlareVr=No\n"
        "bVrAllowFocusShadows=False\n"
        "bVrSunOcclusion=0\n");
    const auto disabled = loadSettings(ini.path());
    require(!disabled.enabled, "master key");
    require(!disabled.precipitationOcclusion, "precipitation key");
    require(!disabled.imageSpaceModifiers, "image-space key");
    require(!disabled.sao, "SAO key");
    require(!disabled.screenSpaceReflections, "SSLR key");
    require(
        !disabled.nativeScreenSpaceMaterialPipeline,
        "native screen-space material pipeline key");
    require(!disabled.lensFlare, "lens-flare key");
    require(!disabled.focusShadows, "focus-shadow key");
    require(!disabled.stereoSunOcclusion, "stereo Sun-occlusion key");

    ini.write(
        "[VanillaFixes]\n"
        "bEnabled=garbage\n"
        "bVrAllowSAO=garbage\n"
        "[LinearLighting]\n"
        "bEnabled=0\n");
    require(loadSettings(ini.path()) == Settings{}, "invalid-value fallback");

    ini.write(
        "[VanillaFixes]\n"
        "bEnabled=1\n"
        "[Diagnostics]\n"
        "iDirectionalLightingMode=15\n");
    require(
        loadSettings(ini.path()).directionalLightDiagnosticMode ==
            DirectionalLightDiagnosticMode::cubemapLookupOnly,
        "cubemap diagnostic key");

    ini.write(
        "[VanillaFixes]\n"
        "bEnabled=1\n"
        "[Diagnostics]\n"
        "iDirectionalLightingMode=16\n");
    require(
        loadSettings(ini.path()).directionalLightDiagnosticMode ==
            DirectionalLightDiagnosticMode::off,
        "out-of-range diagnostic fallback");

    const Settings mixed{
        .enabled = true,
        .precipitationOcclusion = false,
        .imageSpaceModifiers = true,
        .sao = false,
        .screenSpaceReflections = true,
        .nativeScreenSpaceMaterialPipeline = false,
        .lensFlare = true,
        .focusShadows = false,
        .stereoSunOcclusion = false,
        .directionalLightDiagnosticMode =
            DirectionalLightDiagnosticMode::directSpecularOnly,
    };
    require(saveSettings(ini.path(), mixed), "temporary INI save");
    require(loadSettings(ini.path()) == mixed, "save/load round trip");

    std::cout << "Vanilla Fixes settings tests passed.\n";
    return EXIT_SUCCESS;
}
