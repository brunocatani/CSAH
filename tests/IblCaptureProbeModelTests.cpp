#include "Features/ibl/IblCaptureProbeModel.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using csah::ibl::advanceCaptureProbePass;
    using csah::ibl::CaptureProbePassState;
    using csah::ibl::CaptureProbeShaderBinding;
    using csah::ibl::classifyCaptureProbeShader;
    using csah::ibl::kCaptureProbeContracts;
    using csah::ibl::kDFCompositeBoundaryContracts;
    using csah::ibl::matchCaptureProbeContract;
    using csah::ibl::shouldCaptureCompletedProbePass;

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            fail(message);
        }
    }
}

int main()
{
    try {
        constexpr CaptureProbeShaderBinding environment{ true, 7 };
        constexpr CaptureProbeShaderBinding nonEnvironmentComposite{ true, 0 };
        constexpr CaptureProbeShaderBinding outsideComposite{};
        constexpr auto environmentState = advanceCaptureProbePass(
            {},
            environment);
        static_assert(environmentState.inDFComposite);
        static_assert(environmentState.lastEnvironmentContractPlusOne == 7);
        constexpr auto preservedState = advanceCaptureProbePass(
            environmentState,
            nonEnvironmentComposite);
        static_assert(preservedState.inDFComposite);
        static_assert(preservedState.lastEnvironmentContractPlusOne == 7);
        static_assert(!shouldCaptureCompletedProbePass(
            environmentState,
            nonEnvironmentComposite));
        static_assert(shouldCaptureCompletedProbePass(
            preservedState,
            outsideComposite));
        static_assert(!advanceCaptureProbePass(
            preservedState,
            outsideComposite).inDFComposite);
        constexpr auto noEnvironmentState = advanceCaptureProbePass(
            {},
            nonEnvironmentComposite);
        static_assert(!shouldCaptureCompletedProbePass(
            noEnvironmentState,
            outsideComposite));

        require(
            matchCaptureProbeContract(nullptr, 0) == 0,
            "null bytecode must not match");
        for (std::size_t contractIndex = 0;
             contractIndex < kCaptureProbeContracts.size();
             ++contractIndex) {
            const auto& contract = kCaptureProbeContracts[contractIndex];
            std::vector<std::uint8_t> bytecode(contract.bytecodeSize);
            bytecode[0] = 'D';
            bytecode[1] = 'X';
            bytecode[2] = 'B';
            bytecode[3] = 'C';
            for (std::size_t checksumIndex = 0;
                 checksumIndex < contract.checksum.size();
                 ++checksumIndex) {
                bytecode[4 + checksumIndex] =
                    contract.checksum[checksumIndex];
            }
            require(
                matchCaptureProbeContract(
                    bytecode.data(),
                    bytecode.size()) == contractIndex + 1,
                "exact contract identity did not match");
            const auto binding = classifyCaptureProbeShader(
                bytecode.data(),
                bytecode.size());
            require(
                binding.isDFComposite &&
                    binding.environmentContractPlusOne == contractIndex + 1,
                "environment identity did not classify as DFComposite");

            bytecode[4] ^= 0x01;
            require(
                matchCaptureProbeContract(
                    bytecode.data(),
                    bytecode.size()) == 0,
                "checksum mutation must fail closed");
            bytecode[4] ^= 0x01;
            bytecode[0] = 'R';
            require(
                matchCaptureProbeContract(
                    bytecode.data(),
                    bytecode.size()) == 0,
                "non-DXBC input must fail closed");
        }

        for (const auto& contract : kDFCompositeBoundaryContracts) {
            std::vector<std::uint8_t> bytecode(contract.bytecodeSize);
            bytecode[0] = 'D';
            bytecode[1] = 'X';
            bytecode[2] = 'B';
            bytecode[3] = 'C';
            for (std::size_t checksumIndex = 0;
                 checksumIndex < contract.checksum.size();
                 ++checksumIndex) {
                bytecode[4 + checksumIndex] =
                    contract.checksum[checksumIndex];
            }
            const auto binding = classifyCaptureProbeShader(
                bytecode.data(),
                bytecode.size());
            require(
                binding.isDFComposite &&
                    binding.environmentContractPlusOne == 0,
                "non-environment identity did not classify as DFComposite");

            bytecode[4] ^= 0x01;
            require(
                !classifyCaptureProbeShader(
                    bytecode.data(),
                    bytecode.size()).isDFComposite,
                "boundary checksum mutation must fail closed");
        }
    } catch (const std::exception& error) {
        std::cerr << "IblCaptureProbeModelTests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "IBL capture-probe model tests passed.\n";
    return 0;
}
