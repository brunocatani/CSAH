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
            0xCE9BCEA1u,
            0x4184B8E9u,
            0xE6AD6315u,
            0x42AF088Bu,
        };
        // Exact unique stock output suffix beginning at file offset 0x2420:
        //   mul o1.xyz, r0.wwww, r1.xyzx
        //   mul r0.xyz, r0.wwww, r0.xyzx
        //   mov r0.w, l(0)
        //   div o0.xyzw, r0.xyzw, l(3, 3, 3, 3)
        //   mov o1.w, l(1)
        //   ret
        constexpr std::array<std::uint32_t, 35> kStockFinalOutputs{
            0x07000038u, 0x00102072u, 0x00000001u, 0x00100FF6u,
            0x00000000u, 0x00100246u, 0x00000001u, 0x07000038u,
            0x00100072u, 0x00000000u, 0x00100FF6u, 0x00000000u,
            0x00100246u, 0x00000000u, 0x05000036u, 0x00100082u,
            0x00000000u, 0x00004001u, 0x00000000u, 0x0A00000Eu,
            0x001020F2u, 0x00000000u, 0x00100E46u, 0x00000000u,
            0x00004002u, 0x40400000u, 0x40400000u, 0x40400000u,
            0x40400000u, 0x05000036u, 0x00102082u, 0x00000001u,
            0x00004001u, 0x3F800000u, 0x0100003Eu,
        };

        // Preserve the exact 35-token suffix length. Target zero carries the
        // three ownership channels in the native /3 accumulation scale with
        // alpha one; target one is intentionally unbound by the private draw.
        // Four one-token NOPs fill the stock suffix budget.
        constexpr std::array<std::uint32_t, 35> kOwnershipDiagnosticOutputs{
            // mov r0.x, r5.w
            0x05000036u, 0x00100012u, 0x00000000u, 0x0010003Au,
            0x00000005u,
            // mov r0.y, r8.x
            0x05000036u, 0x00100022u, 0x00000000u, 0x0010000Au,
            0x00000008u,
            // mov r0.z, r0.w
            0x05000036u, 0x00100042u, 0x00000000u, 0x0010003Au,
            0x00000000u,
            // mov r0.w, l(3.0)
            0x05000036u, 0x00100082u, 0x00000000u, 0x00004001u,
            0x40400000u,
            // div o0.xyzw, r0.xyzw, l(3, 3, 3, 3)
            0x0A00000Eu, 0x001020F2u, 0x00000000u, 0x00100E46u,
            0x00000000u, 0x00004002u, 0x40400000u, 0x40400000u,
            0x40400000u, 0x40400000u,
            0x0100003Au, 0x0100003Au, 0x0100003Au, 0x0100003Au,
            0x0100003Eu,
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

    bool patchStockDirectionalLightOwnershipDiagnostic(
        const std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        patchedBytecode.clear();
        if (!exactContainer(stockBytecode, kExactStockChecksum)) {
            return false;
        }
        const auto outputOffset = findUniqueSequence(
            stockBytecode,
            kStockFinalOutputs);
        if (!outputOffset || *outputOffset != 0x2420) {
            return false;
        }

        auto candidate = std::vector<std::byte>{
            stockBytecode.begin(),
            stockBytecode.end(),
        };
        auto bytes = std::span<std::byte>{ candidate };
        for (std::size_t index = 0;
             index < kOwnershipDiagnosticOutputs.size();
             ++index) {
            if (!writeU32(
                    bytes,
                    *outputOffset + index * sizeof(std::uint32_t),
                    kOwnershipDiagnosticOutputs[index])) {
                return false;
            }
        }
        if (!linear_lighting::recomputeDxbcChecksum(bytes) ||
            !exactContainer(candidate, kExactPatchedChecksum) ||
            findUniqueSequence(candidate, kStockFinalOutputs) ||
            findUniqueSequence(candidate, kOwnershipDiagnosticOutputs) !=
                outputOffset) {
            return false;
        }

        patchedBytecode.swap(candidate);
        return true;
    }
}
