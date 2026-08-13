#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace community_shaders::complex_materials
{
    enum class ComplexMaterialProducerFamily : std::uint8_t
    {
        kEnvironmentMap = 0,
        kParallax,
        kParallaxOcclusion,
        kMultiLayerParallax,
        kEye,
        kLandscapeComplexParallax,
    };

    using ComplexMaterialProducerFamilyMask = std::uint8_t;

    [[nodiscard]] constexpr ComplexMaterialProducerFamilyMask
        producerFamilyMask(ComplexMaterialProducerFamily family) noexcept
    {
        return static_cast<ComplexMaterialProducerFamilyMask>(
            1u << static_cast<std::uint8_t>(family));
    }

    struct ComplexMaterialProducerContract
    {
        std::uint32_t bytecodeSize{};
        std::array<std::uint8_t, 16> checksum{};
        ComplexMaterialProducerFamilyMask familyMask{};
    };

    struct ComplexMaterialProducerAlias
    {
        std::uint32_t descriptor{};
        std::uint16_t contractPlusOne{};
        ComplexMaterialProducerFamily family{};
    };

    struct ComplexMaterialProducerBinding
    {
        std::uint16_t contractPlusOne{};
        ComplexMaterialProducerFamilyMask familyMask{};

        [[nodiscard]] constexpr bool hasFamily(
            ComplexMaterialProducerFamily family) const noexcept
        {
            return (familyMask & producerFamilyMask(family)) != 0;
        }
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

    // Fallout4VR.exe 1.2.72 at 0x1428B5C10 extracts the lighting material
    // type with SHR 8 followed by AND 0x3F. The six-bit mask is essential:
    // a five-bit mask aliases material type 33 into the ENVMAP family.
    [[nodiscard]] constexpr std::uint8_t materialTypeFromDescriptor(
        std::uint32_t descriptor) noexcept
    {
        return static_cast<std::uint8_t>((descriptor >> 8) & 0x3Fu);
    }

    #include "Features/complex_materials/GeneratedComplexMaterialProducerContracts.inl"

    static_assert(kComplexMaterialProducerContracts.size() == 113);
    static_assert(kComplexMaterialProducerAliases.size() == 151);

    [[nodiscard]] inline ComplexMaterialProducerBinding
        matchComplexMaterialProducer(
            const void* bytecode,
            std::size_t bytecodeSize) noexcept
    {
        if (!bytecode || bytecodeSize < 20) {
            return {};
        }
        const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
        if (bytes[0] != 'D' || bytes[1] != 'X' || bytes[2] != 'B' ||
            bytes[3] != 'C') {
            return {};
        }
        for (std::size_t index = 0;
             index < kComplexMaterialProducerContracts.size();
             ++index) {
            const auto& contract = kComplexMaterialProducerContracts[index];
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
                return {
                    static_cast<std::uint16_t>(index + 1),
                    contract.familyMask,
                };
            }
        }
        return {};
    }

    [[nodiscard]] constexpr const ComplexMaterialProducerAlias*
        findComplexMaterialProducerAlias(std::uint32_t descriptor) noexcept
    {
        std::size_t first{};
        std::size_t last = kComplexMaterialProducerAliases.size();
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            const auto& alias = kComplexMaterialProducerAliases[middle];
            if (alias.descriptor < descriptor) {
                first = middle + 1;
            } else {
                last = middle;
            }
        }
        if (first == kComplexMaterialProducerAliases.size() ||
            kComplexMaterialProducerAliases[first].descriptor != descriptor) {
            return nullptr;
        }
        return &kComplexMaterialProducerAliases[first];
    }
}
