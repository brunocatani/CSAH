#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

namespace csah::basic_wetness
{
    struct RuntimeSnapshot
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
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            previousSurfaceClass_;
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
        [[nodiscard]] bool requested() const noexcept;
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

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        std::atomic_bool enabled_{};
        std::atomic<float> wetness_{ 0.65f };
        std::atomic<float> diffuseDarkening_{ 0.18f };
        std::atomic<float> specularMultiplier_{ 1.6f };
        std::atomic<float> roughnessScale_{ 0.35f };
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
