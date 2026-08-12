#include "Features/ibl/IblSettingsStore.h"

#include <chrono>
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
    ini.write("[ImageBasedLighting]\nbEnabled=0\n");
    require(!loadSettings(ini.path()).enabled, "disabled owned key");
    ini.write("[ImageBasedLighting]\nbEnabled=on\n");
    require(loadSettings(ini.path()).enabled, "enabled owned key");
    ini.write("[ImageBasedLighting]\nbEnabled=broken\n");
    require(loadSettings(ini.path()).enabled, "malformed value default");
    ini.write("[LinearLighting]\nbEnabled=0\n");
    require(loadSettings(ini.path()).enabled, "independent section");

    std::cout << "IBL settings tests passed.\n";
    return EXIT_SUCCESS;
}
