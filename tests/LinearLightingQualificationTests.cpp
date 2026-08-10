#include "diagnostics/LinearLightingQualificationModel.h"

#include <cstdlib>
#include <iostream>

namespace
{
    using namespace community_shaders::diagnostics::qualification_model;
    using namespace community_shaders::linear_lighting;

    [[nodiscard]] Sample completeSample() noexcept
    {
        ContractMask contract{};
        setContractBit(contract, 287);
        return {
            .sessionActivated = true,
            .enabled = true,
            .gpuResourcesReady = true,
            .geometryProviderReady = true,
            .shaderDetoursOwned = true,
            .drawDetoursOwned = true,
            .geometryHookOwned = true,
            .pointLightHookOwned = true,
            .expectedShaderContracts = 288,
            .verifiedShaderContracts = 288,
            .matchingShaderContractMask = expectedContractMask(288),
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
    using namespace community_shaders::linear_lighting;
    bool passed = true;

    const auto complete = evaluate(completeSample(), false);
    passed &= expect(complete.status == Status::passed,
        "complete render proof did not pass");
    passed &= expect(complete.reasonMask == Failure_None,
        "complete render proof retained blockers");
    passed &= expect(contractBitSet(
                         complete.fullyVerifiedContractMask,
                         287),
        "complete render proof lost the common contract");

    auto waiting = completeSample();
    waiting.replacementDrawCalls = 0;
    waiting.drawVerifiedContractMask = {};
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

    auto pointLightHookLost = completeSample();
    pointLightHookLost.pointLightHookOwned = false;
    const auto pointHookEvaluation = evaluate(pointLightHookLost, false);
    passed &= expect(pointHookEvaluation.status == Status::failed,
        "unowned point-light hook did not fail immediately");
    passed &= expect(
        (pointHookEvaluation.reasonMask &
            Failure_PointLightHookUnowned) != 0,
        "unowned point-light hook was not classified");

    auto incompleteContracts = completeSample();
    clearContractBit(incompleteContracts.matchingShaderContractMask, 191);
    const auto contractEvaluation = evaluate(incompleteContracts, false);
    passed &= expect(contractEvaluation.status == Status::waiting,
        "live contract discovery did not remain observable until timeout");
    passed &= expect(
        evaluate(incompleteContracts, true).status == Status::failed,
        "incomplete live contract discovery passed after timeout");

    auto disjoint = completeSample();
    disjoint.replacementContractMask = {};
    disjoint.bindingVerifiedContractMask = {};
    disjoint.drawVerifiedContractMask = {};
    setContractBit(disjoint.replacementContractMask, 63);
    setContractBit(disjoint.bindingVerifiedContractMask, 64);
    setContractBit(disjoint.drawVerifiedContractMask, 287);
    const auto disjointEvaluation = evaluate(disjoint, true);
    passed &= expect(disjointEvaluation.status == Status::failed,
        "disjoint proof masks passed");
    passed &= expect(
        (disjointEvaluation.reasonMask &
            Failure_NoCommonVerifiedContract) != 0,
        "disjoint proof masks were not classified");

    passed &= expect(!anyContractBit(expectedContractMask(0)),
        "zero contract mask is invalid");
    passed &= expect(expectedContractMask(22)[0] == 0x003FFFFFull,
        "22-contract mask is invalid");
    passed &= expect(expectedContractMask(64)[0] == UINT64_MAX,
        "full first-word contract mask is invalid");

    const auto sixtyFive = expectedContractMask(65);
    passed &= expect(
        sixtyFive[0] == UINT64_MAX && sixtyFive[1] == 1,
        "cross-word contract mask is invalid");

    const auto twoEightyEight = expectedContractMask(288);
    passed &= expect(
        twoEightyEight[0] == UINT64_MAX &&
            twoEightyEight[1] == UINT64_MAX &&
            twoEightyEight[2] == UINT64_MAX &&
            twoEightyEight[3] == UINT64_MAX &&
            twoEightyEight[4] == 0xFFFFFFFFull,
        "288-contract mask is invalid");

    AtomicContractMask atomicMask{};
    atomicMask.set(63);
    atomicMask.set(64);
    atomicMask.set(287, std::memory_order_release);
    const auto atomicSnapshot = atomicMask.load(std::memory_order_acquire);
    passed &= expect(
        contractBitSet(atomicSnapshot, 63) &&
            contractBitSet(atomicSnapshot, 64) &&
            contractBitSet(atomicSnapshot, 287),
        "atomic contract mask lost cross-word updates");
    atomicMask.clear();
    passed &= expect(!anyContractBit(atomicMask.load()),
        "atomic contract mask did not clear all words");

    auto overCapacity = completeSample();
    overCapacity.expectedShaderContracts = 321;
    overCapacity.verifiedShaderContracts = 321;
    const auto capacityEvaluation = evaluate(overCapacity, false);
    passed &= expect(capacityEvaluation.status == Status::failed,
        "over-capacity qualification did not fail immediately");
    passed &= expect(
        (capacityEvaluation.reasonMask &
            Failure_ShaderContractCapacityExceeded) != 0,
        "over-capacity qualification was not classified");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
