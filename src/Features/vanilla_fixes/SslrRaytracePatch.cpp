#include "Features/vanilla_fixes/SslrRaytracePatch.h"

#include "Features/linear_lighting/DxbcChecksum.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::size_t kDeclaredSizeOffset = 24;
        constexpr std::size_t kChunkCountOffset = 28;
        constexpr std::size_t kChunkOffsetsOffset = 32;
        constexpr std::uint32_t kShexTag = 0x58454853u;
        constexpr std::uint32_t kShdrTag = 0x52444853u;

        // Stock instructions 2481..2486. This is the global-atlas radial fade
        // centered at (0.5, 0.5). Exact stock shader identity is checked by
        // SslrFix before this byte-level contract is evaluated.
        constexpr std::array<std::uint32_t, 46> kStockFade{
            0x0A000000u, 0x001000C2u, 0x00000000u, 0x00100406u,
            0x00000006u, 0x00004002u, 0x00000000u, 0x00000000u,
            0xBF000000u, 0xBF000000u,
            0x0700000Fu, 0x00100042u, 0x00000000u, 0x00100AE6u,
            0x00000000u, 0x00100AE6u, 0x00000000u,
            0x0500004Bu, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u,
            0x07000000u, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x0010002Au, 0x00000000u,
            0x07000033u, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x00004001u, 0x3F800000u,
            0x0A000032u, 0x00100042u, 0x00000000u, 0x8010002Au,
            0x00000041u, 0x00000000u, 0x0010002Au, 0x00000000u,
            0x00004001u, 0x3F800000u,
        };

        // Equivalent fade in eye-local geometry without touching Bethesda's
        // 32-step DDA, hit tests, depth tolerance, ray-length fade, or sample.
        // localCenteredX = (abs(packedX - 0.5) - 0.25) * 4
        // localCenteredY = (packedY - 0.5) * 2
        // fade = saturate(1 - dot(localCentered, localCentered))
        constexpr std::array<std::uint32_t, 50> kStereoFade{
            0x0A000000u, 0x001000C2u, 0x00000000u, 0x00100406u,
            0x00000006u, 0x00004002u, 0x00000000u, 0x00000000u,
            0xBF000000u, 0xBF000000u,
            0x08000000u, 0x00100042u, 0x00000000u, 0x8010002Au,
            0x00000081u, 0x00000000u, 0x00004001u, 0xBE800000u,
            0x0A000038u, 0x001000C2u, 0x00000000u, 0x00100AE6u,
            0x00000000u, 0x00004002u, 0x00000000u, 0x00000000u,
            0x40800000u, 0x40000000u,
            0x0700000Fu, 0x00100042u, 0x00000000u, 0x00100AE6u,
            0x00000000u, 0x00100AE6u, 0x00000000u,
            0x08000000u, 0x00100042u, 0x00000000u, 0x8010002Au,
            0x00000041u, 0x00000000u, 0x00004001u, 0x3F800000u,
            0x07000034u, 0x00100042u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x00004001u, 0x00000000u,
        };

        // Stock terminal bounds combine x/y outside [0,1]. They do not treat
        // the internal x=0.5 stereo boundary as an edge.
        constexpr std::array<std::uint32_t, 41> kStockFinalBounds{
            0x0A00001Du, 0x00100062u, 0x00000000u, 0x00004002u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00100106u, 0x00000006u,
            0x0A00001Du, 0x00100032u, 0x00000001u, 0x00100046u,
            0x00000006u, 0x00004002u, 0x3F800000u, 0x3F800000u,
            0x00000000u, 0x00000000u,
            0x0700003Cu, 0x00100022u, 0x00000000u, 0x0010001Au,
            0x00000000u, 0x0010000Au, 0x00000001u,
            0x0700003Cu, 0x00100022u, 0x00000000u, 0x0010002Au,
            0x00000000u, 0x0010001Au, 0x00000000u,
            0x0700003Cu, 0x00100022u, 0x00000000u, 0x0010001Au,
            0x00000001u, 0x0010001Au, 0x00000000u,
        };

        // Reject a hit whose packed half differs from the originating pixel.
        // The ray marcher may inspect its depth pyramid beyond the seam, but a
        // cross-eye candidate can never be sampled or published.
        constexpr std::array<std::uint32_t, 28> kStereoEyeGuard{
            0x0700001Du, 0x00100042u, 0x00000001u, 0x0010100Au,
            0x00000001u, 0x00004001u, 0x3F000000u,
            0x0700001Du, 0x00100082u, 0x00000001u, 0x0010000Au,
            0x00000006u, 0x00004001u, 0x3F000000u,
            0x07000027u, 0x00100042u, 0x00000001u, 0x0010002Au,
            0x00000001u, 0x0010003Au, 0x00000001u,
            0x0700003Cu, 0x00100022u, 0x00000000u, 0x0010002Au,
            0x00000001u, 0x0010001Au, 0x00000000u,
        };

        struct Chunk
        {
            std::uint32_t offset{};
            std::uint32_t size{};
            std::uint32_t tag{};
        };

        [[nodiscard]] bool readU32(
            std::span<const std::byte> bytes,
            std::size_t offset,
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
            std::size_t offset,
            std::uint32_t value) noexcept
        {
            if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
            return true;
        }

        template <std::size_t Size>
        [[nodiscard]] std::optional<std::size_t> findUniqueSequence(
            std::span<const std::uint32_t> words,
            const std::array<std::uint32_t, Size>& sequence) noexcept
        {
            std::optional<std::size_t> match;
            if (sequence.size() > words.size()) {
                return match;
            }
            for (std::size_t index = 0;
                 index <= words.size() - sequence.size();
                 ++index) {
                if (!std::equal(
                        sequence.begin(),
                        sequence.end(),
                        words.begin() + index)) {
                    continue;
                }
                if (match) {
                    return std::nullopt;
                }
                match = index;
            }
            return match;
        }

        [[nodiscard]] bool validateChunks(
            std::span<const std::byte> bytes,
            std::vector<Chunk>& chunks,
            std::size_t& shaderChunkIndex) noexcept
        {
            std::uint32_t declaredSize{};
            std::uint32_t chunkCount{};
            if (bytes.size() < kChunkOffsetsOffset ||
                !readU32(bytes, kDeclaredSizeOffset, declaredSize) ||
                declaredSize != bytes.size() ||
                !readU32(bytes, kChunkCountOffset, chunkCount) ||
                chunkCount == 0 ||
                chunkCount > (bytes.size() - kChunkOffsetsOffset) /
                    sizeof(std::uint32_t)) {
                return false;
            }

            const auto headerSize = kChunkOffsetsOffset +
                static_cast<std::size_t>(chunkCount) * sizeof(std::uint32_t);
            chunks.clear();
            chunks.reserve(chunkCount);
            std::optional<std::size_t> shaderIndex;
            for (std::uint32_t index = 0; index < chunkCount; ++index) {
                Chunk chunk{};
                if (!readU32(
                        bytes,
                        kChunkOffsetsOffset +
                            static_cast<std::size_t>(index) *
                                sizeof(std::uint32_t),
                        chunk.offset) ||
                    chunk.offset < headerSize ||
                    !readU32(bytes, chunk.offset, chunk.tag) ||
                    !readU32(bytes, chunk.offset + 4, chunk.size) ||
                    chunk.offset > bytes.size() ||
                    8 > bytes.size() - chunk.offset ||
                    chunk.size > bytes.size() - chunk.offset - 8) {
                    return false;
                }
                if (chunk.tag == kShexTag || chunk.tag == kShdrTag) {
                    if (shaderIndex) {
                        return false;
                    }
                    shaderIndex = chunks.size();
                }
                chunks.push_back(chunk);
            }
            if (!shaderIndex) {
                return false;
            }

            auto sortedChunks = chunks;
            std::ranges::sort(
                sortedChunks,
                {},
                &Chunk::offset);
            for (std::size_t index = 1; index < sortedChunks.size(); ++index) {
                const auto previousEnd =
                    static_cast<std::size_t>(sortedChunks[index - 1].offset) +
                    8 + sortedChunks[index - 1].size;
                if (sortedChunks[index].offset < previousEnd) {
                    return false;
                }
            }
            shaderChunkIndex = *shaderIndex;
            return true;
        }
    }

    bool patchStockSslrRaytracePixel(
        std::span<const std::byte> stockBytecode,
        std::vector<std::byte>& patchedBytecode) noexcept
    {
        patchedBytecode.clear();
        try {
            std::vector<Chunk> chunks;
            std::size_t shaderChunkIndex{};
            if (!validateChunks(
                    stockBytecode,
                    chunks,
                    shaderChunkIndex)) {
                return false;
            }
            const auto& shaderChunk = chunks[shaderChunkIndex];
            if (shaderChunk.size < 2 * sizeof(std::uint32_t) ||
                shaderChunk.size % sizeof(std::uint32_t) != 0) {
                return false;
            }

            const auto shaderPayloadOffset =
                static_cast<std::size_t>(shaderChunk.offset) + 8;
            std::vector<std::uint32_t> words(
                shaderChunk.size / sizeof(std::uint32_t));
            std::memcpy(
                words.data(),
                stockBytecode.data() + shaderPayloadOffset,
                shaderChunk.size);
            if (words[1] != words.size()) {
                return false;
            }

            const auto fadePosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockFade);
            const auto boundsPosition = findUniqueSequence(
                std::span<const std::uint32_t>{ words },
                kStockFinalBounds);
            if (!fadePosition || !boundsPosition ||
                *fadePosition + kStockFade.size() > *boundsPosition) {
                return false;
            }

            words.insert(
                words.begin() + *boundsPosition + kStockFinalBounds.size(),
                kStereoEyeGuard.begin(),
                kStereoEyeGuard.end());
            words.erase(
                words.begin() + *fadePosition,
                words.begin() + *fadePosition + kStockFade.size());
            words.insert(
                words.begin() + *fadePosition,
                kStereoFade.begin(),
                kStereoFade.end());
            if (words.size() >
                (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            words[1] = static_cast<std::uint32_t>(words.size());

            const auto newShaderSize = words.size() * sizeof(std::uint32_t);
            const auto growth = newShaderSize - shaderChunk.size;
            if (growth >
                    (std::numeric_limits<std::uint32_t>::max)() -
                        stockBytecode.size() ||
                stockBytecode.size() + growth >
                    (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            const auto oldShaderEnd = shaderPayloadOffset + shaderChunk.size;
            std::vector<std::byte> candidate;
            candidate.reserve(stockBytecode.size() + growth);
            candidate.insert(
                candidate.end(),
                stockBytecode.begin(),
                stockBytecode.begin() + shaderPayloadOffset);
            const auto* newShaderBytes = reinterpret_cast<const std::byte*>(
                words.data());
            candidate.insert(
                candidate.end(),
                newShaderBytes,
                newShaderBytes + newShaderSize);
            candidate.insert(
                candidate.end(),
                stockBytecode.begin() + oldShaderEnd,
                stockBytecode.end());

            if (!writeU32(
                    candidate,
                    kDeclaredSizeOffset,
                    static_cast<std::uint32_t>(candidate.size())) ||
                !writeU32(
                    candidate,
                    static_cast<std::size_t>(shaderChunk.offset) + 4,
                    static_cast<std::uint32_t>(newShaderSize))) {
                return false;
            }
            for (std::size_t index = 0; index < chunks.size(); ++index) {
                if (chunks[index].offset <= shaderChunk.offset) {
                    continue;
                }
                if (growth >
                    (std::numeric_limits<std::uint32_t>::max)() -
                        chunks[index].offset) {
                    return false;
                }
                if (!writeU32(
                        candidate,
                        kChunkOffsetsOffset +
                            index * sizeof(std::uint32_t),
                        chunks[index].offset +
                            static_cast<std::uint32_t>(growth))) {
                    return false;
                }
            }
            if (!linear_lighting::recomputeDxbcChecksum(candidate)) {
                return false;
            }
            patchedBytecode.swap(candidate);
            return true;
        } catch (...) {
            patchedBytecode.clear();
            return false;
        }
    }
}
