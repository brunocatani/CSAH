#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

#include <cstdint>
#include <numbers>

namespace community_shaders::linear_lighting
{
    constexpr float kVanillaDFLightGamma = 2.2f;
    constexpr std::uint32_t kDFLightDirectionalDescriptorMask = 0x00000003u;
    constexpr std::uint32_t kDFLightAmbientDescriptorMask = 0x00020000u;
    constexpr std::uint32_t kDFLightCharacterDescriptorMask = 0x04000000u;

    enum class DFLightProducerKind : std::uint8_t
    {
        other,
        directional,
        ambient,
    };

    struct DFLightProducerState
    {
        bool enabled{};
        float directionalGamma{ kVanillaDFLightGamma };
        float directionalMultiplier{ 1.0f };
        float ambientGamma{ kVanillaDFLightGamma };
        float ambientMultiplier{ 1.0f };
    };

    [[nodiscard]] inline DFLightProducerKind classifyDFLightProducer(
        std::uint32_t descriptor) noexcept
    {
        if ((descriptor & kDFLightCharacterDescriptorMask) != 0u) {
            return DFLightProducerKind::other;
        }
        if ((descriptor & kDFLightAmbientDescriptorMask) != 0u) {
            return DFLightProducerKind::ambient;
        }
        if ((descriptor & kDFLightDirectionalDescriptorMask) != 0u) {
            return DFLightProducerKind::directional;
        }
        return DFLightProducerKind::other;
    }

    [[nodiscard]] inline DFLightProducerState makeDFLightProducerState(
        const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        if (!safe.enabled) {
            return {};
        }
        return {
            .enabled = true,
            .directionalGamma = safe.lightGamma,
            .directionalMultiplier =
                std::numbers::pi_v<float> * safe.directionalLightMultiplier,
            .ambientGamma = safe.ambientGamma,
            .ambientMultiplier = safe.ambientMultiplier,
        };
    }
}
