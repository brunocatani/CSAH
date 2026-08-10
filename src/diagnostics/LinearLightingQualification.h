#pragma once

#include <cstdint>

namespace community_shaders::diagnostics
{
    enum class LinearLightingQualificationState : std::uint8_t
    {
        waitingForWorld,
        running,
        passed,
        failed,
    };

    struct LinearLightingQualificationSnapshot
    {
        LinearLightingQualificationState state{
            LinearLightingQualificationState::waitingForWorld };
        std::uint64_t generation{};
        std::uint64_t elapsedMilliseconds{};
        std::uint64_t reasonMask{};
        std::uint32_t fullyVerifiedContracts{};
        std::uint32_t expectedContracts{};
        bool worldLifecycleReached{};
        bool renderThreadActivated{};
    };

    void startLinearLightingQualificationReporter() noexcept;
    void beginLinearLightingQualificationSession(const char* trigger) noexcept;
    [[nodiscard]] LinearLightingQualificationSnapshot
    linearLightingQualificationSnapshot() noexcept;
    [[nodiscard]] const char* linearLightingQualificationStateName(
        LinearLightingQualificationState state) noexcept;
}
