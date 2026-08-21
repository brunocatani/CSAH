#include "Features/contact_shadows/ContactShadowSettingsStore.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Contact Shadows settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("FO4VRCommunityShaders-ContactShadows-" +
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
    using community_shaders::contact_shadows::loadSettings;
    using community_shaders::contact_shadows::parseBoolean;

    require(parseBoolean(L" TRUE ") == true, "true syntax");
    require(parseBoolean(L"off") == false, "off syntax");
    require(!parseBoolean(L"invalid"), "invalid syntax");

    TemporaryIni ini;
    const auto defaults = loadSettings(ini.path());
    require(defaults.enabled, "missing file enabled default");
    require(defaults.foveated, "missing file foveated default");
    require(defaults.sampleCount == 8, "missing file sample default");
    require(defaults.fadeDistance == 2048.0f, "missing file fade default");

    ini.write(
        "[ContactShadows]\n"
        "bEnabled=0\n"
        "bFoveated= false \n"
        "fStrength=0.62\n"
        "fMaxDistance=72\n"
        "fFadeDistance=1536\n"
        "fThickness=0.009\n"
        "iSampleCount=6\n");
    const auto configured = loadSettings(ini.path());
    require(!configured.enabled, "disabled owned key");
    require(!configured.foveated, "disabled foveation key");
    require(
        std::abs(configured.strength - 0.62f) < 1.0e-6f,
        "strength key");
    require(configured.maxDistance == 72.0f, "ray-distance key");
    require(configured.fadeDistance == 1536.0f, "fade-distance key");
    require(
        std::abs(configured.thickness - 0.009f) < 1.0e-6f,
        "thickness key");
    require(configured.sampleCount == 6, "sample-count key");

    ini.write(
        "[ContactShadows]\n"
        "fStrength=9\n"
        "fMaxDistance=1\n"
        "fFadeDistance=99999\n"
        "fThickness=1\n"
        "iSampleCount=0\n");
    const auto clamped = loadSettings(ini.path());
    require(clamped.strength == 1.0f, "strength upper clamp");
    require(clamped.maxDistance == 8.0f, "ray-distance lower clamp");
    require(clamped.fadeDistance == 8192.0f, "fade-distance upper clamp");
    require(clamped.thickness == 0.05f, "thickness upper clamp");
    require(clamped.sampleCount == 2, "sample-count lower clamp");

    ini.write("[ContactShadows]\niSampleCount=99\n");
    require(
        loadSettings(ini.path()).sampleCount == 8,
        "sample-count upper clamp");

    ini.write("[LinearLighting]\nbEnabled=0\n");
    require(loadSettings(ini.path()).enabled, "independent INI section");

    std::cout << "Contact Shadows settings tests passed.\n";
    return EXIT_SUCCESS;
}
