#pragma once

#include "Features/pbr/PbrSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace community_shaders::linear_lighting
{
    struct ReplacementShaderBinding;
}

namespace community_shaders::pbr
{
    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool gpuReady{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t drawFallbacks{};
        std::uint64_t settingsUploads{};
        std::uint32_t authoredMaterials{};
        std::uint32_t resolvedAuthoredMaterials{};
        std::uint64_t manifestFailures{};
        std::uint64_t failures{};
    };

    class ScopedAuthoredMaterialBindings final
    {
    public:
        ScopedAuthoredMaterialBindings() noexcept = default;
        ~ScopedAuthoredMaterialBindings() noexcept;
        ScopedAuthoredMaterialBindings(
            const ScopedAuthoredMaterialBindings&) = delete;
        ScopedAuthoredMaterialBindings(
            ScopedAuthoredMaterialBindings&& other) noexcept;
        ScopedAuthoredMaterialBindings& operator=(
            const ScopedAuthoredMaterialBindings&) = delete;
        ScopedAuthoredMaterialBindings& operator=(
            ScopedAuthoredMaterialBindings&&) = delete;

        [[nodiscard]] bool active() const noexcept
        {
            return context_ && authoredShader_;
        }
        [[nodiscard]] ID3D11PixelShader* authoredShader() const noexcept
        {
            return authoredShader_;
        }
        [[nodiscard]] ID3D11PixelShader* previousShader() const noexcept
        {
            return previousShader_.Get();
        }

    private:
        friend class Runtime;
        ScopedAuthoredMaterialBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* rmaos,
            ID3D11PixelShader* previousShader,
            ID3D11PixelShader* authoredShader) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousRmaos_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> previousShader_;
        ID3D11PixelShader* authoredShader_{};
    };

    class ScopedDrawBindings final
    {
    public:
        ScopedDrawBindings() noexcept = default;
        ~ScopedDrawBindings() noexcept;
        ScopedDrawBindings(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings(ScopedDrawBindings&&) = delete;
        ScopedDrawBindings& operator=(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings& operator=(ScopedDrawBindings&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedDrawBindings(
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            ID3D11ShaderResourceView* pbrMaterial,
            ID3D11ShaderResourceView* surfaceClass,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            previousPbrMaterial_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousSurfaceClass_;
        bool materialResourcesBound_{};
        std::atomic_uint64_t* restoreCounter_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept;
        void applySettings(const Settings& settings) noexcept;
        void onGameDataReady() noexcept;
        void setLinearLightingEnabled(bool enabled) noexcept;
        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] Settings settings() const noexcept;
        [[nodiscard]] ScopedDrawBindings scopeDraw(
            ID3D11DeviceContext* context,
            bool active) noexcept;
        [[nodiscard]] ScopedAuthoredMaterialBindings
            scopeAuthoredMaterialDraw(
                ID3D11DeviceContext* context,
                const linear_lighting::ReplacementShaderBinding& binding,
                std::uint32_t surfaceClassCode) noexcept;
        void recordDrawFallback() noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        struct MaterialRegistry;

        Runtime() = default;
        ~Runtime();
        void uploadSettings(
            ID3D11DeviceContext* context,
            bool active) noexcept;
        void publishEffectiveState(const Settings& settings) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        std::atomic_bool configuredEnabled_{ true };
        std::atomic_bool linearLightingEnabled_{};
        std::atomic_bool legacyMaterials_{ true };
        std::atomic_bool directGgx_{ true };
        std::atomic_bool grassGgx_{};
        std::atomic_bool environmentFresnel_{ true };
        std::atomic_bool energyConservation_{ true };
        std::atomic_bool multiscatterCompensation_{ true };
        std::atomic_bool specularOcclusion_{ true };
        std::atomic<float> roughnessMultiplier_{ 1.0f };
        std::atomic<float> specularRoughnessBlend_{ 1.0f };
        std::atomic<float> baseF0Multiplier_{ 0.32f };
        std::atomic<float> minimumF0_{ 0.02f };
        std::atomic<float> cubemapToF0Multiplier_{ 1.0f };
        std::atomic<float> complexMaterialF0Multiplier_{ 1.0f };
        std::atomic<float> directLightingScale_{ 1.0f };
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool resourcesReady_{};
        std::uint64_t uploadedRevision_{};
        bool uploadedActive_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t drawFallbacks_{};
        std::atomic_uint64_t settingsUploads_{};
        std::unique_ptr<MaterialRegistry> materialRegistryOwner_;
        std::atomic<MaterialRegistry*> materialRegistry_{};
        std::atomic_bool materialRegistryLoaded_{};
        std::atomic_uint64_t manifestFailures_{};
        std::atomic_uint64_t failures_{};
    };
}
