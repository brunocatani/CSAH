#pragma once

#include <array>
#include <string_view>

namespace community_shaders::dlaa
{
    struct StreamlinePayloadContract
    {
        std::wstring_view fileName;
        std::string_view sha256;
    };

    inline constexpr std::array<StreamlinePayloadContract, 4>
        kStreamlinePayloadContracts{ {
            { L"sl.interposer.dll", "@STREAMLINE_INTERPOSER_SHA256@" },
            { L"sl.common.dll", "@STREAMLINE_COMMON_SHA256@" },
            { L"sl.dlss.dll", "@STREAMLINE_DLSS_SHA256@" },
            { L"nvngx_dlss.dll", "@STREAMLINE_NGX_SHA256@" },
        } };
}
