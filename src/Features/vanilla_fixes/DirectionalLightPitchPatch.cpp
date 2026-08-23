#include "Features/vanilla_fixes/DirectionalLightPitchPatch.h"

#include "Features/linear_lighting/DxbcChecksum.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::size_t kExactStockSize = 9388;
        constexpr std::size_t kChecksumOffset = 4;
        constexpr std::size_t kDeclaredSizeOffset = 24;
        constexpr std::array<std::uint32_t, 4> kExactStockChecksum{
            0xEE23B97Eu,
            0x314D542Du,
            0xE122666Du,
            0xC065151Fu,
        };
        constexpr std::array<std::uint32_t, 4> kExactPatchedChecksum{
            0x2B65726Du,
            0x9676D0E0u,
            0x3A3DBF34u,
            0x609D7C67u,
        };
        // Exact unique stock suffix beginning at file offset 0x13D4:
        //   add r0.w, r0.w, l(-1.0)
        //   mad r0.w, r1.w, r0.w, l(1.0)
        // The incoming r0.w is the split-blended cascade result and r1.w is
        // the high-power radial confidence derived from receiver distance.
        constexpr std::array<std::uint32_t, 16> kStockRadialDistanceFade{
            0x07000000u,
            0x00100082u,
            0x00000000u,
            0x0010003Au,
            0x00000000u,
            0x00004001u,
            0xBF800000u,
            0x09000032u,
            0x00100082u,
            0x00000000u,
            0x0010003Au,
            0x00000001u,
            0x0010003Au,
            0x00000000u,
            0x00004001u,
            0x3F800000u,
        };

        // Copy the incoming cascade result without the (shadow - 1) + 1
        // fractional round trip, then publish it while retaining the original
        // two-instruction token lengths. The computed r1.w is overwritten
        // before its next read.
        constexpr std::array<std::uint32_t, 16> kRadialDistanceFadeBypassed{
            0x07000038u,
            0x00100082u,
            0x00000000u,
            0x0010003Au,
            0x00000000u,
            0x00004001u,
            0x3F800000u,
            0x09000032u,
            0x00100082u,
            0x00000000u,
            0x00004001u,
            0x00000000u,
            0x0010003Au,
            0x00000000u,
            0x0010003Au,
            0x00000000u,
        };

        [[nodiscard]] bool readU32(
            std::span<const std::byte> bytes,
            const std::size_t offset,
            std::uint32_t& value) noexcept
        {
            if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return true;
        }

        [[nodiscard]] bool writeU32(
            std::span<std::byte> bytes,
            const std::size_t offset,
            const std::uint32_t value) noexcept
        {
            if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
            return true;
        }

        [[nodiscard]] bool exactContainer(
            const std::span<const std::byte> bytes,
            const std::array<std::uint32_t, 4>& checksum) noexcept
        {
            constexpr std::array<std::byte, 4> magic{
                std::byte{ 'D' },
                std::byte{ 'X' },
                std::byte{ 'B' },
                std::byte{ 'C' },
            };
            if (bytes.size() != kExactStockSize ||
                !std::equal(magic.begin(), magic.end(), bytes.begin())) {
                return false;
            }
            std::uint32_t declaredSize{};
            if (!readU32(bytes, kDeclaredSizeOffset, declaredSize) ||
                declaredSize != bytes.size()) {
                return false;
            }
            for (std::size_t index = 0; index < checksum.size(); ++index) {
                std::uint32_t actual{};
                if (!readU32(
                        bytes,
                        kChecksumOffset + index * sizeof(std::uint32_t),
                        actual) ||
                    actual != checksum[index]) {
                    return false;
                }
            }
            return true;
        }

        template <std::size_t Size>
        [[nodiscard]] std::optional<std::size_t> findUniqueSequence(
            const std::span<const std::byte> bytes,
            const std::array<std::uint32_t, Size>& sequence) noexcept
        {
            const auto byteCount = sequence.size() * sizeof(std::uint32_t);
            if (byteCount > bytes.size()) {
                return std::nullopt;
            }
            std::optional<std::size_t> match;
            for (std::size_t offset = 20;
                 offset <= bytes.size() - byteCount;
                 offset += sizeof(std::uint32_t)) {
                if (std::memcmp(
                        bytes.data() + offset,
                        sequence.data(),
                        byteCount) != 0) {
                    continue;
                }
                if (match) {
                    return std::nullopt;
                }
                match = offset;
            }
            return match;
        }
    }

    bool patchStockDirectionalLightRadialFade(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        patchedBytecode.clear();
        if (!exactContainer(stockBytecode, kExactStockChecksum)) {
            return false;
        }
        const auto fadeOffset = findUniqueSequence(
            stockBytecode,
            kStockRadialDistanceFade);
        if (!fadeOffset || *fadeOffset != 0x13D4) {
            return false;
        }

        auto candidate = std::vector<std::byte>{
            stockBytecode.begin(),
            stockBytecode.end(),
        };
        auto bytes = std::span<std::byte>{ candidate };
        for (std::size_t index = 0;
             index < kRadialDistanceFadeBypassed.size();
             ++index) {
            if (!writeU32(
                    bytes,
                    *fadeOffset + index * sizeof(std::uint32_t),
                    kRadialDistanceFadeBypassed[index])) {
                return false;
            }
        }
        if (!linear_lighting::recomputeDxbcChecksum(bytes) ||
            !exactContainer(candidate, kExactPatchedChecksum) ||
            findUniqueSequence(candidate, kStockRadialDistanceFade) ||
            findUniqueSequence(candidate, kRadialDistanceFadeBypassed) !=
                fadeOffset) {
            return false;
        }

        patchedBytecode.swap(candidate);
        return true;
    }
}
