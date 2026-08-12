#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>

namespace community_shaders::ibl
{
    // Owns the reflection-neutral inputs and one output-compatible scratch
    // target. Resource replacement is transactional: an existing compatible
    // set remains live if a resize/reformat rebuild fails.
    class ReflectionFreeCaptureResources final
    {
    public:
        [[nodiscard]] bool initialize(ID3D11Device* device) noexcept;
        void reset() noexcept;

        [[nodiscard]] bool prepareScratch(
            ID3D11RenderTargetView* outputView) noexcept;
        [[nodiscard]] bool scratchMatches(
            ID3D11RenderTargetView* outputView) const noexcept;

        [[nodiscard]] ID3D11Device* device() const noexcept
        {
            return device_.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* blackEnvironment() const
            noexcept
        {
            return blackEnvironmentSrv_.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* blackScreenReflection() const
            noexcept
        {
            return blackScreenReflectionSrv_.Get();
        }

        [[nodiscard]] ID3D11Texture2D* scratchTexture() const noexcept
        {
            return scratchTexture_.Get();
        }

        [[nodiscard]] ID3D11RenderTargetView* scratchRenderTarget() const
            noexcept
        {
            return scratchRenderTarget_.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* scratchShaderResource() const
            noexcept
        {
            return scratchShaderResource_.Get();
        }

        [[nodiscard]] const D3D11_TEXTURE2D_DESC& scratchDescription() const
            noexcept
        {
            return scratchDescription_;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> blackEnvironmentTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            blackEnvironmentSrv_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>
            blackScreenReflectionTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            blackScreenReflectionSrv_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> scratchTexture_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> scratchRenderTarget_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            scratchShaderResource_;
        D3D11_TEXTURE2D_DESC scratchDescription_{};
    };

    // Render-thread-only transaction used around one duplicate qualified
    // DFComposite draw. It changes only OM-RT0, the depth/stencil state, and
    // PS t8/t14, validates the applied identities, and restores the exact
    // retained objects on every exit. The retained DSV remains bound while
    // depth and stencil writes are disabled for the duplicate draw.
    class ScopedReflectionFreeCapture final
    {
    public:
        ScopedReflectionFreeCapture() noexcept = default;
        ScopedReflectionFreeCapture(
            ID3D11DeviceContext* context,
            ReflectionFreeCaptureResources& resources) noexcept;
        ~ScopedReflectionFreeCapture();

        ScopedReflectionFreeCapture(const ScopedReflectionFreeCapture&) =
            delete;
        ScopedReflectionFreeCapture& operator=(
            const ScopedReflectionFreeCapture&) = delete;
        ScopedReflectionFreeCapture(
            ScopedReflectionFreeCapture&& other) noexcept;
        ScopedReflectionFreeCapture& operator=(
            ScopedReflectionFreeCapture&&) = delete;

        [[nodiscard]] bool active() const noexcept
        {
            return active_;
        }

        [[nodiscard]] bool restore() noexcept;

    private:
        static constexpr UINT kRenderTargetCount =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

        [[nodiscard]] bool appliedStateMatches(
            ID3D11RenderTargetView* renderTarget,
            ID3D11DepthStencilView* depthStencil,
            ID3D11DepthStencilState* depthStencilState,
            UINT stencilReference,
            ID3D11ShaderResourceView* environment,
            ID3D11ShaderResourceView* screenReflection) const noexcept;

        ID3D11DeviceContext* context_{};
        std::array<
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
            kRenderTargetCount>
            renderTargets_{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencil_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>
            captureDepthStencilState_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> environment_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> screenReflection_;
        UINT renderTargetCount_{};
        UINT stencilReference_{};
        bool stateCaptured_{};
        bool active_{};
    };
}
