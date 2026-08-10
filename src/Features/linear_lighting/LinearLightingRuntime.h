#pragma once

#include "Features/linear_lighting/LinearLightingContractMask.h"
#include "Features/linear_lighting/FixedShaderBindingLookup.h"
#include "Features/linear_lighting/LinearLightingSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace community_shaders::linear_lighting
{
    class Runtime;

    // The active FO4VR FXP contains 631 unique Effect pixel-shader
    // identities. Reserve the complete fixed capacity now so incremental
    // coverage never requires allocation or another telemetry schema shape.
    inline constexpr std::size_t kEffectContractMaskWordCount = 10;
    inline constexpr std::size_t kEffectContractMaskCapacity =
        kContractMaskWordBits * kEffectContractMaskWordCount;
    using EffectContractMask =
        FixedContractMask<kEffectContractMaskWordCount>;
    using AtomicEffectContractMask =
        AtomicFixedContractMask<kEffectContractMaskWordCount>;

    enum class ReplacementShaderFamily : std::uint8_t
    {
        none,
        material,
        sky,
        distantTree,
        particle,
        effect,
        dFLightAmbient,
    };

    enum ReplacementPixelConstantFlag : std::uint8_t
    {
        ReplacementPixelConstants_None = 0,
        ReplacementPixelConstants_Frame = 1u << 0,
        ReplacementPixelConstants_Geometry = 1u << 1,
    };

    struct ReplacementShaderBinding
    {
        ReplacementShaderFamily family{ ReplacementShaderFamily::none };
        std::uint32_t contractPlusOne{};
        std::uint8_t constantFlags{};
    };

    // Owns the temporary pixel-constant-buffer state for one replacement
    // draw. The immediate context is non-owning and the scope must remain
    // inside the intercepted draw call.
    class ScopedReplacementPixelConstants final
    {
    public:
        ScopedReplacementPixelConstants() noexcept = default;
        ~ScopedReplacementPixelConstants() noexcept;

        ScopedReplacementPixelConstants(
            const ScopedReplacementPixelConstants&) = delete;
        ScopedReplacementPixelConstants(
            ScopedReplacementPixelConstants&&) = delete;
        ScopedReplacementPixelConstants& operator=(
            const ScopedReplacementPixelConstants&) = delete;
        ScopedReplacementPixelConstants& operator=(
            ScopedReplacementPixelConstants&&) = delete;

    private:
        friend class Runtime;

        ScopedReplacementPixelConstants(
            ID3D11DeviceContext* context,
            ID3D11Buffer* frameBuffer,
            ID3D11Buffer* geometryBuffer,
            std::uint8_t constantFlags,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        std::uint8_t constantFlags_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousFrameBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousGeometryBuffer_;
        std::atomic_uint64_t* restoreCounter_{};
    };

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
        std::uint32_t verifiedSkyShaderContracts{};
        std::uint16_t matchingSkyShaderContractMask{};
        std::uint32_t matchingSkyShadersCreated{};
        std::uint32_t trackedOriginalSkyShaders{};
        std::uint64_t skyReplacementBinds{};
        std::uint32_t verifiedDistantTreeShaderContracts{};
        std::uint8_t matchingDistantTreeShaderContractMask{};
        std::uint32_t matchingDistantTreeShadersCreated{};
        std::uint32_t trackedOriginalDistantTreeShaders{};
        std::uint64_t distantTreeReplacementBinds{};
        std::uint32_t verifiedParticleShaderContracts{};
        std::uint8_t matchingParticleShaderContractMask{};
        std::uint32_t matchingParticleShadersCreated{};
        std::uint32_t trackedOriginalParticleShaders{};
        std::uint64_t particleReplacementBinds{};
        std::uint32_t verifiedEffectShaderContracts{};
        EffectContractMask matchingEffectShaderContractMask{};
        std::uint32_t matchingEffectShadersCreated{};
        std::uint32_t trackedOriginalEffectShaders{};
        std::uint64_t effectReplacementBinds{};
        std::uint64_t shaderSelectionCalls{};
        std::uint64_t rejectedShaderContexts{};
        std::uint64_t inactiveShaderSelections{};
        std::uint64_t unmatchedShaderSelections{};
        std::uint64_t replacementBinds{};
        std::uint64_t replacementConstantScopes{};
        std::uint64_t replacementConstantRestores{};
        std::uint64_t shaderBindingLookupFailures{};
        std::uint32_t verifiedDFLightAmbientShaderContracts{};
        std::uint64_t matchingDFLightAmbientContractMask{};
        std::uint64_t readyDFLightAmbientContractMask{};
        std::uint32_t matchingDFLightAmbientShaders{};
        std::uint32_t trackedDFLightAmbientShaders{};
        std::uint64_t dFLightAmbientReplacementBinds{};
        std::uint64_t dFLightAmbientReplacementBuilds{};
        std::uint64_t dFLightAmbientReplacementFailures{};
        std::uint64_t dFLightAmbientGammaRebuilds{};
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
        ReplacementShaderBinding binding{};
        bool retainedForBind{};
    };

    class Runtime final
    {
    public:
        static constexpr std::size_t kShaderContractCount = 288;
        static constexpr std::size_t kSkyShaderContractCount = 8;
        static constexpr std::size_t kDistantTreeShaderContractCount = 1;
        static constexpr std::size_t kParticleShaderContractCount = 4;
        static constexpr std::size_t kEffectShaderContractCount = 28;
        static constexpr std::size_t kDFLightAmbientShaderContractCount = 39;
        static constexpr std::size_t kShaderBindingLookupCapacity = 32768;
        static constexpr std::size_t
            kMaximumTrackedOriginalShadersPerContract = 8;
        static_assert(kShaderContractCount <= kContractMaskCapacity);
        static_assert(
            kEffectShaderContractCount <= kEffectContractMaskCapacity);

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

        // Captures only the constant-buffer slots owned by the selected
        // replacement family, installs the private buffers for one draw, and
        // restores the exact captured state at scope exit.
        [[nodiscard]] ScopedReplacementPixelConstants
        scopeReplacementPixelConstants(
            ID3D11DeviceContext* context,
            ReplacementShaderBinding binding) noexcept;

        // Sampled by the qualification hooks only. D3D11 Get calls retain the
        // observed interfaces, which this method releases before returning.
        [[nodiscard]] std::uint32_t inspectReplacementPipelineState(
            ID3D11DeviceContext* context,
            ReplacementShaderBinding binding) const noexcept;

        [[nodiscard]] std::uint64_t geometryUpdateGeneration() const noexcept;
        [[nodiscard]] bool dFLightAmbientDescriptorReady(
            std::uint32_t descriptor) const noexcept;
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
        [[nodiscard]] bool createDFLightAmbientReplacement(
            std::span<const std::byte> originalBytecode,
            std::size_t contractIndex,
            float ambientGamma,
            Microsoft::WRL::ComPtr<ID3D11PixelShader>& replacement) noexcept;
        [[nodiscard]] bool rebuildDFLightAmbientReplacements(
            float ambientGamma) noexcept;
        [[nodiscard]] bool registerShaderBinding(
            ID3D11PixelShader* shader,
            ReplacementShaderBinding binding) noexcept;
        [[nodiscard]] PixelShaderSelection selectDFLightAmbientShader(
            ID3D11PixelShader* requested,
            std::size_t contractIndex) noexcept;
        void applyQueuedSettingsForRenderBoundary() noexcept;
        void publishFrameData() noexcept;

        using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**);

        Settings settings_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kShaderContractCount>
            replacementShaders_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSkyShaderContractCount>
            skyReplacementShaders_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDistantTreeShaderContractCount>
            distantTreeReplacementShaders_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kParticleShaderContractCount>
            particleReplacementShaders_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kEffectShaderContractCount>
            effectReplacementShaders_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDFLightAmbientShaderContractCount>
            dFLightAmbientReplacementShaders_{};
        std::array<std::vector<std::byte>,
            kDFLightAmbientShaderContractCount>
            dFLightAmbientOriginalBytecode_{};
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
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kSkyShaderContractCount>
            originalSkyShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kSkyShaderContractCount>
            originalSkyShaders_{};
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kDistantTreeShaderContractCount>
            originalDistantTreeShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kDistantTreeShaderContractCount>
            originalDistantTreeShaders_{};
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kParticleShaderContractCount>
            originalParticleShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kParticleShaderContractCount>
            originalParticleShaders_{};
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kEffectShaderContractCount>
            originalEffectShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kEffectShaderContractCount>
            originalEffectShaders_{};
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kDFLightAmbientShaderContractCount>
            dFLightAmbientOriginalShaderOwners_{};
        std::array<std::array<std::atomic<ID3D11PixelShader*>,
                       kMaximumTrackedOriginalShadersPerContract>,
            kDFLightAmbientShaderContractCount>
            dFLightAmbientOriginalShaders_{};
        FixedShaderBindingLookup<kShaderBindingLookupCapacity>
            shaderBindingLookup_{};
        CreatePixelShaderFunction createPixelShader_{};
        std::atomic_bool enabled_{};
        std::atomic_bool gpuResourcesReady_{};
        std::atomic_bool geometryProviderReady_{};
        AtomicContractMask matchingShaderContractMask_{};
        std::atomic_uint32_t matchingShadersCreated_{};
        std::atomic_uint32_t trackedOriginalShaders_{};
        std::atomic_uint16_t matchingSkyShaderContractMask_{};
        std::atomic_uint32_t matchingSkyShadersCreated_{};
        std::atomic_uint32_t trackedOriginalSkyShaders_{};
        std::atomic_uint8_t matchingDistantTreeShaderContractMask_{};
        std::atomic_uint32_t matchingDistantTreeShadersCreated_{};
        std::atomic_uint32_t trackedOriginalDistantTreeShaders_{};
        std::atomic_uint8_t matchingParticleShaderContractMask_{};
        std::atomic_uint32_t matchingParticleShadersCreated_{};
        std::atomic_uint32_t trackedOriginalParticleShaders_{};
        AtomicEffectContractMask matchingEffectShaderContractMask_{};
        std::atomic_uint32_t matchingEffectShadersCreated_{};
        std::atomic_uint32_t trackedOriginalEffectShaders_{};
        std::atomic_uint64_t shaderSelectionCalls_{};
        std::atomic_uint64_t rejectedShaderContexts_{};
        std::atomic_uint64_t inactiveShaderSelections_{};
        std::atomic_uint64_t unmatchedShaderSelections_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t skyReplacementBinds_{};
        std::atomic_uint64_t distantTreeReplacementBinds_{};
        std::atomic_uint64_t particleReplacementBinds_{};
        std::atomic_uint64_t effectReplacementBinds_{};
        std::atomic_uint64_t replacementConstantScopes_{};
        std::atomic_uint64_t replacementConstantRestores_{};
        std::atomic_uint64_t shaderBindingLookupFailures_{};
        std::atomic_uint64_t matchingDFLightAmbientContractMask_{};
        std::atomic_uint64_t readyDFLightAmbientContractMask_{};
        std::atomic_uint32_t dFLightAmbientGammaBits_{
            std::bit_cast<std::uint32_t>(Settings{}.ambientGamma) };
        std::atomic_uint32_t matchingDFLightAmbientShaders_{};
        std::atomic_uint32_t trackedDFLightAmbientShaders_{};
        std::atomic_uint64_t dFLightAmbientReplacementBinds_{};
        std::atomic_uint64_t dFLightAmbientReplacementBuilds_{};
        std::atomic_uint64_t dFLightAmbientReplacementFailures_{};
        std::atomic_uint64_t dFLightAmbientGammaRebuilds_{};
        std::atomic_uint64_t geometryUpdates_{};
        std::atomic_uint64_t rejectedGeometryUpdates_{};
        std::atomic_uint64_t geometryResourceRejects_{};
        std::atomic_uint64_t geometryDisabledRejects_{};
        std::atomic_uint64_t geometryInvalidSourceRejects_{};
        std::atomic_uint32_t firstReplacementContractPlusOne_{};
        std::atomic_uint64_t frameDataUploads_{};
        std::array<std::atomic_bool, kShaderContractCount>
            originalCapacityWarningLogged_{};
        std::array<std::atomic_bool, kSkyShaderContractCount>
            originalSkyCapacityWarningLogged_{};
        std::array<std::atomic_bool, kDistantTreeShaderContractCount>
            originalDistantTreeCapacityWarningLogged_{};
        std::array<std::atomic_bool, kParticleShaderContractCount>
            originalParticleCapacityWarningLogged_{};
        std::array<std::atomic_bool, kEffectShaderContractCount>
            originalEffectCapacityWarningLogged_{};
        std::array<std::atomic_bool, kDFLightAmbientShaderContractCount>
            dFLightAmbientCapacityWarningLogged_{};
        std::atomic_bool shaderBindingLookupCapacityWarningLogged_{};
        std::mutex shaderRegistryMutex_;
        std::mutex queuedSettingsMutex_;
        Settings queuedSettings_{};
        std::atomic_uint64_t queuedSettingsRevision_{};
        std::atomic_uint64_t appliedSettingsRevision_{};
    };
}
