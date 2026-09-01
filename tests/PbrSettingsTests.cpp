#include "Features/pbr/PbrSettingsStore.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "PBR settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("FO4VRCommunityShaders-PbrSettings-" +
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
    using community_shaders::pbr::loadSettings;

    TemporaryIni ini;
    require(loadSettings(ini.path()).enabled, "missing file default");
    ini.write(
        "[PBR]\n"
        "bEnabled=0\n"
        "bDirectGGX=0\n"
        "bEnvironmentFresnel=0\n"
        "fRoughnessMultiplier=1.5\n"
        "fBaseF0Multiplier=0.5\n");
    const auto configured = loadSettings(ini.path());
    require(!configured.enabled, "disabled owned key");
    require(!configured.directGgx, "direct GGX key");
    require(!configured.environmentFresnel, "environment Fresnel key");
    require(std::fabs(configured.roughnessMultiplier - 1.5f) < 1.0e-6f,
        "roughness multiplier key");
    require(std::fabs(configured.baseF0Multiplier - 0.5f) < 1.0e-6f,
        "base F0 key");

    ini.write(
        "[CommunityShaders]\n"
        "bEnabled=0\n"
        "[PBR]\n"
        "bEnabled=1\n");
    require(!loadSettings(ini.path()).enabled,
        "Community Shaders master gate did not disable PBR");

    ini.write(
        "[PBR]\n"
        "fRoughnessMultiplier=99\n"
        "fMinimumF0=-1\n"
        "fDirectLightingScale=99\n");
    const auto clamped = loadSettings(ini.path());
    require(clamped.roughnessMultiplier == 4.0f,
        "roughness upper clamp");
    require(clamped.minimumF0 == 0.0f, "minimum F0 lower clamp");
    require(clamped.directLightingScale == 4.0f,
        "direct-light scale upper clamp");

    std::cout << "PBR settings tests passed.\n";
    return EXIT_SUCCESS;
}
