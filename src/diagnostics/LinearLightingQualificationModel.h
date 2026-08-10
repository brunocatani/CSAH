#pragma once

#include "Features/linear_lighting/LinearLightingContractMask.h"

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
        Failure_ShaderContractCapacityExceeded = 1ull << 20,
        Failure_PointLightHookUnowned = 1ull << 21,
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
        bool pointLightHookOwned{};
        std::uint32_t expectedShaderContracts{};
        std::uint32_t verifiedShaderContracts{};
        linear_lighting::ContractMask matchingShaderContractMask{};
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
        linear_lighting::ContractMask replacementContractMask{};
        linear_lighting::ContractMask bindingVerifiedContractMask{};
        linear_lighting::ContractMask drawVerifiedContractMask{};
    };

    struct Evaluation
    {
        Status status{ Status::waiting };
        std::uint64_t reasonMask{};
        linear_lighting::ContractMask fullyVerifiedContractMask{};
    };

    [[nodiscard]] inline Evaluation evaluate(
        const Sample& sample,
        bool timedOut) noexcept
    {
        Evaluation result{};
        const auto expectedMask = linear_lighting::expectedContractMask(
            sample.expectedShaderContracts);
        result.fullyVerifiedContractMask =
            linear_lighting::intersectContractMasks(
                sample.replacementContractMask,
                sample.bindingVerifiedContractMask,
                sample.drawVerifiedContractMask);

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
        if (!sample.pointLightHookOwned) {
            result.reasonMask |= Failure_PointLightHookUnowned;
        }
        if (sample.verifiedShaderContracts != sample.expectedShaderContracts) {
            result.reasonMask |= Failure_ShaderContractsIncomplete;
        }
        if (sample.expectedShaderContracts >
            linear_lighting::kContractMaskCapacity) {
            result.reasonMask |= Failure_ShaderContractCapacityExceeded;
        }
        if (!linear_lighting::anyContractBit(expectedMask) ||
            !linear_lighting::containsContractMask(
                sample.matchingShaderContractMask,
                expectedMask)) {
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
            !linear_lighting::anyContractBit(
                sample.replacementContractMask)) {
            result.reasonMask |= Failure_NoReplacementBind;
        }
        if (sample.bindingStateFailures > 0) {
            result.reasonMask |= Failure_BindingStateMismatch;
        }
        if (!linear_lighting::anyContractBit(
                sample.bindingVerifiedContractMask)) {
            result.reasonMask |= Failure_NoVerifiedBinding;
        }
        if (sample.replacementDrawCalls == 0) {
            result.reasonMask |= Failure_NoReplacementDraw;
        }
        if (sample.drawStateFailures > 0) {
            result.reasonMask |= Failure_DrawStateMismatch;
        }
        if (!linear_lighting::anyContractBit(
                sample.drawVerifiedContractMask)) {
            result.reasonMask |= Failure_NoVerifiedDraw;
        }
        if (!linear_lighting::anyContractBit(
                result.fullyVerifiedContractMask)) {
            result.reasonMask |= Failure_NoCommonVerifiedContract;
        }

        const auto hardFailures = Failure_FeatureDisabled |
            Failure_GpuResourcesUnavailable |
            Failure_ShaderDetoursUnowned | Failure_DrawDetoursUnowned |
            Failure_GeometryHookUnowned | Failure_ShaderContractsIncomplete |
            Failure_GeometryProviderUnavailable |
            Failure_PointLightHookUnowned |
            Failure_ShaderContractCapacityExceeded;
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
