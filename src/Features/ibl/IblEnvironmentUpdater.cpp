#include "Features/ibl/IblEnvironmentUpdater.h"

#include "Features/ibl/IblComputeStateScope.h"
#include "Features/ibl/IblSceneRadianceProbeModel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace community_shaders::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::uint32_t kMinimumSceneConstantRows = 71;
        constexpr UINT kThreadGroupExtent = 8;
        constexpr float kNonBlackThreshold = 1.0e-5f;

        [[nodiscard]] bool sameDevice(
            ID3D11DeviceChild* child,
            ID3D11Device* expected) noexcept
        {
            if (!child || !expected) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            child->GetDevice(&device);
            return device.Get() == expected;
        }

        [[nodiscard]] bool textureFromView(
            ID3D11ShaderResourceView* view,
            ComPtr<ID3D11Texture2D>& texture,
            D3D11_SHADER_RESOURCE_VIEW_DESC& viewDescription,
            D3D11_TEXTURE2D_DESC& textureDescription) noexcept
        {
            if (!view) {
                return false;
            }
            view->GetDesc(&viewDescription);
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDesc(&textureDescription);
            return true;
        }
    }

    bool EnvironmentUpdater::initialize(
        ID3D11Device* device,
        const void* shaderBytecode,
        std::size_t shaderBytecodeSize,
        std::uint32_t extent) noexcept
    {
        if (!device || !shaderBytecode || shaderBytecodeSize < 20 ||
            std::memcmp(shaderBytecode, "DXBC", 4) != 0 || extent < 16 ||
            extent > 512 || !std::has_single_bit(extent)) {
            recordFailure();
            return false;
        }

        Resources candidate{};
        candidate.device = device;
        candidate.extent = extent;
        if (FAILED(device->CreateComputeShader(
                shaderBytecode,
                shaderBytecodeSize,
                nullptr,
                &candidate.shader))) {
            recordFailure();
            return false;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(
                &samplerDescription,
                &candidate.sampler))) {
            recordFailure();
            return false;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(UpdateConstants);
        constantDescription.Usage = D3D11_USAGE_DEFAULT;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(
                &constantDescription,
                nullptr,
                &candidate.updateConstants))) {
            recordFailure();
            return false;
        }

        D3D11_TEXTURE2D_DESC stagingDescription{};
        stagingDescription.Width = extent;
        stagingDescription.Height = extent;
        stagingDescription.MipLevels = 1;
        stagingDescription.ArraySize = kEnvironmentCubeFaceCount;
        stagingDescription.Format = EnvironmentProvider::kFormat;
        stagingDescription.SampleDesc.Count = 1;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(
                &stagingDescription,
                nullptr,
                &candidate.stagingMipZero))) {
            recordFailure();
            return false;
        }

        D3D11_QUERY_DESC queryDescription{ D3D11_QUERY_EVENT, 0 };
        if (FAILED(device->CreateQuery(
                &queryDescription,
                &candidate.completionEvent))) {
            recordFailure();
            return false;
        }

        resources_ = std::move(candidate);
        summary_ = {};
        summary_.initialized = true;
        nextGeneration_ = 1;
        return true;
    }

    void EnvironmentUpdater::reset() noexcept
    {
        resources_ = {};
        summary_ = {};
        nextGeneration_ = 1;
    }

    bool EnvironmentUpdater::validateInputs(
        ID3D11DeviceContext* context,
        const EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants,
        D3D11_TEXTURE2D_DESC& radianceDescription) const noexcept
    {
        if (!summary_.initialized || summary_.pending || !context ||
            !resources_.device || !sameDevice(context, resources_.device.Get()) ||
            !sameDevice(reflectionFreeRadiance, resources_.device.Get()) ||
            !sameDevice(sceneDepth, resources_.device.Get()) ||
            !sameDevice(sceneConstants, resources_.device.Get())) {
            return false;
        }

        const auto providerSnapshot = provider.snapshot();
        if (providerSnapshot.state != EnvironmentProviderState::ready ||
            providerSnapshot.extent != resources_.extent ||
            providerSnapshot.mipCount == 0 ||
            providerSnapshot.mipCount > kEnvironmentMaximumMipCount) {
            return false;
        }

        ComPtr<ID3D11Texture2D> radianceTexture;
        D3D11_SHADER_RESOURCE_VIEW_DESC radianceView{};
        if (!textureFromView(
                reflectionFreeRadiance,
                radianceTexture,
                radianceView,
                radianceDescription) ||
            radianceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            radianceView.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
            radianceDescription.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
            radianceDescription.ArraySize != 1 ||
            radianceDescription.SampleDesc.Count != 1 ||
            radianceDescription.Width < 2 || radianceDescription.Height == 0 ||
            (radianceDescription.Width & 1U) != 0) {
            return false;
        }

        ComPtr<ID3D11Texture2D> depthTexture;
        D3D11_SHADER_RESOURCE_VIEW_DESC depthView{};
        D3D11_TEXTURE2D_DESC depthDescription{};
        if (!textureFromView(
                sceneDepth,
                depthTexture,
                depthView,
                depthDescription) ||
            depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            depthView.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
            depthDescription.Format != DXGI_FORMAT_R24G8_TYPELESS ||
            depthDescription.Width != radianceDescription.Width ||
            depthDescription.Height != radianceDescription.Height ||
            depthDescription.ArraySize != 1 ||
            depthDescription.SampleDesc.Count != 1) {
            return false;
        }

        D3D11_BUFFER_DESC constantDescription{};
        sceneConstants->GetDesc(&constantDescription);
        return constantDescription.ByteWidth >=
                kMinimumSceneConstantRows * sizeof(std::array<float, 4>) &&
            (constantDescription.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
    }

    bool EnvironmentUpdater::dispatchDiagnostic(
        ID3D11DeviceContext* context,
        EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants) noexcept
    {
        D3D11_TEXTURE2D_DESC sourceDescription{};
        if (!validateInputs(
                context,
                provider,
                reflectionFreeRadiance,
                sceneDepth,
                sceneConstants,
                sourceDescription) ||
            !provider.beginUpdate()) {
            recordFailure();
            return false;
        }

        ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 2,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 1,
                .firstSampler = 0,
                .samplerCount = 1,
                .firstConstantBuffer = 11,
                .constantBufferCount = 2,
            });
        if (!restore.captured()) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }

        std::array<ID3D11ShaderResourceView*, 2> sources{
            reflectionFreeRadiance,
            sceneDepth,
        };
        std::array<ID3D11Buffer*, 2> constantBuffers{
            resources_.updateConstants.Get(),
            sceneConstants,
        };
        auto* sampler = resources_.sampler.Get();
        context->CSSetShader(resources_.shader.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(sources.size()),
            sources.data());
        context->CSSetSamplers(0, 1, &sampler);
        context->CSSetConstantBuffers(
            11,
            static_cast<UINT>(constantBuffers.size()),
            constantBuffers.data());

        const auto providerSnapshot = provider.snapshot();
        bool completed = true;
        for (std::uint32_t mipLevel = 0;
             mipLevel < providerSnapshot.mipCount;
             ++mipLevel) {
            const auto targetExtent = std::max(
                1U,
                providerSnapshot.extent >> mipLevel);
            const UpdateConstants constants{
                sourceDescription.Width,
                sourceDescription.Height,
                targetExtent,
                0,
            };
            context->UpdateSubresource(
                resources_.updateConstants.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);
            auto* destination = provider.writableMip(mipLevel);
            if (!destination) {
                completed = false;
                break;
            }
            context->CSSetUnorderedAccessViews(
                0,
                1,
                &destination,
                nullptr);
            context->Dispatch(
                (targetExtent + kThreadGroupExtent - 1) /
                    kThreadGroupExtent,
                (targetExtent + kThreadGroupExtent - 1) /
                    kThreadGroupExtent,
                kEnvironmentCubeFaceCount);
            for (std::uint32_t face = 0;
                 face < kEnvironmentCubeFaceCount;
                 ++face) {
                completed = provider.markSubresourceComplete(
                                static_cast<EnvironmentCubeFace>(face),
                                mipLevel) &&
                    completed;
            }
        }

        std::array<ID3D11ShaderResourceView*, 2> nullSources{};
        ID3D11UnorderedAccessView* nullDestination{};
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());
        context->CSSetUnorderedAccessViews(
            0,
            1,
            &nullDestination,
            nullptr);
        if (!completed || !restore.restore()) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }

        auto* writableTexture = provider.writableTexture();
        if (!writableTexture) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        for (UINT face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            context->CopySubresourceRegion(
                resources_.stagingMipZero.Get(),
                D3D11CalcSubresource(0, face, 1),
                0,
                0,
                0,
                writableTexture,
                D3D11CalcSubresource(
                    0,
                    face,
                    providerSnapshot.mipCount),
                nullptr);
        }
        context->End(resources_.completionEvent.Get());
        provider.abortUpdate();

        summary_.pending = true;
        summary_.generation = nextGeneration_++;
        ++summary_.dispatches;
        return true;
    }

    EnvironmentDiagnosticConsumeResult EnvironmentUpdater::consumeDiagnostic(
        ID3D11DeviceContext* context) noexcept
    {
        if (!summary_.pending) {
            return EnvironmentDiagnosticConsumeResult::idle;
        }
        if (!context || !resources_.device ||
            !sameDevice(context, resources_.device.Get())) {
            summary_.pending = false;
            recordFailure();
            return EnvironmentDiagnosticConsumeResult::failed;
        }

        BOOL complete{};
        const auto queryResult = context->GetData(
            resources_.completionEvent.Get(),
            &complete,
            sizeof(complete),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (queryResult == S_FALSE || complete != TRUE) {
            return EnvironmentDiagnosticConsumeResult::pending;
        }
        if (FAILED(queryResult)) {
            summary_.pending = false;
            recordFailure();
            return EnvironmentDiagnosticConsumeResult::failed;
        }

        double red{};
        double green{};
        double blue{};
        std::array<double, kEnvironmentCubeFaceCount> faceLuminance{};
        float peak{};
        std::uint32_t nonBlack{};
        const auto faceSampleCount = resources_.extent * resources_.extent;
        for (UINT face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(
                    resources_.stagingMipZero.Get(),
                    D3D11CalcSubresource(0, face, 1),
                    D3D11_MAP_READ,
                    0,
                    &mapped)) ||
                !mapped.pData ||
                mapped.RowPitch < resources_.extent * sizeof(std::uint32_t)) {
                if (mapped.pData) {
                    context->Unmap(
                        resources_.stagingMipZero.Get(),
                        D3D11CalcSubresource(0, face, 1));
                }
                summary_.pending = false;
                recordFailure();
                return EnvironmentDiagnosticConsumeResult::failed;
            }

            for (std::uint32_t y = 0; y < resources_.extent; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::byte*>(mapped.pData) +
                    static_cast<std::size_t>(y) * mapped.RowPitch);
                for (std::uint32_t x = 0; x < resources_.extent; ++x) {
                    const auto sample = decodeR11G11B10Float(row[x]);
                    red += sample.red;
                    green += sample.green;
                    blue += sample.blue;
                    const auto luminance = sample.red * 0.2126f +
                        sample.green * 0.7152f + sample.blue * 0.0722f;
                    faceLuminance[face] += luminance;
                    const auto samplePeak = std::max(
                        sample.red,
                        std::max(sample.green, sample.blue));
                    peak = std::max(peak, samplePeak);
                    if (samplePeak > kNonBlackThreshold) {
                        ++nonBlack;
                    }
                }
            }
            context->Unmap(
                resources_.stagingMipZero.Get(),
                D3D11CalcSubresource(0, face, 1));
        }

        const auto totalSamples = faceSampleCount * kEnvironmentCubeFaceCount;
        const auto inverseTotal = 1.0 / static_cast<double>(totalSamples);
        summary_.average = {
            static_cast<float>(red * inverseTotal),
            static_cast<float>(green * inverseTotal),
            static_cast<float>(blue * inverseTotal),
        };
        for (std::uint32_t face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            summary_.faceAverageLuminance[face] = static_cast<float>(
                faceLuminance[face] / static_cast<double>(faceSampleCount));
        }
        summary_.peak = peak;
        summary_.nonBlackSamples = nonBlack;
        summary_.sampleCount = totalSamples;
        summary_.pending = false;
        ++summary_.completedReadbacks;
        return EnvironmentDiagnosticConsumeResult::completed;
    }

    void EnvironmentUpdater::recordFailure() noexcept
    {
        ++summary_.failedUpdates;
    }
}
