#pragma once

#include "Features/ibl/IblProviderModel.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace community_shaders::ibl
{
    enum class EnvironmentProviderState : std::uint8_t
    {
        uninitialized,
        ready,
        updating,
        disabledFailed,
    };

    struct EnvironmentProviderSnapshot
    {
        EnvironmentProviderState state{};
        std::uint32_t extent{};
        std::uint32_t mipCount{};
        std::uint64_t resourceGeneration{};
        std::uint64_t activeUpdateGeneration{};
        std::uint64_t publishedGeneration{};
        std::uint64_t completedSubresources{};
        std::uint64_t rebuildFailures{};
    };

    // Render-thread-only owner for the shared view-centred environment map.
    // Both cube chains are created transactionally. Updates always target the
    // private back chain and only a complete six-face/all-mip generation can
    // become the immutable published front chain.
    class EnvironmentProvider final
    {
    public:
        static constexpr DXGI_FORMAT kFormat =
            DXGI_FORMAT_R11G11B10_FLOAT;

        [[nodiscard]] bool initialize(
            ID3D11Device* device,
            std::uint32_t extent) noexcept;
        void reset() noexcept;

        [[nodiscard]] bool beginUpdate() noexcept;
        [[nodiscard]] bool markSubresourceComplete(
            EnvironmentCubeFace face,
            std::uint32_t mipLevel) noexcept;
        [[nodiscard]] bool publishUpdate() noexcept;
        void abortUpdate() noexcept;

        [[nodiscard]] ID3D11UnorderedAccessView* writableMip(
            std::uint32_t mipLevel) const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* publishedEnvironment()
            const noexcept;
        [[nodiscard]] ID3D11Texture2D* publishedTexture() const noexcept;
        [[nodiscard]] EnvironmentProviderSnapshot snapshot() const noexcept;

    private:
        struct CubeChain
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;
            std::array<
                Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>,
                kEnvironmentMaximumMipCount>
                mipUnorderedAccess;
        };

        struct ResourceSet
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            std::array<CubeChain, 2> chains;
            std::uint32_t extent{};
            std::uint32_t mipCount{};
        };

        [[nodiscard]] static bool validExtent(
            std::uint32_t extent) noexcept;
        [[nodiscard]] static bool createChain(
            ID3D11Device* device,
            std::uint32_t extent,
            std::uint32_t mipCount,
            CubeChain& chain) noexcept;

        ResourceSet resources_{};
        EnvironmentUpdateCoverage coverage_{};
        EnvironmentProviderState state_{
            EnvironmentProviderState::uninitialized
        };
        std::uint32_t frontChain_{};
        std::uint64_t resourceGeneration_{};
        std::uint64_t nextUpdateGeneration_{ 1 };
        std::uint64_t activeUpdateGeneration_{};
        std::uint64_t publishedGeneration_{};
        std::uint64_t rebuildFailures_{};
    };
}
