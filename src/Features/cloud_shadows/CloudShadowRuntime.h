#pragma once

#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace community_shaders::cloud_shadows
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool gpuReady{};
        bool cubeReady{};
        std::uint32_t populatedFaceMask{};
        std::uint64_t captureCandidates{};
        std::uint64_t capturedDraws{};
        std::uint64_t captureRejects{};
        std::uint64_t cubeRebuilds{};
        std::uint64_t lightingBinds{};
        std::uint64_t lightingRejects{};
        std::uint64_t restores{};
        std::uint64_t failures{};
    };

    class Runtime;

    class ScopedCloudCapture final
    {
    public:
        ScopedCloudCapture() noexcept = default;
        ~ScopedCloudCapture() noexcept;
        ScopedCloudCapture(const ScopedCloudCapture&) = delete;
        ScopedCloudCapture(ScopedCloudCapture&&) = delete;
        ScopedCloudCapture& operator=(const ScopedCloudCapture&) = delete;
        ScopedCloudCapture& operator=(ScopedCloudCapture&&) = delete;
        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedCloudCapture(
            Runtime* owner,
            ID3D11DeviceContext* context,
            std::uint32_t face,
            ID3D11RenderTargetView* cloudTarget,
            ID3D11BlendState* captureBlend) noexcept;

        Runtime* owner_{};
        ID3D11DeviceContext* context_{};
        std::uint32_t face_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 8>
            previousTargets_{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> previousBlend_;
        std::array<float, 4> previousBlendFactor_{};
        UINT previousSampleMask_{};
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
        [[nodiscard]] ScopedCloudCapture scopeCapture(
            ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding) noexcept;
        [[nodiscard]] bool prepareLighting(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView*& cube,
            ID3D11SamplerState*& sampler,
            float& opacity) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        friend class ScopedCloudCapture;
        Runtime() = default;
        [[nodiscard]] bool ensureCaptureResources(
            const D3D11_TEXTURE2D_DESC& source) noexcept;
        [[nodiscard]] bool ensureCaptureBlend(
            ID3D11BlendState* source) noexcept;
        void markFaceRendered(std::uint32_t face) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> cloudCube_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cloudView_;
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 6>
            cloudTargets_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> sourceBlend_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> captureBlend_;
        std::uint32_t cubeSize_{};
        std::atomic_uint32_t populatedFaces_{};
        std::uint32_t lastFace_{ 6 };
        std::atomic_bool enabled_{ true };
        std::atomic<float> opacity_{ 0.55f };
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool resourcesReady_{};
        std::atomic_uint64_t captureCandidates_{};
        std::atomic_uint64_t capturedDraws_{};
        std::atomic_uint64_t captureRejects_{};
        std::atomic_uint64_t cubeRebuilds_{};
        std::atomic_uint64_t lightingBinds_{};
        std::atomic_uint64_t lightingRejects_{};
        std::atomic_uint64_t restores_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstCaptureLogged_{};
        std::atomic_bool firstCaptureRejectLogged_{};
        std::atomic_bool firstCubeReadyLogged_{};
        std::atomic_bool firstLightingBindLogged_{};
        std::atomic_bool firstLightingRejectLogged_{};
    };
}
