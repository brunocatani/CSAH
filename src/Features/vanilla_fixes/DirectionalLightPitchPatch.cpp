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
            0xDC479311u,
            0x35F0293Eu,
            0xFBB954C5u,
            0xCC529728u,
        };

        // Exact unique stock context beginning at file offset 0xE74:
        //   lt r6.z, r2.y, cb2[12].y
        //   if_nz r6.z
        constexpr std::array<std::uint32_t, 11> kStockFinalCascadeCutoff{
            0x08000031u,
            0x00100042u,
            0x00000006u,
            0x0010001Au,
            0x00000002u,
            0x0020801Au,
            0x00000002u,
            0x0000000Cu,
            0x0304001Fu,
            0x0010002Au,
            0x00000006u,
        };

        // Preserve the comparison and force only the following IF true.
        constexpr std::array<std::uint32_t, 11> kPitchStableFinalCascade{
            0x08000031u,
            0x00100042u,
            0x00000006u,
            0x0010001Au,
            0x00000002u,
            0x0020801Au,
            0x00000002u,
            0x0000000Cu,
            0x0304001Fu,
            0x00004001u,
            0x3F800000u,
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

    bool patchStockDirectionalLightPitchCutoff(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        patchedBytecode.clear();
        if (!exactContainer(stockBytecode, kExactStockChecksum)) {
            return false;
        }
        const auto cutoffOffset = findUniqueSequence(
            stockBytecode,
            kStockFinalCascadeCutoff);
        if (!cutoffOffset || *cutoffOffset != 0xE74) {
            return false;
        }

        auto candidate = std::vector<std::byte>{
            stockBytecode.begin(),
            stockBytecode.end(),
        };
        auto bytes = std::span<std::byte>{ candidate };
        constexpr std::size_t predicateOperandIndex = 9;
        constexpr std::size_t predicateValueIndex = 10;
        if (!writeU32(
                bytes,
                *cutoffOffset +
                    predicateOperandIndex * sizeof(std::uint32_t),
                kPitchStableFinalCascade[predicateOperandIndex]) ||
            !writeU32(
                bytes,
                *cutoffOffset +
                    predicateValueIndex * sizeof(std::uint32_t),
                kPitchStableFinalCascade[predicateValueIndex]) ||
            !linear_lighting::recomputeDxbcChecksum(bytes) ||
            !exactContainer(candidate, kExactPatchedChecksum) ||
            findUniqueSequence(candidate, kStockFinalCascadeCutoff) ||
            findUniqueSequence(candidate, kPitchStableFinalCascade) !=
                cutoffOffset) {
            return false;
        }

        patchedBytecode.swap(candidate);
        return true;
    }
}
