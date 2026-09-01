#pragma once

#include "Features/pbr/PbrSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

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
        std::uint64_t failures{};
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
            ID3D11ShaderResourceView* surfaceClass,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousSurfaceClass_;
        bool surfaceClassBound_{};
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
        void setLinearLightingEnabled(bool enabled) noexcept;
        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] Settings settings() const noexcept;
        [[nodiscard]] ScopedDrawBindings scopeDraw(
            ID3D11DeviceContext* context,
            bool active) noexcept;
        void recordDrawFallback() noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
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
        std::atomic_uint64_t failures_{};
    };
}
