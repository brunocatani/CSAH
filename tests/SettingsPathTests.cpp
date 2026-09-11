#include "support/SettingsPath.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Settings path test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    namespace settings_path = community_shaders::settings_path;

    require(
        settings_path::fromDocuments({}).empty(),
        "empty Documents path must fail closed");

    const std::filesystem::path documents = LR"(C:\Users\Test\Documents)";
    const auto actual = settings_path::fromDocuments(documents);
    const auto expected = documents /
        L"My Games" /
        L"Fallout4VR" /
        L"Mods_Config" / L"FO4VRCommunityShaders" /
        L"FO4VRCommunityShaders.ini";
    require(actual == expected, "ROCK-style My Games path contract");
    require(
        actual.parent_path().filename() == L"FO4VRCommunityShaders",
        "plugin must own a dedicated configuration folder");
    require(
        actual.filename() == L"FO4VRCommunityShaders.ini",
        "INI filename contract");

    const auto resolved = settings_path::resolveIniPath();
    require(!resolved.empty(), "Windows Documents folder must resolve");
    require(
        resolved.parent_path().filename() ==
            L"FO4VRCommunityShaders",
        "resolved path must use the dedicated configuration folder");
    require(
        resolved.filename() == L"FO4VRCommunityShaders.ini",
        "resolved path must use the INI filename contract");

    std::cout << "Settings path tests passed.\n";
    return EXIT_SUCCESS;
}
