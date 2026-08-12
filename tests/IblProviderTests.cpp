#include "Features/ibl/IblComputeStateScope.h"
#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblProviderModel.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;
    using community_shaders::ibl::ComputeStateFootprint;
    using community_shaders::ibl::EnvironmentCubeFace;
    using community_shaders::ibl::EnvironmentProvider;
    using community_shaders::ibl::EnvironmentProviderState;
    using community_shaders::ibl::EnvironmentUpdateCoverage;
    using community_shaders::ibl::ScopedComputeState;

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            fail(message);
        }
    }

    void requireSucceeded(HRESULT result, const std::string& operation)
    {
        if (FAILED(result)) {
            fail(operation + " failed with HRESULT " +
                std::to_string(static_cast<unsigned>(result)));
        }
    }

    void requireNear(float actual, float expected, const char* label)
    {
        if (std::abs(actual - expected) > 0.0001f) {
            fail(std::string(label) + " differed");
        }
    }

    struct Device
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    [[nodiscard]] Device createDevice()
    {
        Device result;
        constexpr std::array levels{ D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selected{};
        requireSucceeded(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                levels.data(),
                static_cast<UINT>(levels.size()),
                D3D11_SDK_VERSION,
                &result.device,
                &selected,
                &result.context),
            "D3D11CreateDevice(WARP)");
        require(
            selected == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");
        return result;
    }

    [[nodiscard]] ComPtr<ID3D11ComputeShader> createComputeShader(
        ID3D11Device& device)
    {
        constexpr auto source =
            "[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {}";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        requireSucceeded(
            D3DCompile(
                source,
                std::strlen(source),
                "IblProviderTests",
                nullptr,
                nullptr,
                "main",
                "cs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS,
                0,
                &bytecode,
                &errors),
            "D3DCompile(CS)");
        ComPtr<ID3D11ComputeShader> shader;
        requireSucceeded(
            device.CreateComputeShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                &shader),
            "CreateComputeShader");
        return shader;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createShaderResource(
        ID3D11Device& device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32_FLOAT;
        description.SampleDesc.Count = 1;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, nullptr, &texture),
            "CreateTexture2D(SRV)");
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11UnorderedAccessView> createUnorderedAccess(
        ID3D11Device& device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32_UINT;
        description.SampleDesc.Count = 1;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, nullptr, &texture),
            "CreateTexture2D(UAV)");
        ComPtr<ID3D11UnorderedAccessView> view;
        requireSucceeded(
            device.CreateUnorderedAccessView(texture.Get(), nullptr, &view),
            "CreateUnorderedAccessView");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11SamplerState> createSampler(
        ID3D11Device& device,
        D3D11_TEXTURE_ADDRESS_MODE addressMode)
    {
        D3D11_SAMPLER_DESC description{};
        description.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        description.AddressU = addressMode;
        description.AddressV = addressMode;
        description.AddressW = addressMode;
        description.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        requireSucceeded(
            device.CreateSamplerState(&description, &sampler),
            "CreateSamplerState");
        return sampler;
    }

    [[nodiscard]] ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device& device)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 16;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> buffer;
        requireSucceeded(
            device.CreateBuffer(&description, nullptr, &buffer),
            "CreateBuffer(CB)");
        return buffer;
    }

    void testOrientationAndCoverage()
    {
        const auto positiveX =
            community_shaders::ibl::environmentCubeDirection(
                EnvironmentCubeFace::positiveX,
                0.0f,
                0.0f);
        requireNear(positiveX.x, 1.0f, "+X centre x");
        requireNear(positiveX.y, 0.0f, "+X centre y");
        requireNear(positiveX.z, 0.0f, "+X centre z");
        const auto negativeZ =
            community_shaders::ibl::environmentCubeDirection(
                EnvironmentCubeFace::negativeZ,
                0.0f,
                0.0f);
        requireNear(negativeZ.z, -1.0f, "-Z centre z");
        const auto positiveYCorner =
            community_shaders::ibl::environmentCubeDirection(
                EnvironmentCubeFace::positiveY,
                1.0f,
                1.0f);
        require(
            positiveYCorner.x > 0.0f && positiveYCorner.y > 0.0f &&
                positiveYCorner.z < 0.0f,
            "+Y face orientation changed");

        EnvironmentUpdateCoverage coverage;
        constexpr std::uint32_t mipCount = 8;
        for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
            for (std::uint32_t face = 0;
                 face < community_shaders::ibl::kEnvironmentCubeFaceCount;
                 ++face) {
                if (mip == mipCount - 1 && face == 5) {
                    continue;
                }
                require(
                    coverage.markComplete(
                        static_cast<EnvironmentCubeFace>(face),
                        mip,
                        mipCount),
                    "valid coverage bit was rejected");
            }
        }
        require(!coverage.complete(mipCount), "partial coverage was complete");
        require(
            coverage.markComplete(
                EnvironmentCubeFace::negativeZ,
                mipCount - 1,
                mipCount),
            "final coverage bit was rejected");
        require(coverage.complete(mipCount), "full coverage was incomplete");
    }

    void markCompleteGeneration(EnvironmentProvider& provider)
    {
        const auto mipCount = provider.snapshot().mipCount;
        for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
            require(provider.writableMip(mip) != nullptr, "missing mip UAV");
            for (std::uint32_t face = 0;
                 face < community_shaders::ibl::kEnvironmentCubeFaceCount;
                 ++face) {
                require(
                    provider.markSubresourceComplete(
                        static_cast<EnvironmentCubeFace>(face),
                        mip),
                    "provider rejected completed subresource");
            }
        }
    }

    void testTransactionalProvider(Device& d3d)
    {
        EnvironmentProvider provider;
        require(
            provider.initialize(d3d.device.Get(), 128),
            "provider did not initialize");
        auto snapshot = provider.snapshot();
        require(
            snapshot.state == EnvironmentProviderState::ready,
            "provider did not become ready");
        require(snapshot.extent == 128, "provider extent changed");
        require(snapshot.mipCount == 8, "provider mip count changed");
        require(
            provider.publishedEnvironment() == nullptr,
            "uninitialized front chain was published");

        require(provider.beginUpdate(), "first update did not begin");
        require(
            provider.writableTexture() != nullptr,
            "active update did not expose its private texture");
        markCompleteGeneration(provider);
        require(provider.publishUpdate(), "complete update did not publish");
        auto* firstPublishedView = provider.publishedEnvironment();
        auto* firstPublishedTexture = provider.publishedTexture();
        require(firstPublishedView != nullptr, "published SRV is null");
        require(firstPublishedTexture != nullptr, "published texture is null");
        require(
            provider.snapshot().publishedGeneration == 1,
            "first generation identity changed");

        require(
            !provider.initialize(d3d.device.Get(), 3),
            "invalid rebuild unexpectedly succeeded");
        require(
            provider.publishedEnvironment() == firstPublishedView &&
                provider.publishedTexture() == firstPublishedTexture,
            "failed rebuild replaced the published chain");
        require(
            provider.snapshot().state == EnvironmentProviderState::ready,
            "failed rebuild disabled valid resources");

        require(provider.beginUpdate(), "second update did not begin");
        ComPtr<ID3D11Resource> writableResource;
        provider.writableMip(0)->GetResource(&writableResource);
        require(
            writableResource.Get() != firstPublishedTexture,
            "update targeted the published front chain");
        require(
            !provider.publishUpdate(),
            "incomplete update was published");
        provider.abortUpdate();
        require(
            provider.writableTexture() == nullptr,
            "aborted update retained writable texture access");
        require(
            provider.publishedEnvironment() == firstPublishedView,
            "aborted update replaced the published chain");

        require(provider.beginUpdate(), "replacement update did not begin");
        markCompleteGeneration(provider);
        require(provider.publishUpdate(), "replacement update did not publish");
        require(
            provider.publishedTexture() != firstPublishedTexture,
            "double buffer did not swap after complete publication");
        require(
            provider.snapshot().publishedGeneration == 3,
            "aborted generation was not kept private");
    }

    template <class T>
    [[nodiscard]] ComPtr<T> getBound(
        ID3D11DeviceContext& context,
        void (ID3D11DeviceContext::*getter)(UINT, UINT, T**),
        UINT slot)
    {
        T* raw{};
        (context.*getter)(slot, 1, &raw);
        ComPtr<T> result;
        result.Attach(raw);
        return result;
    }

    void testComputeStateRestoration(Device& d3d)
    {
        const auto originalShader = createComputeShader(*d3d.device.Get());
        const auto replacementShader = createComputeShader(*d3d.device.Get());
        const auto originalSrv3 = createShaderResource(*d3d.device.Get());
        const auto originalSrv4 = createShaderResource(*d3d.device.Get());
        const auto replacementSrv = createShaderResource(*d3d.device.Get());
        const auto originalUav = createUnorderedAccess(*d3d.device.Get());
        const auto replacementUav = createUnorderedAccess(*d3d.device.Get());
        const auto originalSampler = createSampler(
            *d3d.device.Get(),
            D3D11_TEXTURE_ADDRESS_CLAMP);
        const auto replacementSampler = createSampler(
            *d3d.device.Get(),
            D3D11_TEXTURE_ADDRESS_WRAP);
        const auto originalCb4 = createConstantBuffer(*d3d.device.Get());
        const auto originalCb5 = createConstantBuffer(*d3d.device.Get());
        const auto replacementCb = createConstantBuffer(*d3d.device.Get());

        std::array<ID3D11ShaderResourceView*, 2> originalSrvs{
            originalSrv3.Get(),
            originalSrv4.Get(),
        };
        std::array<ID3D11Buffer*, 2> originalCbs{
            originalCb4.Get(),
            originalCb5.Get(),
        };
        auto* originalUavRaw = originalUav.Get();
        auto* originalSamplerRaw = originalSampler.Get();
        d3d.context->CSSetShader(originalShader.Get(), nullptr, 0);
        d3d.context->CSSetShaderResources(3, 2, originalSrvs.data());
        d3d.context->CSSetUnorderedAccessViews(
            2,
            1,
            &originalUavRaw,
            nullptr);
        d3d.context->CSSetSamplers(1, 1, &originalSamplerRaw);
        d3d.context->CSSetConstantBuffers(4, 2, originalCbs.data());

        ScopedComputeState scope(
            d3d.context.Get(),
            ComputeStateFootprint{
                .firstShaderResource = 3,
                .shaderResourceCount = 2,
                .firstUnorderedAccess = 2,
                .unorderedAccessCount = 1,
                .firstSampler = 1,
                .samplerCount = 1,
                .firstConstantBuffer = 4,
                .constantBufferCount = 2,
            });
        require(scope.captured(), "compute state was not captured");
        auto* replacementSrvRaw = replacementSrv.Get();
        auto* replacementUavRaw = replacementUav.Get();
        auto* replacementSamplerRaw = replacementSampler.Get();
        auto* replacementCbRaw = replacementCb.Get();
        d3d.context->CSSetShader(replacementShader.Get(), nullptr, 0);
        d3d.context->CSSetShaderResources(3, 1, &replacementSrvRaw);
        d3d.context->CSSetUnorderedAccessViews(
            2,
            1,
            &replacementUavRaw,
            nullptr);
        d3d.context->CSSetSamplers(1, 1, &replacementSamplerRaw);
        d3d.context->CSSetConstantBuffers(4, 1, &replacementCbRaw);
        require(scope.restore(), "compute state did not restore");
        require(!scope.restore(), "compute state restored twice");

        ID3D11ComputeShader* restoredShaderRaw{};
        UINT classCount{};
        d3d.context->CSGetShader(&restoredShaderRaw, nullptr, &classCount);
        ComPtr<ID3D11ComputeShader> restoredShader;
        restoredShader.Attach(restoredShaderRaw);
        require(
            restoredShader.Get() == originalShader.Get(),
            "compute shader identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetShaderResources,
                3)
                    .Get() == originalSrv3.Get(),
            "CS t3 identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetShaderResources,
                4)
                    .Get() == originalSrv4.Get(),
            "CS t4 identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetUnorderedAccessViews,
                2)
                    .Get() == originalUav.Get(),
            "CS u2 identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetSamplers,
                1)
                    .Get() == originalSampler.Get(),
            "CS s1 identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetConstantBuffers,
                4)
                    .Get() == originalCb4.Get(),
            "CS b4 identity was not restored");
        require(
            getBound(
                *d3d.context.Get(),
                &ID3D11DeviceContext::CSGetConstantBuffers,
                5)
                    .Get() == originalCb5.Get(),
            "CS b5 identity was not restored");
    }
}

int main()
{
    try {
        testOrientationAndCoverage();
        auto d3d = createDevice();
        testTransactionalProvider(d3d);
        testComputeStateRestoration(d3d);
        std::cout << "IBL provider tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "IBL provider tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
