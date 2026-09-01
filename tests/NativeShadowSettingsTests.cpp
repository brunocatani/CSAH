#include "Features/native_shadows/NativeShadowSettingsStore.h"

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
            std::cerr << "Native Shadows settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("FO4VRCommunityShaders-NativeShadows-" +
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
    using community_shaders::native_shadows::Settings;
    using community_shaders::native_shadows::loadSettings;
    using community_shaders::native_shadows::saveSettings;

    TemporaryIni ini;
    require(loadSettings(ini.path()) == Settings{}, "missing-file defaults");

    ini.write(
        "[CommunityShaders]\n"
        "bEnabled=0\n"
        "[NativeShadows]\n"
        "bEnabled=1\n");
    require(
        loadSettings(ini.path()).enabled,
        "Community Shaders visual gate disabled independent Native Shadows");

    ini.write(
        "[NativeShadows]\n"
        "bEnabled=off\n"
        "bExtendedDirectionalCascades=no\n"
        "bTiledDeferredLighting=false\n"
        "fDirectionalShadowDistance=24000\n"
        "fCascadeBlendDistance=48\n"
        "iOrthographicShadowFilter=4\n");
    const auto disabled = loadSettings(ini.path());
    require(!disabled.enabled, "master key");
    require(
        !disabled.extendedDirectionalCascades,
        "extended-cascade key");
    require(!disabled.tiledDeferredLighting, "tiled-lighting key");
    require(
        disabled.directionalShadowDistance == 24000.0f,
        "fixed-distance key");
    require(
        disabled.cascadeBlendDistance == 48.0f,
        "cascade-blend key");
    require(
        disabled.orthographicShadowFilter == 4,
        "orthographic-filter key");

    ini.write(
        "[NativeShadows]\n"
        "bEnabled=garbage\n"
        "fDirectionalShadowDistance=nan\n"
        "fCascadeBlendDistance=nan\n"
        "iOrthographicShadowFilter=-1\n");
    require(loadSettings(ini.path()) == Settings{}, "invalid-value fallback");

    ini.write(
        "[NativeShadows]\n"
        "fDirectionalShadowDistance=100\n"
        "fCascadeBlendDistance=-20\n");
    require(
        loadSettings(ini.path()).directionalShadowDistance == 3000.0f,
        "minimum clamp");
    require(
        loadSettings(ini.path()).cascadeBlendDistance == 0.0f,
        "cascade-blend minimum clamp");

    ini.write(
        "[NativeShadows]\n"
        "fDirectionalShadowDistance=90000\n"
        "fCascadeBlendDistance=9000\n"
        "iOrthographicShadowFilter=99\n");
    require(
        loadSettings(ini.path()).directionalShadowDistance == 50000.0f,
        "maximum clamp");
    require(
        loadSettings(ini.path()).cascadeBlendDistance == 5000.0f,
        "cascade-blend maximum clamp");
    require(
        loadSettings(ini.path()).orthographicShadowFilter == 5,
        "orthographic-filter maximum clamp");

    const Settings custom{
        .enabled = true,
        .extendedDirectionalCascades = false,
        .tiledDeferredLighting = true,
        .directionalShadowDistance = 18000.0f,
        .cascadeBlendDistance = 72.0f,
        .orthographicShadowFilter = 3,
    };
    require(saveSettings(ini.path(), custom), "temporary INI save");
    require(loadSettings(ini.path()) == custom, "save/load round trip");

    std::cout << "Native Shadows settings tests passed.\n";
    return EXIT_SUCCESS;
}
