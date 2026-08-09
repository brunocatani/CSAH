#pragma once

#include <cstdint>

namespace community_shaders::diagnostics::qualification_model
{
    enum class Status : std::uint8_t
    {
        waiting,
        passed,
        failed,
    };

    enum FailureReason : std::uint64_t
    {
        Failure_None = 0,
        Failure_SessionNotActivated = 1ull << 0,
        Failure_FeatureDisabled = 1ull << 1,
        Failure_GpuResourcesUnavailable = 1ull << 2,
        Failure_GeometryProviderUnavailable = 1ull << 3,
        Failure_ShaderDetoursUnowned = 1ull << 4,
        Failure_DrawDetoursUnowned = 1ull << 5,
        Failure_GeometryHookUnowned = 1ull << 6,
        Failure_ShaderContractsIncomplete = 1ull << 7,
        Failure_OriginalContractsIncomplete = 1ull << 8,
        Failure_NoGeometryCall = 1ull << 9,
        Failure_GeometrySourceRejected = 1ull << 10,
        Failure_NoGeometryUpdate = 1ull << 11,
        Failure_GeometryUpdateRejected = 1ull << 12,
        Failure_NoReplacementBind = 1ull << 13,
        Failure_BindingStateMismatch = 1ull << 14,
        Failure_NoVerifiedBinding = 1ull << 15,
        Failure_NoReplacementDraw = 1ull << 16,
        Failure_DrawStateMismatch = 1ull << 17,
        Failure_NoVerifiedDraw = 1ull << 18,
        Failure_NoCommonVerifiedContract = 1ull << 19,
    };

    struct Sample
    {
        bool sessionActivated{};
        bool enabled{};
        bool gpuResourcesReady{};
        bool geometryProviderReady{};
        bool shaderDetoursOwned{};
        bool drawDetoursOwned{};
        bool geometryHookOwned{};
        std::uint32_t expectedShaderContracts{};
        std::uint32_t verifiedShaderContracts{};
        std::uint64_t matchingShaderContractMask{};
        std::uint64_t geometryCalls{};
        std::uint64_t geometryAccepted{};
        std::uint64_t geometrySourceRejected{};
        std::uint32_t deepestGeometrySourceStage{};
        std::uint64_t geometryUpdates{};
        std::uint64_t geometryUpdateRejects{};
        std::uint64_t replacementShaderBinds{};
        std::uint64_t replacementDrawCalls{};
        std::uint64_t bindingStateFailures{};
        std::uint64_t drawStateFailures{};
        std::uint64_t replacementContractMask{};
        std::uint64_t bindingVerifiedContractMask{};
        std::uint64_t drawVerifiedContractMask{};
    };

    struct Evaluation
    {
        Status status{ Status::waiting };
        std::uint64_t reasonMask{};
        std::uint64_t fullyVerifiedContractMask{};
    };

    [[nodiscard]] inline std::uint64_t expectedContractMask(
        std::uint32_t count) noexcept
    {
        return count == 0 ? 0 :
            count >= 64 ? UINT64_MAX :
                          (1ull << count) - 1ull;
    }

    [[nodiscard]] inline Evaluation evaluate(
        const Sample& sample,
        bool timedOut) noexcept
    {
        Evaluation result{};
        const auto expectedMask = expectedContractMask(
            sample.expectedShaderContracts);
        result.fullyVerifiedContractMask = sample.replacementContractMask &
            sample.bindingVerifiedContractMask &
            sample.drawVerifiedContractMask;

        if (!sample.sessionActivated) {
            result.reasonMask |= Failure_SessionNotActivated;
        }
        if (!sample.enabled) {
            result.reasonMask |= Failure_FeatureDisabled;
        }
        if (!sample.gpuResourcesReady) {
            result.reasonMask |= Failure_GpuResourcesUnavailable;
        }
        if (!sample.geometryProviderReady) {
            result.reasonMask |= Failure_GeometryProviderUnavailable;
        }
        if (!sample.shaderDetoursOwned) {
            result.reasonMask |= Failure_ShaderDetoursUnowned;
        }
        if (!sample.drawDetoursOwned) {
            result.reasonMask |= Failure_DrawDetoursUnowned;
        }
        if (!sample.geometryHookOwned) {
            result.reasonMask |= Failure_GeometryHookUnowned;
        }
        if (sample.verifiedShaderContracts != sample.expectedShaderContracts) {
            result.reasonMask |= Failure_ShaderContractsIncomplete;
        }
        if (expectedMask == 0 ||
            (sample.matchingShaderContractMask & expectedMask) != expectedMask) {
            result.reasonMask |= Failure_OriginalContractsIncomplete;
        }
        if (sample.geometryCalls == 0) {
            result.reasonMask |= Failure_NoGeometryCall;
        }
        if (sample.geometrySourceRejected > 0 ||
            (sample.geometryCalls > 0 &&
                sample.deepestGeometrySourceStage < 2)) {
            result.reasonMask |= Failure_GeometrySourceRejected;
        }
        if (sample.geometryUpdates == 0 || sample.geometryAccepted == 0) {
            result.reasonMask |= Failure_NoGeometryUpdate;
        }
        if (sample.geometryUpdateRejects > 0) {
            result.reasonMask |= Failure_GeometryUpdateRejected;
        }
        if (sample.replacementShaderBinds == 0 ||
            sample.replacementContractMask == 0) {
            result.reasonMask |= Failure_NoReplacementBind;
        }
        if (sample.bindingStateFailures > 0) {
            result.reasonMask |= Failure_BindingStateMismatch;
        }
        if (sample.bindingVerifiedContractMask == 0) {
            result.reasonMask |= Failure_NoVerifiedBinding;
        }
        if (sample.replacementDrawCalls == 0) {
            result.reasonMask |= Failure_NoReplacementDraw;
        }
        if (sample.drawStateFailures > 0) {
            result.reasonMask |= Failure_DrawStateMismatch;
        }
        if (sample.drawVerifiedContractMask == 0) {
            result.reasonMask |= Failure_NoVerifiedDraw;
        }
        if (result.fullyVerifiedContractMask == 0) {
            result.reasonMask |= Failure_NoCommonVerifiedContract;
        }

        const auto hardFailures = Failure_FeatureDisabled |
            Failure_GpuResourcesUnavailable |
            Failure_ShaderDetoursUnowned | Failure_DrawDetoursUnowned |
            Failure_GeometryHookUnowned | Failure_ShaderContractsIncomplete |
            Failure_GeometryProviderUnavailable;
        if ((result.reasonMask & hardFailures) != 0) {
            result.status = Status::failed;
        } else if (result.reasonMask == Failure_None) {
            result.status = Status::passed;
        } else if (timedOut) {
            result.status = Status::failed;
        }
        return result;
    }
}
