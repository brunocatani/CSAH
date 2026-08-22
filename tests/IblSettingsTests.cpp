#include "Features/ibl/IblSettingsStore.h"

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
            std::cerr << "IBL settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("FO4VRCommunityShaders-IblSettings-" +
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
    using community_shaders::ibl::loadSettings;
    using community_shaders::ibl::parseBoolean;

    require(parseBoolean(L" TRUE ") == true, "true syntax");
    require(parseBoolean(L"off") == false, "off syntax");
    require(!parseBoolean(L"banana"), "invalid syntax");

    TemporaryIni ini;
    require(loadSettings(ini.path()).enabled, "missing file default");
    ini.write(
        "[ImageBasedLighting]\n"
        "bEnabled=0\n"
        "bDiffuseEnabled=0\n"
        "fDiffuseLevel=1.35\n"
        "[DynamicCubemaps]\n"
        "bEnabled=0\n");
    const auto configured = loadSettings(ini.path());
    require(!configured.enabled, "disabled owned key");
    require(
        !configured.dynamicCubemapsEnabled,
        "disabled Dynamic Cubemaps key");
    require(!configured.diffuseEnabled, "disabled diffuse key");
    require(
        std::abs(configured.diffuseLevel - 1.35f) < 1.0e-6f,
        "diffuse level key");
    ini.write("[ImageBasedLighting]\nbEnabled=on\n");
    require(loadSettings(ini.path()).enabled, "enabled owned key");
    ini.write("[ImageBasedLighting]\nbEnabled=broken\n");
    require(loadSettings(ini.path()).enabled, "malformed value default");
    ini.write("[LinearLighting]\nbEnabled=0\n");
    const auto independent = loadSettings(ini.path());
    require(independent.enabled, "independent section");
    require(
        independent.dynamicCubemapsEnabled,
        "Dynamic Cubemaps default");
    require(independent.diffuseEnabled, "diffuse default");
    require(
        std::abs(independent.diffuseLevel - 1.0f) < 1.0e-6f,
        "diffuse level default");

    ini.write(
        "[ImageBasedLighting]\n"
        "fDiffuseLevel=99\n");
    require(
        loadSettings(ini.path()).diffuseLevel == 2.0f,
        "diffuse level upper clamp");

    std::cout << "IBL settings tests passed.\n";
    return EXIT_SUCCESS;
}
