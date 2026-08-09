#pragma once

#include "Features/linear_lighting/LinearLightingContractMask.h"

#include <cstdint>

namespace community_shaders::render
{
    struct HookSnapshot
    {
        bool deviceCreationImportInstalled{};
        bool deviceCreationImportOwned{};
        bool deviceCaptured{};
        bool deviceHooksInstalled{};
        bool shaderInterceptionActive{};
        bool createPixelShaderDetourEnabled{};
        bool pixelShaderBindDetourEnabled{};
        bool qualificationDrawDetoursInstalled{};
        bool qualificationDrawDetoursOwned{};
        std::uint64_t shaderHookInstallFailures{};
        std::uint64_t shaderHookValidationFailures{};
        std::uint64_t qualificationDrawHookInstallFailures{};
        std::uint64_t qualificationDrawHookValidationFailures{};
        std::uint64_t pixelShaderBindRecursions{};
        std::uint64_t deviceCreationCalls{};
        std::uint64_t pixelShaderCreationCalls{};
        std::uint64_t pixelShaderBindCalls{};
    };

    struct QualificationSnapshot
    {
        bool sessionActive{};
        std::uint64_t sessionId{};
        std::uint64_t geometryUpdateBaseline{};
        std::uint64_t replacementShaderBinds{};
        std::uint64_t drawIndexedCalls{};
        std::uint64_t drawCalls{};
        std::uint64_t drawIndexedInstancedCalls{};
        std::uint64_t drawInstancedCalls{};
        std::uint64_t replacementDrawCalls{};
        std::uint64_t bindingStateChecks{};
        std::uint64_t bindingStateFailures{};
        std::uint64_t drawStateChecks{};
        std::uint64_t drawStateFailures{};
        std::uint64_t bindingsWithoutFreshGeometry{};
        std::uint64_t drawsWithoutFreshGeometry{};
        linear_lighting::ContractMask replacementContractMask{};
        linear_lighting::ContractMask bindingVerifiedContractMask{};
        linear_lighting::ContractMask drawVerifiedContractMask{};
        std::uint32_t lastBindingState{};
        std::uint32_t lastDrawState{};
    };

    // Installs before Fallout4VR creates the D3D11 device. The import entry is
    // found by PE metadata and checked against the live d3d11 export first.
    [[nodiscard]] bool installEarlyD3D11Hooks() noexcept;
    // Verifies the live native method detours without mutating an unknown
    // downstream chain. Failed validation is rate-limited and fail-closed.
    [[nodiscard]] bool validateD3D11ShaderHooks(
        const char* trigger) noexcept;
    [[nodiscard]] HookSnapshot d3d11HookSnapshot() noexcept;

    // A new session invalidates render-thread-local proof state through its
    // monotonically increasing ID. All counters below are session-local.
    [[nodiscard]] std::uint64_t beginD3D11QualificationSession(
        std::uint64_t geometryUpdateBaseline) noexcept;
    void endD3D11QualificationSession(std::uint64_t sessionId) noexcept;
    [[nodiscard]] QualificationSnapshot d3d11QualificationSnapshot() noexcept;
}
