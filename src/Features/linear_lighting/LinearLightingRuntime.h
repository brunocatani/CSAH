#pragma once

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
        std::uint32_t matchingShadersCreated{};
        std::uint32_t trackedOriginalShaders{};
        std::uint64_t replacementBinds{};
        std::uint64_t geometryUpdates{};
        std::uint64_t rejectedGeometryUpdates{};
    };

    class Runtime final
    {
    public:
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

        [[nodiscard]] ID3D11PixelShader* selectPixelShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* requested) noexcept;

        [[nodiscard]] bool updateGeometryEmissive(
            float emissiveMultiplier) noexcept;

        // UI/Prisma threads only publish immutable settings here. The render
        // hook consumes the latest revision before touching the immediate
        // context or any D3D11 resource.
        void queueSettings(const Settings& settings) noexcept;
        void applyQueuedSettingsForGeometryDraw() noexcept;

        void setGeometryProviderReady(bool ready) noexcept;

        // Must be called on the render thread. Prisma commands are queued and
        // applied at that boundary rather than mutating GPU state in callbacks.
        void applySettings(const Settings& settings) noexcept;

        [[nodiscard]] Settings settings() const noexcept;
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
        void publishFrameData() noexcept;

        static constexpr std::size_t kMaximumTrackedOriginalShaders = 8;

        Settings settings_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacementShader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> currentlyRequestedShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> frameBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> geometryBuffer_;
        std::array<std::atomic<ID3D11PixelShader*>,
            kMaximumTrackedOriginalShaders>
            originalShaders_{};
        std::atomic_bool enabled_{};
        std::atomic_bool gpuResourcesReady_{};
        std::atomic_bool geometryProviderReady_{};
        std::atomic_bool replacementCurrentlyBound_{};
        std::atomic_uint32_t matchingShadersCreated_{};
        std::atomic_uint32_t trackedOriginalShaders_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t geometryUpdates_{};
        std::atomic_uint64_t rejectedGeometryUpdates_{};
        std::mutex queuedSettingsMutex_;
        Settings queuedSettings_{};
        std::atomic_uint64_t queuedSettingsRevision_{};
        std::uint64_t appliedSettingsRevision_{};
    };
}
