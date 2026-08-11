#include "Features/ibl/IblCaptureProbeModel.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using community_shaders::ibl::kCaptureProbeContracts;
    using community_shaders::ibl::matchCaptureProbeContract;

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
    } catch (const std::exception& error) {
        std::cerr << "IblCaptureProbeModelTests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "IBL capture-probe model tests passed.\n";
    return 0;
}
