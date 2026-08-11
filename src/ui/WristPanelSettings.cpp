#include "ui/WristPanelSettings.h"

#include <Windows.h>

#include <array>
#include <system_error>

namespace community_shaders::ui::wrist_panel_settings
{
    namespace
    {
        constexpr auto kSection = L"PrismaPanel";
        constexpr auto kEnabledKey = L"bEnabled";

        [[nodiscard]] constexpr bool whitespace(wchar_t value) noexcept
        {
            return value == L' ' || value == L'\t' || value == L'\r' ||
                value == L'\n';
        }

        [[nodiscard]] constexpr wchar_t asciiLower(wchar_t value) noexcept
        {
            return value >= L'A' && value <= L'Z' ?
                static_cast<wchar_t>(value - L'A' + L'a') :
                value;
        }

        [[nodiscard]] constexpr bool equalsIgnoreAsciiCase(
            std::wstring_view left,
            std::wstring_view right) noexcept
        {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                if (asciiLower(left[index]) != asciiLower(right[index])) {
                    return false;
                }
            }
            return true;
        }
    }

    std::optional<bool> parseBoolean(std::wstring_view text) noexcept
    {
        while (!text.empty() && whitespace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && whitespace(text.back())) {
            text.remove_suffix(1);
        }
        if (text == L"1" || equalsIgnoreAsciiCase(text, L"true") ||
            equalsIgnoreAsciiCase(text, L"yes") ||
            equalsIgnoreAsciiCase(text, L"on")) {
            return true;
        }
        if (text == L"0" || equalsIgnoreAsciiCase(text, L"false") ||
            equalsIgnoreAsciiCase(text, L"no") ||
            equalsIgnoreAsciiCase(text, L"off")) {
            return false;
        }
        return std::nullopt;
    }

    LoadResult load(const std::filesystem::path& iniPath) noexcept
    {
        LoadResult result{};
        std::error_code fileError;
        if (iniPath.empty() ||
            !std::filesystem::is_regular_file(iniPath, fileError) ||
            fileError) {
            return result;
        }

        std::array<wchar_t, 64> value{};
        const auto length = GetPrivateProfileStringW(
            kSection,
            kEnabledKey,
            L"",
            value.data(),
            static_cast<DWORD>(value.size()),
            iniPath.c_str());
        if (length == 0) {
            return result;
        }

        result.keyPresent = true;
        const auto parsed = parseBoolean(
            std::wstring_view(value.data(), length));
        if (!parsed) {
            result.valueValid = false;
            return result;
        }
        result.enabled = *parsed;
        return result;
    }
}
