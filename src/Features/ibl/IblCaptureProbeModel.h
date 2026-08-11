#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace community_shaders::ibl
{
    struct CaptureProbeContract
    {
        std::size_t bytecodeSize{};
        std::array<std::uint8_t, 16> checksum{};
    };

    namespace detail
    {
        [[nodiscard]] consteval std::uint8_t hexNibble(char value)
        {
            if (value >= '0' && value <= '9') {
                return static_cast<std::uint8_t>(value - '0');
            }
            if (value >= 'a' && value <= 'f') {
                return static_cast<std::uint8_t>(value - 'a' + 10);
            }
            if (value >= 'A' && value <= 'F') {
                return static_cast<std::uint8_t>(value - 'A' + 10);
            }
            return 0xFF;
        }

        template <std::size_t N>
        [[nodiscard]] consteval std::array<std::uint8_t, 16> dxbcChecksum(
            const char (&text)[N])
        {
            static_assert(N == 33, "DXBC checksum must contain 32 hex digits");
            std::array<std::uint8_t, 16> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                const auto high = hexNibble(text[index * 2]);
                const auto low = hexNibble(text[index * 2 + 1]);
                if (high > 0x0F || low > 0x0F) {
                    throw "DXBC checksum contains a non-hex character";
                }
                result[index] = static_cast<std::uint8_t>((high << 4) | low);
            }
            return result;
        }
    }

    // All unique TextureCubeArray-t8 DFComposite PS identities from the
    // locally authoritative Shaders012_VR.fxp. These are the exact existing
    // environment consumers and therefore the diagnostic capture boundary.
    // The build verifier checks identity, declarations, aliases, and complete
    // 41-identity/83-record coverage against the local FXP.
    inline constexpr std::array<CaptureProbeContract, 41>
        kCaptureProbeContracts{ {
            { 11104, detail::dxbcChecksum("05f5cabf7edcaaf695805ffb328ad36d") },
            { 11208, detail::dxbcChecksum("05fa298224cd5b2e250b7a7521131fba") },
            { 3424, detail::dxbcChecksum("127c7bb7c39363bfb67e02cbeb310818") },
            { 5204, detail::dxbcChecksum("12876af2f385b41c0ddd4558b859f5f7") },
            { 5096, detail::dxbcChecksum("182696bc27b5ed3394b950dcc15266af") },
            { 10888, detail::dxbcChecksum("237ceacb0120711a605b69c1bf788b3b") },
            { 9252, detail::dxbcChecksum("33ca4f05dc8bbd98e96859113bea32c9") },
            { 5016, detail::dxbcChecksum("3843512b6ec9938745dda58b4cb7de15") },
            { 9464, detail::dxbcChecksum("3b4ace2ce4b13a967fa38dc8580e99a5") },
            { 3588, detail::dxbcChecksum("3c3cc20b57a82e9f490fdffbec68d273") },
            { 5320, detail::dxbcChecksum("3ec58c35949bf97ae2377b909d61e7f7") },
            { 5124, detail::dxbcChecksum("4c76cbf23925bac1b6bcf47508f6644d") },
            { 9564, detail::dxbcChecksum("4d6870ba7d5498e729b3195f7dcdf978") },
            { 5312, detail::dxbcChecksum("4eff5e0df8329d29e1721897f428d237") },
            { 5408, detail::dxbcChecksum("5a125823477142e0fdc16714247367dd") },
            { 3272, detail::dxbcChecksum("618ab40ad839b752b54678586d987365") },
            { 3304, detail::dxbcChecksum("67fc05f4bebd9fff8a81e6f64fe08b29") },
            { 5160, detail::dxbcChecksum("69644eba8219584adad6054886da80a0") },
            { 5192, detail::dxbcChecksum("83191af2685bcd2c24e3a546eb62a132") },
            { 9348, detail::dxbcChecksum("93edc6af41cbb2d290962e995a4fce25") },
            { 3372, detail::dxbcChecksum("93f4d80a489c3c1f9fb8457bbbb6da4b") },
            { 10992, detail::dxbcChecksum("a1c51753f0ccd83f96990dcd0940a536") },
            { 10780, detail::dxbcChecksum("a2f0b6c3c1d83fbc750048fbe30f062a") },
            { 5204, detail::dxbcChecksum("a5f7c7bd0cba2f606c45c253eb7611e9") },
            { 10996, detail::dxbcChecksum("a8327fffef3f9dd21f6669f912185203") },
            { 3520, detail::dxbcChecksum("aa07c7869032eb3a4bd1bd1646efda22") },
            { 3392, detail::dxbcChecksum("af6880283597f0a13f3a390edd684848") },
            { 3492, detail::dxbcChecksum("b4314b330c8d12a9f08542b8a5c022e9") },
            { 3384, detail::dxbcChecksum("bcd6111c0bad9e847797a6d031108aa8") },
            { 5300, detail::dxbcChecksum("bf35d56c338554073f37c94a66305252") },
            { 9248, detail::dxbcChecksum("c22cdaf9eba9124b3b7ef28e5013744b") },
            { 3488, detail::dxbcChecksum("c72125641444c8ac71ae923b3567954b") },
            { 9136, detail::dxbcChecksum("c80f14ce794c85a162db770bcb180099") },
            { 3316, detail::dxbcChecksum("ccb5c1700aca810b0cd03e080c3623bb") },
            { 3284, detail::dxbcChecksum("d2ea118dbb2a5fe0d2009f25cb272343") },
            { 5084, detail::dxbcChecksum("d4640e2752ee85c9c5872b4d8d7c04ed") },
            { 9036, detail::dxbcChecksum("e633fa0d0aab32daa3e9f28588c9e7bc") },
            { 9352, detail::dxbcChecksum("e7b1971482313136cd85d46a28d18186") },
            { 11100, detail::dxbcChecksum("eb839491ab08ee92fc485990945bbec5") },
            { 5104, detail::dxbcChecksum("f77db52d3d4d4a3a3ab0b02d4ea5ffa0") },
            { 11316, detail::dxbcChecksum("fc24da7bbdad0360e221fc9e45ad2e9f") },
        } };

    [[nodiscard]] inline std::uint16_t matchCaptureProbeContract(
        const void* bytecode,
        std::size_t bytecodeSize) noexcept
    {
        if (!bytecode || bytecodeSize < 20) {
            return 0;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
        if (bytes[0] != 'D' || bytes[1] != 'X' || bytes[2] != 'B' ||
            bytes[3] != 'C') {
            return 0;
        }
        for (std::size_t contractIndex = 0;
             contractIndex < kCaptureProbeContracts.size();
             ++contractIndex) {
            const auto& contract = kCaptureProbeContracts[contractIndex];
            if (contract.bytecodeSize != bytecodeSize) {
                continue;
            }
            bool matches = true;
            for (std::size_t checksumIndex = 0;
                 checksumIndex < contract.checksum.size();
                 ++checksumIndex) {
                if (bytes[4 + checksumIndex] !=
                    contract.checksum[checksumIndex]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return static_cast<std::uint16_t>(contractIndex + 1);
            }
        }
        return 0;
    }
}
