#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace csah::linear_lighting
{
    namespace dxbc_checksum_detail
    {
        constexpr std::array<std::uint32_t, 64> kRotationCounts{
            7, 12, 17, 22, 7, 12, 17, 22,
            7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20,
            5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23,
            4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21,
            6, 10, 15, 21, 6, 10, 15, 21,
        };

        constexpr std::array<std::uint32_t, 64> kRoundConstants{
            0xD76AA478u, 0xE8C7B756u, 0x242070DBu, 0xC1BDCEEEu,
            0xF57C0FAFu, 0x4787C62Au, 0xA8304613u, 0xFD469501u,
            0x698098D8u, 0x8B44F7AFu, 0xFFFF5BB1u, 0x895CD7BEu,
            0x6B901122u, 0xFD987193u, 0xA679438Eu, 0x49B40821u,
            0xF61E2562u, 0xC040B340u, 0x265E5A51u, 0xE9B6C7AAu,
            0xD62F105Du, 0x02441453u, 0xD8A1E681u, 0xE7D3FBC8u,
            0x21E1CDE6u, 0xC33707D6u, 0xF4D50D87u, 0x455A14EDu,
            0xA9E3E905u, 0xFCEFA3F8u, 0x676F02D9u, 0x8D2A4C8Au,
            0xFFFA3942u, 0x8771F681u, 0x6D9D6122u, 0xFDE5380Cu,
            0xA4BEEA44u, 0x4BDECFA9u, 0xF6BB4B60u, 0xBEBFBC70u,
            0x289B7EC6u, 0xEAA127FAu, 0xD4EF3085u, 0x04881D05u,
            0xD9D4D039u, 0xE6DB99E5u, 0x1FA27CF8u, 0xC4AC5665u,
            0xF4292244u, 0x432AFF97u, 0xAB9423A7u, 0xFC93A039u,
            0x655B59C3u, 0x8F0CCC92u, 0xFFEFF47Du, 0x85845DD1u,
            0x6FA87E4Fu, 0xFE2CE6E0u, 0xA3014314u, 0x4E0811A1u,
            0xF7537E82u, 0xBD3AF235u, 0x2AD7D2BBu, 0xEB86D391u,
        };

        inline void transform(
            std::array<std::uint32_t, 4>& state,
            const std::byte* block) noexcept
        {
            std::array<std::uint32_t, 16> words{};
            std::memcpy(words.data(), block, sizeof(words));

            auto a = state[0];
            auto b = state[1];
            auto c = state[2];
            auto d = state[3];
            for (std::size_t index = 0; index < 64; ++index) {
                std::uint32_t function{};
                std::size_t wordIndex{};
                if (index < 16) {
                    function = (b & c) | (~b & d);
                    wordIndex = index;
                } else if (index < 32) {
                    function = (d & b) | (~d & c);
                    wordIndex = (5 * index + 1) % 16;
                } else if (index < 48) {
                    function = b ^ c ^ d;
                    wordIndex = (3 * index + 5) % 16;
                } else {
                    function = c ^ (b | ~d);
                    wordIndex = (7 * index) % 16;
                }

                const auto previousD = d;
                d = c;
                c = b;
                b += std::rotl(
                    a + function + kRoundConstants[index] +
                        words[wordIndex],
                    static_cast<int>(kRotationCounts[index]));
                a = previousD;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
        }

        [[nodiscard]] inline std::array<std::byte, 16> compute(
            std::span<const std::byte> payload) noexcept
        {
            std::array<std::uint32_t, 4> state{
                0x67452301u,
                0xEFCDAB89u,
                0x98BADCFEu,
                0x10325476u,
            };
            const auto completeBlocks = payload.size() / 64;
            for (std::size_t index = 0; index < completeBlocks; ++index) {
                transform(state, payload.data() + index * 64);
            }

            const auto remainder = payload.size() % 64;
            const auto payloadLength = static_cast<std::uint32_t>(
                payload.size());
            std::array<std::byte, 64> finalBlock{};
            if (remainder < 56) {
                const auto bitLength = payloadLength * 8u;
                std::memcpy(
                    finalBlock.data(), &bitLength, sizeof(bitLength));
                std::memcpy(
                    finalBlock.data() + sizeof(bitLength),
                    payload.data() + completeBlocks * 64,
                    remainder);
                finalBlock[sizeof(bitLength) + remainder] =
                    std::byte{ 0x80 };
            } else {
                std::memcpy(
                    finalBlock.data(),
                    payload.data() + completeBlocks * 64,
                    remainder);
                finalBlock[remainder] = std::byte{ 0x80 };
                transform(state, finalBlock.data());
                finalBlock.fill(std::byte{});
                const auto bitLength = payloadLength * 8u;
                std::memcpy(
                    finalBlock.data(), &bitLength, sizeof(bitLength));
            }

            const auto encodedLength = payloadLength * 2u | 1u;
            std::memcpy(
                finalBlock.data() + 60,
                &encodedLength,
                sizeof(encodedLength));
            transform(state, finalBlock.data());

            std::array<std::byte, 16> checksum{};
            std::memcpy(checksum.data(), state.data(), checksum.size());
            return checksum;
        }
    }

    // DXBC uses Microsoft's container checksum variant, not ordinary MD5.
    // The checksum covers bytes after the 20-byte magic/checksum prefix and
    // uses the payload bit length at final-block +0x00 plus
    // (payloadBytes * 2) | 1 at +0x3C.
    [[nodiscard]] inline bool recomputeDxbcChecksum(
        std::span<std::byte> bytecode) noexcept
    {
        constexpr std::array<std::byte, 4> magic{
            std::byte{ 0x44 },
            std::byte{ 0x58 },
            std::byte{ 0x42 },
            std::byte{ 0x43 },
        };
        constexpr std::size_t checksumOffset = 4;
        constexpr std::size_t payloadOffset = 20;
        constexpr std::size_t declaredSizeOffset = 24;
        if (bytecode.size() < 32 ||
            !std::equal(magic.begin(), magic.end(), bytecode.begin()) ||
            bytecode.size() - payloadOffset >
                (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }

        std::uint32_t declaredSize{};
        std::memcpy(
            &declaredSize,
            bytecode.data() + declaredSizeOffset,
            sizeof(declaredSize));
        if (declaredSize != bytecode.size()) {
            return false;
        }

        const auto checksum = dxbc_checksum_detail::compute(
            std::span<const std::byte>{
                bytecode.data(), bytecode.size() }
                .subspan(payloadOffset));
        std::memcpy(
            bytecode.data() + checksumOffset,
            checksum.data(),
            checksum.size());
        return true;
    }
}
