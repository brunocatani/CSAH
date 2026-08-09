#pragma once

#include <cstdint>

namespace community_shaders::render::d3d11_hook_repair
{
    enum class Decision : std::uint8_t
    {
        noAction,
        observeDisplacement,
        preserveReachableChain,
        repairDisplacement
    };

    struct State
    {
        std::uintptr_t displacedTarget{};
        std::uint64_t bindCallsAtObservation{};
        bool downstreamChainProven{};
    };

    [[nodiscard]] inline Decision advance(
        State& state,
        bool hookInstalled,
        bool cellOwned,
        std::uintptr_t currentTarget,
        std::uint64_t bindCalls) noexcept
    {
        if (!hookInstalled || cellOwned || currentTarget == 0) {
            state = {};
            return Decision::noAction;
        }

        if (state.displacedTarget != currentTarget) {
            state = {
                .displacedTarget = currentTarget,
                .bindCallsAtObservation = bindCalls,
            };
            return Decision::observeDisplacement;
        }

        if (bindCalls != state.bindCallsAtObservation) {
            state.bindCallsAtObservation = bindCalls;
            state.downstreamChainProven = true;
            return Decision::preserveReachableChain;
        }
        if (state.downstreamChainProven) {
            return Decision::preserveReachableChain;
        }
        return Decision::repairDisplacement;
    }
}
