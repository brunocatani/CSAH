#pragma once

#include "Features/linear_lighting/LinearLightingContractMask.h"
#include "Features/linear_lighting/LinearLightingSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace community_shaders::linear_lighting
{
    struct RuntimeSnapshot
    {
        bool enabled{};
        bool gpuResourcesReady{};
        bool geometryProviderReady{};
        std::uint32_t verifiedShaderContracts{};
        ContractMask matchingShaderContractMask{};
        std::uint32_t matchingShadersCreated{};
        std::uint32_t trackedOriginalShaders{};
        std::uint32_t firstReplacementContractPlusOne{};
        std::uint64_t shaderSelectionCalls{};
        std::uint64_t rejectedShaderContexts{};
        std::uint64_t inactiveShaderSelections{};
        std::uint64_t unmatchedShaderSelections{};
        std::uint64_t replacementBinds{};
        std::uint64_t geometryUpdates{};
        std::uint64_t rejectedGeometryUpdates{};
        std::uint64_t geometryResourceRejects{};
        std::uint64_t geometryDisabledRejects{};
        std::uint64_t geometryInvalidSourceRejects{};
        std::uint64_t queuedSettingsRevision{};
        std::uint64_t appliedSettingsRevision{};
        std::uint64_t frameDataUploads{};
    };

    enum PipelineBindingStateFlag : std::uint32_t
    {
        PipelineBinding_SelectedReplacement = 1u << 0,
        PipelineBinding_FrameBuffer = 1u << 1,
        PipelineBinding_GeometryBuffer = 1u << 2,
        PipelineBinding_All = PipelineBinding_SelectedReplacement |
            PipelineBinding_FrameBuffer | PipelineBinding_GeometryBuffer,
    };

    struct PixelShaderSelection
    {
        ID3D11PixelShader* shader{};
        std::uint32_t contractPlusOne{};
    };

    class Runtime final
    {
    public:
        static constexpr std::size_t kShaderContractCount = 134;
        static_assert(kShaderContractCount <= kContractMaskCapacity);

        static Runtime& get() noexcept;

        Runtime(const Runtime&) = delete;
        Runtime(Runtime&&) = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime& operator=(Runtime&&) = delete;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            HRESULT(STDMETHODCALLTYPE* createPixelShader)(
                ID3D11Device*,
                const void*,
                SIZE_T,
                ID3D11ClassLinkage*,
                ID3D11PixelShader**)) noexcept;

        void onPixelShaderCreated(
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11PixelShader* shader) noexcept;

        [[nodiscard]] PixelShaderSelection selectPixelShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* requested) noexcept;

        // Sampled by the qualification hooks only. D3D11 Get calls retain the
        // observed interfaces, which this method releases before returning.
        [[nodiscard]] std::uint32_t inspectReplacementPipelineState(
            ID3D11DeviceContext* context,
            std::uint32_t contractPlusOne) const noexcept;

        [[nodiscard]] std::uint64_t geometryUpdateGeneration() const noexcept;
        [[nodiscard]] static const char* shaderContractName(
            std::size_t contractIndex) noexcept;

        [[nodiscard]] bool updateGeometryEmissive(
            float emissiveMultiplier) noexcept;

        // UI/Prisma threads only publish immutable settings here. The render
        // boundary consumes the latest revision before selecting a shader or
        // touching the immediate context.
        void queueSettings(const Settings& settings) noexcept;

        void setGeometryProviderReady(bool ready) noexcept;

        // Must be called on the render thread. Prisma commands are queued and
        // applied at that boundary rather than mutating GPU state in callbacks.
        void applySettings(const Settings& settings) noexcept;

        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;

        [[nodiscard]] bool createResources(
            ID3D11Device* device,
            HRESULT(STDMETHODCALLTYPE* createPixelShader)(
                ID3D11Device*,
                const void*,
                SIZE_T,
                ID3D11ClassLinkage*,
                ID3D11PixelShader**)) noexcept;
        void applyQueuedSettingsForRenderBoundary() noexcept;
        void publishFrameData() noexcept;

        static constexpr std::size_t kMaximumTrackedOriginalShadersPerContract = 8;

        Settings settings_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kShaderContractCount>
            replacementShaders_{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> currentlyRequestedShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> frameBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> geometryBuffer_;
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kShaderContractCount>
            originalShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kShaderContractCount>
            originalShaders_{};
        std::atomic_bool enabled_{};
        std::atomic_bool gpuResourcesReady_{};
        std::atomic_bool geometryProviderReady_{};
        AtomicContractMask matchingShaderContractMask_{};
        std::atomic_uint32_t matchingShadersCreated_{};
        std::atomic_uint32_t trackedOriginalShaders_{};
        std::atomic_uint64_t shaderSelectionCalls_{};
        std::atomic_uint64_t rejectedShaderContexts_{};
        std::atomic_uint64_t inactiveShaderSelections_{};
        std::atomic_uint64_t unmatchedShaderSelections_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t geometryUpdates_{};
        std::atomic_uint64_t rejectedGeometryUpdates_{};
        std::atomic_uint64_t geometryResourceRejects_{};
        std::atomic_uint64_t geometryDisabledRejects_{};
        std::atomic_uint64_t geometryInvalidSourceRejects_{};
        std::atomic_uint32_t firstReplacementContractPlusOne_{};
        std::atomic_uint64_t frameDataUploads_{};
        std::array<std::atomic_bool, kShaderContractCount>
            originalCapacityWarningLogged_{};
        std::mutex shaderRegistryMutex_;
        std::mutex queuedSettingsMutex_;
        Settings queuedSettings_{};
        std::atomic_uint64_t queuedSettingsRevision_{};
        std::atomic_uint64_t appliedSettingsRevision_{};
    };
}
