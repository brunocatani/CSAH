#include "Features/linear_lighting/DFLightAmbientShaderPatch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>

namespace
{
    bool expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

    bool near(float left, float right, float epsilon = 2.0e-6f)
    {
        return std::abs(left - right) <= epsilon *
            (std::max)({ 1.0f, std::abs(left), std::abs(right) });
    }
}

int main()
{
    using namespace csah::linear_lighting;

    bool passed = true;
    std::array<std::byte, 128> bytecode{};
    bytecode[0] = std::byte{ 0x44 };
    bytecode[1] = std::byte{ 0x58 };
    bytecode[2] = std::byte{ 0x42 };
    bytecode[3] = std::byte{ 0x43 };
    const auto bytecodeSize = static_cast<std::uint32_t>(bytecode.size());
    std::memcpy(bytecode.data() + 24, &bytecodeSize, sizeof(bytecodeSize));
    const DFLightAmbientGammaOffsets offsets{ 32, 40, 48, 64, 72, 80 };
    const auto vanillaBits = std::bit_cast<std::uint32_t>(
        kVanillaDFLightAmbientShaderGamma);
    for (const auto offset : offsets) {
        std::memcpy(bytecode.data() + offset, &vanillaBits, sizeof(vanillaBits));
    }
    passed &= expect(
        patchDFLightAmbientGamma(bytecode, offsets, 1.8f),
        "verified DFLight ambient gamma patch was rejected");
    const auto expectedBits = std::bit_cast<std::uint32_t>(1.8f);
    for (const auto offset : offsets) {
        std::uint32_t observed{};
        std::memcpy(&observed, bytecode.data() + offset, sizeof(observed));
        passed &= expect(
            observed == expectedBits,
            "DFLight ambient gamma float was not replaced");
    }
    passed &= expect(
        !patchDFLightAmbientGamma(bytecode, offsets, 1.7f),
        "already-patched bytecode bypassed the vanilla identity gate");

    DirectionalAmbientTransform transform{
        1.0f, 2.0f, 3.0f, 101.0f,
        4.0f, 5.0f, 6.0f, 102.0f,
        7.0f, 8.0f, 9.0f, 103.0f,
        10.0f, 11.0f, 12.0f, 2.0f,
    };
    const auto original = transform;
    constexpr float gamma = 1.8f;
    constexpr float multiplier = 0.75f;
    const auto inputScale = directionalAmbientInputScale(multiplier, gamma);
    scaleDirectionalAmbientTransform(transform, inputScale);
    for (const auto index : { 3u, 7u, 11u, 15u }) {
        passed &= expect(
            transform[index] == original[index],
            "ambient transform padding/scale was modified");
    }

    constexpr std::array<float, 4> direction{ 0.2f, 0.3f, 0.4f, 1.0f };
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto source = original[channel] * direction[0] +
            original[4 + channel] * direction[1] +
            original[8 + channel] * direction[2] +
            original[12 + channel] * direction[3];
        const auto scaled = transform[channel] * direction[0] +
            transform[4 + channel] * direction[1] +
            transform[8 + channel] * direction[2] +
            transform[12 + channel] * direction[3];
        passed &= expect(
            near(
                std::pow(scaled, gamma),
                std::pow(source, gamma) * multiplier),
            "pre-pow ambient transform scale lost post-pow multiplier parity");
    }

    passed &= expect(
        directionalAmbientInputScale(-1.0f, gamma) == 1.0f,
        "invalid ambient multiplier did not fail closed");
    return passed ? 0 : 1;
}
