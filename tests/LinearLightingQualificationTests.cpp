#include "diagnostics/LinearLightingQualificationModel.h"

#include <cstdlib>
#include <iostream>

namespace
{
    using namespace community_shaders::diagnostics::qualification_model;

    [[nodiscard]] Sample completeSample() noexcept
    {
        constexpr std::uint64_t contract = 1ull << 35;
        return {
            .sessionActivated = true,
            .enabled = true,
            .gpuResourcesReady = true,
            .geometryProviderReady = true,
            .shaderDetoursOwned = true,
            .drawDetoursOwned = true,
            .geometryHookOwned = true,
            .expectedShaderContracts = 37,
            .verifiedShaderContracts = 37,
            .matchingShaderContractMask = expectedContractMask(37),
            .geometryCalls = 2,
            .geometryAccepted = 2,
            .deepestGeometrySourceStage = 2,
            .geometryUpdates = 2,
            .replacementShaderBinds = 1,
            .replacementDrawCalls = 1,
            .replacementContractMask = contract,
            .bindingVerifiedContractMask = contract,
            .drawVerifiedContractMask = contract,
        };
    }

    [[nodiscard]] bool expect(
        bool condition,
        const char* message) noexcept
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    using namespace community_shaders::diagnostics::qualification_model;
    bool passed = true;

    const auto complete = evaluate(completeSample(), false);
    passed &= expect(complete.status == Status::passed,
        "complete render proof did not pass");
    passed &= expect(complete.reasonMask == Failure_None,
        "complete render proof retained blockers");
    passed &= expect(complete.fullyVerifiedContractMask == (1ull << 35),
        "complete render proof lost the common contract");

    auto waiting = completeSample();
    waiting.replacementDrawCalls = 0;
    waiting.drawVerifiedContractMask = 0;
    const auto waitingEvaluation = evaluate(waiting, false);
    passed &= expect(waitingEvaluation.status == Status::waiting,
        "incomplete non-timeout proof did not wait");
    passed &= expect(
        (waitingEvaluation.reasonMask & Failure_NoReplacementDraw) != 0,
        "missing draw was not classified");
    passed &= expect(evaluate(waiting, true).status == Status::failed,
        "incomplete timed-out proof did not fail");

    auto disabled = completeSample();
    disabled.enabled = false;
    passed &= expect(evaluate(disabled, false).status == Status::failed,
        "disabled feature did not fail immediately");

    auto incompleteContracts = completeSample();
    incompleteContracts.matchingShaderContractMask &= ~(1ull << 35);
    const auto contractEvaluation = evaluate(incompleteContracts, false);
    passed &= expect(contractEvaluation.status == Status::waiting,
        "live contract discovery did not remain observable until timeout");
    passed &= expect(
        evaluate(incompleteContracts, true).status == Status::failed,
        "incomplete live contract discovery passed after timeout");

    auto disjoint = completeSample();
    disjoint.replacementContractMask = 1ull << 33;
    disjoint.bindingVerifiedContractMask = 1ull << 34;
    disjoint.drawVerifiedContractMask = 1ull << 35;
    const auto disjointEvaluation = evaluate(disjoint, true);
    passed &= expect(disjointEvaluation.status == Status::failed,
        "disjoint proof masks passed");
    passed &= expect(
        (disjointEvaluation.reasonMask &
            Failure_NoCommonVerifiedContract) != 0,
        "disjoint proof masks were not classified");

    passed &= expect(expectedContractMask(0) == 0,
        "zero contract mask is invalid");
    passed &= expect(expectedContractMask(22) == 0x003FFFFFull,
        "22-contract mask is invalid");
    passed &= expect(expectedContractMask(37) == 0x0000001FFFFFFFFFull,
        "37-contract mask is invalid");
    passed &= expect(expectedContractMask(64) == UINT64_MAX,
        "full contract mask is invalid");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
