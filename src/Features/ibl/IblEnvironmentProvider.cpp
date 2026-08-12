#include "Features/ibl/IblEnvironmentProvider.h"

#include <bit>
#include <utility>

namespace community_shaders::ibl
{
    bool EnvironmentProvider::validExtent(std::uint32_t extent) noexcept
    {
        return extent >= 16 && extent <= 512 && std::has_single_bit(extent);
    }

    bool EnvironmentProvider::createChain(
        ID3D11Device* device,
        std::uint32_t extent,
        std::uint32_t mipCount,
        CubeChain& chain) noexcept
    {
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = extent;
        textureDescription.Height = extent;
        textureDescription.MipLevels = mipCount;
        textureDescription.ArraySize = kEnvironmentCubeFaceCount;
        textureDescription.Format = kFormat;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_UNORDERED_ACCESS;
        textureDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (FAILED(device->CreateTexture2D(
                &textureDescription,
                nullptr,
                &chain.texture))) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription{};
        shaderResourceDescription.Format = kFormat;
        shaderResourceDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURECUBE;
        shaderResourceDescription.TextureCube.MostDetailedMip = 0;
        shaderResourceDescription.TextureCube.MipLevels = mipCount;
        if (FAILED(device->CreateShaderResourceView(
                chain.texture.Get(),
                &shaderResourceDescription,
                &chain.shaderResource))) {
            return false;
        }

        for (std::uint32_t mipLevel = 0; mipLevel < mipCount; ++mipLevel) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC unorderedAccessDescription{};
            unorderedAccessDescription.Format = kFormat;
            unorderedAccessDescription.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            unorderedAccessDescription.Texture2DArray.MipSlice = mipLevel;
            unorderedAccessDescription.Texture2DArray.FirstArraySlice = 0;
            unorderedAccessDescription.Texture2DArray.ArraySize =
                kEnvironmentCubeFaceCount;
            if (FAILED(device->CreateUnorderedAccessView(
                    chain.texture.Get(),
                    &unorderedAccessDescription,
                    &chain.mipUnorderedAccess[mipLevel]))) {
                return false;
            }
        }
        return true;
    }

    bool EnvironmentProvider::initialize(
        ID3D11Device* device,
        std::uint32_t extent) noexcept
    {
        if (!device || !validExtent(extent)) {
            ++rebuildFailures_;
            if (!resources_.device) {
                state_ = EnvironmentProviderState::disabledFailed;
            }
            return false;
        }
        if (resources_.device.Get() == device && resources_.extent == extent &&
            state_ != EnvironmentProviderState::disabledFailed) {
            return true;
        }

        UINT formatSupport{};
        constexpr UINT requiredFormatSupport =
            D3D11_FORMAT_SUPPORT_TEXTURE2D |
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
            D3D11_FORMAT_SUPPORT_MIP |
            D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
        if (FAILED(device->CheckFormatSupport(kFormat, &formatSupport)) ||
            (formatSupport & requiredFormatSupport) != requiredFormatSupport) {
            ++rebuildFailures_;
            if (!resources_.device) {
                state_ = EnvironmentProviderState::disabledFailed;
            }
            return false;
        }

        ResourceSet candidate{};
        candidate.device = device;
        candidate.extent = extent;
        candidate.mipCount = std::bit_width(extent);
        for (auto& chain : candidate.chains) {
            if (!createChain(
                    device,
                    candidate.extent,
                    candidate.mipCount,
                    chain)) {
                ++rebuildFailures_;
                if (!resources_.device) {
                    state_ = EnvironmentProviderState::disabledFailed;
                }
                return false;
            }
        }

        resources_ = std::move(candidate);
        coverage_.reset();
        state_ = EnvironmentProviderState::ready;
        frontChain_ = 0;
        ++resourceGeneration_;
        nextUpdateGeneration_ = 1;
        activeUpdateGeneration_ = 0;
        publishedGeneration_ = 0;
        return true;
    }

    void EnvironmentProvider::reset() noexcept
    {
        resources_ = {};
        coverage_.reset();
        state_ = EnvironmentProviderState::uninitialized;
        frontChain_ = 0;
        resourceGeneration_ = 0;
        nextUpdateGeneration_ = 1;
        activeUpdateGeneration_ = 0;
        publishedGeneration_ = 0;
        rebuildFailures_ = 0;
    }

    bool EnvironmentProvider::beginUpdate() noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            !resources_.device) {
            return false;
        }
        coverage_.reset();
        activeUpdateGeneration_ = nextUpdateGeneration_++;
        if (activeUpdateGeneration_ == 0) {
            activeUpdateGeneration_ = nextUpdateGeneration_++;
        }
        state_ = EnvironmentProviderState::updating;
        return true;
    }

    bool EnvironmentProvider::markSubresourceComplete(
        EnvironmentCubeFace face,
        std::uint32_t mipLevel) noexcept
    {
        return state_ == EnvironmentProviderState::updating &&
            coverage_.markComplete(face, mipLevel, resources_.mipCount);
    }

    bool EnvironmentProvider::publishUpdate() noexcept
    {
        if (state_ != EnvironmentProviderState::updating ||
            !coverage_.complete(resources_.mipCount)) {
            return false;
        }
        frontChain_ = 1 - frontChain_;
        publishedGeneration_ = activeUpdateGeneration_;
        activeUpdateGeneration_ = 0;
        coverage_.reset();
        state_ = EnvironmentProviderState::ready;
        return true;
    }

    void EnvironmentProvider::abortUpdate() noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return;
        }
        coverage_.reset();
        activeUpdateGeneration_ = 0;
        state_ = EnvironmentProviderState::ready;
    }

    ID3D11UnorderedAccessView* EnvironmentProvider::writableMip(
        std::uint32_t mipLevel) const noexcept
    {
        if (state_ != EnvironmentProviderState::updating ||
            mipLevel >= resources_.mipCount) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .mipUnorderedAccess[mipLevel]
            .Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::publishedEnvironment()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].shaderResource.Get();
    }

    ID3D11Texture2D* EnvironmentProvider::publishedTexture() const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].texture.Get();
    }

    EnvironmentProviderSnapshot EnvironmentProvider::snapshot() const
        noexcept
    {
        return {
            .state = state_,
            .extent = resources_.extent,
            .mipCount = resources_.mipCount,
            .resourceGeneration = resourceGeneration_,
            .activeUpdateGeneration = activeUpdateGeneration_,
            .publishedGeneration = publishedGeneration_,
            .completedSubresources = coverage_.bits(),
            .rebuildFailures = rebuildFailures_,
        };
    }
}
