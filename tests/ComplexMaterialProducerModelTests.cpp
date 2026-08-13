#include "Features/complex_materials/ComplexMaterialProducerModel.h"
#include "Features/complex_materials/ComplexEnvironmentMaterialModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace community_shaders::complex_materials;

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
}

int main()
{
    try {
        static_assert(materialTypeFromDescriptor(0x00000100u) == 1);
        static_assert(materialTypeFromDescriptor(0x00000300u) == 3);
        static_assert(materialTypeFromDescriptor(0x00000700u) == 7);
        static_assert(materialTypeFromDescriptor(0x00000B00u) == 11);
        static_assert(materialTypeFromDescriptor(0x00001000u) == 16);
        static_assert(materialTypeFromDescriptor(0x00002102u) == 33);
        static_assert(((0x00002102u >> 8) & 0x1Fu) == 1);

        std::array<std::size_t, 6> aliasCounts{};
        std::array<std::size_t, 6> identityCounts{};
        std::uint32_t previousDescriptor{};
        bool firstAlias = true;
        for (const auto& alias : kComplexMaterialProducerAliases) {
            require(
                firstAlias || alias.descriptor > previousDescriptor,
                "producer aliases are not strictly descriptor-sorted");
            firstAlias = false;
            previousDescriptor = alias.descriptor;
            require(
                alias.contractPlusOne > 0 &&
                    alias.contractPlusOne <=
                        kComplexMaterialProducerContracts.size(),
                "producer alias references an invalid identity");
            const auto familyIndex = static_cast<std::size_t>(alias.family);
            ++aliasCounts[familyIndex];
            const auto& contract = kComplexMaterialProducerContracts[
                alias.contractPlusOne - 1];
            require(
                (contract.familyMask & producerFamilyMask(alias.family)) != 0,
                "producer alias family is absent from its identity mask");
            if (alias.family !=
                ComplexMaterialProducerFamily::kLandscapeComplexParallax) {
                constexpr std::array<std::uint8_t, 5> expectedTypes{
                    1, 3, 7, 11, 16,
                };
                require(
                    materialTypeFromDescriptor(alias.descriptor) ==
                        expectedTypes[familyIndex],
                    "producer family disagrees with the six-bit material type");
            } else {
                require(
                    materialTypeFromDescriptor(alias.descriptor) == 0,
                    "tested landscape producer escaped material type zero");
            }
        }

        require(
            aliasCounts == std::array<std::size_t, 6>{ 122, 2, 0, 1, 22, 4 },
            "producer alias family census changed");
        require(
            findComplexMaterialProducerAlias(0x00002102u) == nullptr,
            "type-33 descriptor leaked into ENVMAP contracts");
        require(
            findComplexMaterialProducerAlias(0x00000023u) != nullptr &&
                findComplexMaterialProducerAlias(0x0200003Bu) != nullptr &&
                findComplexMaterialProducerAlias(0x0A000023u) != nullptr,
            "tested landscape producer descriptors are missing");

        for (std::size_t contractIndex = 0;
             contractIndex < kComplexMaterialProducerContracts.size();
             ++contractIndex) {
            const auto& contract =
                kComplexMaterialProducerContracts[contractIndex];
            for (std::size_t familyIndex = 0;
                 familyIndex < identityCounts.size();
                 ++familyIndex) {
                if ((contract.familyMask &
                        producerFamilyMask(static_cast<
                            ComplexMaterialProducerFamily>(familyIndex))) != 0) {
                    ++identityCounts[familyIndex];
                }
            }

            std::vector<std::uint8_t> bytecode(contract.bytecodeSize);
            bytecode[0] = 'D';
            bytecode[1] = 'X';
            bytecode[2] = 'B';
            bytecode[3] = 'C';
            for (std::size_t checksumIndex = 0;
                 checksumIndex < contract.checksum.size();
                 ++checksumIndex) {
                bytecode[4 + checksumIndex] = contract.checksum[checksumIndex];
            }
            const auto binding = matchComplexMaterialProducer(
                bytecode.data(), bytecode.size());
            require(
                binding.contractPlusOne == contractIndex + 1 &&
                    binding.familyMask == contract.familyMask,
                "exact producer identity did not round-trip");
            bytecode[4] ^= 0x01;
            require(
                matchComplexMaterialProducer(
                    bytecode.data(), bytecode.size()).contractPlusOne == 0,
                "mutated producer checksum did not fail closed");
        }

        require(
            identityCounts ==
                std::array<std::size_t, 6>{ 86, 2, 0, 1, 22, 3 },
            "producer identity family census changed");
        require(
            matchComplexMaterialProducer(nullptr, 0).contractPlusOne == 0,
            "null producer bytecode did not fail closed");

        std::size_t complexEnvironmentContracts{};
        for (const auto contractPlusOne :
             kComplexEnvironmentProducerByLinearContract) {
            if (contractPlusOne == 0) {
                continue;
            }
            ++complexEnvironmentContracts;
            require(
                contractPlusOne <= kComplexMaterialProducerContracts.size(),
                "complex environment map references an invalid producer");
            require(
                kComplexMaterialProducerContracts[contractPlusOne - 1].
                    familyMask &
                    producerFamilyMask(
                        ComplexMaterialProducerFamily::kEnvironmentMap),
                "complex environment map references a non-ENVMAP producer");
        }
        require(
            complexEnvironmentContracts ==
                kComplexEnvironmentShaderContractCount,
            "complex environment shader census changed");
        require(
            !isComplexEnvironmentMask({ 0.5F, 0.5F, 0.5F, 0.5F }, true),
            "grayscale legacy mask was treated as complex");
        require(
            isComplexEnvironmentMask({ 0.0F, 0.0F, 0.0F, 0.5F }, true),
            "solid-black complex height marker was rejected");
        require(
            isComplexEnvironmentMask({ 0.2F, 0.6F, 0.8F, 0.5F }, true),
            "coloured complex marker was rejected");
        require(
            !isComplexEnvironmentMask({ 0.2F, 0.6F, 0.8F, 1.0F }, true),
            "unmarked environment mask was treated as complex");
        require(
            decodeMetalnessTag(encodeMetalnessTag(0.0F)) == 0.0F &&
                decodeMetalnessTag(encodeMetalnessTag(1.0F)) == 1.0F,
            "metalness tag endpoints did not round-trip");
        require(
            retainedDiffuseScale(1.0F) == kMinimumRetainedDiffuse &&
                retainedDiffuseScale(0.0F) == 1.0F,
            "diffuse energy retention endpoints changed");
        std::array<std::uint8_t, 20> nonDxbc{};
        require(
            matchComplexMaterialProducer(
                nonDxbc.data(), nonDxbc.size()).contractPlusOne == 0,
            "non-DXBC producer bytecode did not fail closed");
    } catch (const std::exception& error) {
        std::cerr << "ComplexMaterialProducerModelTests failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "Complex-material producer model tests passed.\n";
    return 0;
}
