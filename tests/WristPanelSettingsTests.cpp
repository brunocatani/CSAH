#include "ui/WristPanelSettings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    using community_shaders::ui::wrist_panel_settings::load;
    using community_shaders::ui::wrist_panel_settings::parseBoolean;

    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("FO4VRCommunityShaders-WristPanelSettings-" +
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

    void testBooleanSyntax()
    {
        expect(parseBoolean(L"true") == true, "true is accepted");
        expect(parseBoolean(L" FALSE ") == false,
            "trimmed case-insensitive false is accepted");
        expect(parseBoolean(L"1") == true, "numeric one is accepted");
        expect(parseBoolean(L"0") == false, "numeric zero is accepted");
        expect(parseBoolean(L"On") == true, "on is accepted");
        expect(parseBoolean(L"no") == false, "no is accepted");
        expect(!parseBoolean(L"disabled"), "unknown text is rejected");
    }

    void testIniLoading()
    {
        TemporaryIni ini;
        const auto missing = load(ini.path());
        expect(missing.enabled, "missing file preserves enabled default");
        expect(!missing.keyPresent, "missing file has no owned key");
        expect(missing.valueValid, "missing file is not malformed");

        ini.write("[PrismaPanel]\nbEnabled=false\n");
        const auto disabled = load(ini.path());
        expect(!disabled.enabled, "false INI value disables the panel");
        expect(disabled.keyPresent, "owned key is detected");
        expect(disabled.valueValid, "false INI value is valid");

        ini.write("[PrismaPanel]\nbEnabled=banana\n");
        const auto malformed = load(ini.path());
        expect(malformed.enabled, "malformed value fails to enabled default");
        expect(malformed.keyPresent, "malformed owned key is present");
        expect(!malformed.valueValid, "malformed value is reported");

        ini.write("[LinearLighting]\nbEnabled=0\n");
        const auto unrelated = load(ini.path());
        expect(unrelated.enabled, "Linear Lighting toggle is independent");
        expect(!unrelated.keyPresent, "unrelated section is ignored");
    }
}

int main()
{
    testBooleanSyntax();
    testIniLoading();
    if (failures) {
        return 1;
    }
    std::cout << "Wrist panel settings tests passed.\n";
    return 0;
}
