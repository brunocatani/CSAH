#include "Features/ibl/IblEnvironmentUpdater.h"

#include "Features/ibl/IblComputeStateScope.h"
#include "Features/ibl/IblSceneRadianceProbeModel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace community_shaders::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::uint32_t kMinimumSceneConstantRows = 71;
        constexpr UINT kThreadGroupExtent = 8;
        constexpr float kNonBlackThreshold = 1.0e-5f;
        constexpr float kCoveredThreshold = 1.0e-4f;
        constexpr float kHistoryDecay = 0.98F;
        constexpr float kVisibleDirectionHistoryBlend = 0.8F;

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

        [[nodiscard]] bool createConstantBuffer(
            ID3D11Device* device,
            UINT byteWidth,
            ComPtr<ID3D11Buffer>& buffer) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = byteWidth;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            return SUCCEEDED(device->CreateBuffer(
                &description,
                nullptr,
                &buffer));
        }

        [[nodiscard]] bool createCapturedCube(
            ID3D11Device* device,
            std::uint32_t extent,
            DXGI_FORMAT format,
            ComPtr<ID3D11Texture2D>& texture,
            ComPtr<ID3D11ShaderResourceView>& shaderResource,
            ComPtr<ID3D11UnorderedAccessView>& unorderedAccess) noexcept
        {
            D3D11_TEXTURE2D_DESC textureDescription{};
            textureDescription.Width = extent;
            textureDescription.Height = extent;
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = kEnvironmentCubeFaceCount;
            textureDescription.Format = format;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            textureDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            if (FAILED(device->CreateTexture2D(
                    &textureDescription,
                    nullptr,
                    &texture))) {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription{};
            shaderResourceDescription.Format = format;
            shaderResourceDescription.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURECUBE;
            shaderResourceDescription.TextureCube.MostDetailedMip = 0;
            shaderResourceDescription.TextureCube.MipLevels = 1;
            if (FAILED(device->CreateShaderResourceView(
                    texture.Get(),
                    &shaderResourceDescription,
                    &shaderResource))) {
                return false;
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC unorderedAccessDescription{};
            unorderedAccessDescription.Format = format;
            unorderedAccessDescription.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            unorderedAccessDescription.Texture2DArray.MipSlice = 0;
            unorderedAccessDescription.Texture2DArray.FirstArraySlice = 0;
            unorderedAccessDescription.Texture2DArray.ArraySize =
                kEnvironmentCubeFaceCount;
            return SUCCEEDED(device->CreateUnorderedAccessView(
                texture.Get(),
                &unorderedAccessDescription,
                &unorderedAccess));
        }

        [[nodiscard]] bool createStagingCube(
            ID3D11Device* device,
            std::uint32_t extent,
            DXGI_FORMAT format,
            ComPtr<ID3D11Texture2D>& texture) noexcept
        {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = extent;
            description.Height = extent;
            description.MipLevels = 1;
            description.ArraySize = kEnvironmentCubeFaceCount;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_STAGING;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            return SUCCEEDED(device->CreateTexture2D(
                &description,
                nullptr,
                &texture));
        }
    }

    bool EnvironmentUpdater::initialize(
        ID3D11Device* device,
        const void* captureShaderBytecode,
        std::size_t captureShaderBytecodeSize,
        const void* filterShaderBytecode,
        std::size_t filterShaderBytecodeSize,
        std::uint32_t extent) noexcept
    {
        if (!device || !captureShaderBytecode ||
            captureShaderBytecodeSize < 20 ||
            std::memcmp(captureShaderBytecode, "DXBC", 4) != 0 ||
            !filterShaderBytecode || filterShaderBytecodeSize < 20 ||
            std::memcmp(filterShaderBytecode, "DXBC", 4) != 0 ||
            extent < 16 || extent > 512 || !std::has_single_bit(extent)) {
            recordFailure();
            return false;
        }

        Resources candidate{};
        candidate.device = device;
        candidate.extent = extent;
        if (FAILED(device->CreateComputeShader(
                captureShaderBytecode,
                captureShaderBytecodeSize,
                nullptr,
                &candidate.captureShader)) ||
            FAILED(device->CreateComputeShader(
                filterShaderBytecode,
                filterShaderBytecodeSize,
                nullptr,
                &candidate.filterShader))) {
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
                &candidate.sampler)) ||
            !createConstantBuffer(
                device,
                sizeof(UpdateConstants),
                candidate.captureConstants) ||
            !createConstantBuffer(
                device,
                sizeof(FilterConstants),
                candidate.filterConstants) ||
            !createCapturedCube(
                device,
                extent,
                EnvironmentProvider::kFormat,
                candidate.capturedRadiance,
                candidate.capturedRadianceView,
                candidate.capturedRadianceOutput) ||
            !createCapturedCube(
                device,
                extent,
                EnvironmentProvider::kValidityFormat,
                candidate.capturedValidity,
                candidate.capturedValidityView,
                candidate.capturedValidityOutput) ||
            !createStagingCube(
                device,
                extent,
                EnvironmentProvider::kFormat,
                candidate.stagingRadiance) ||
            !createStagingCube(
                device,
                extent,
                EnvironmentProvider::kValidityFormat,
                candidate.stagingValidity)) {
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
        return true;
    }

    void EnvironmentUpdater::reset() noexcept
    {
        resources_ = {};
        summary_ = {};
    }

    bool EnvironmentUpdater::validateInputs(
        ID3D11DeviceContext* context,
        const EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants,
        bool usePublishedHistory,
        D3D11_TEXTURE2D_DESC& radianceDescription) const noexcept
    {
        if (!summary_.initialized || summary_.pending || !context ||
            !resources_.device || !sameDevice(context, resources_.device.Get()) ||
            !sameDevice(reflectionFreeRadiance, resources_.device.Get()) ||
            !sameDevice(sceneDepth, resources_.device.Get()) ||
            !sameDevice(sceneConstants, resources_.device.Get())) {
            return false;
        }

        if (usePublishedHistory &&
            (!sameDevice(
                 provider.publishedEnvironment(),
                 resources_.device.Get()) ||
                !sameDevice(
                    provider.publishedValidity(),
                    resources_.device.Get()))) {
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

    bool EnvironmentUpdater::dispatchUpdate(
        ID3D11DeviceContext* context,
        EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants,
        bool usePublishedHistory) noexcept
    {
        D3D11_TEXTURE2D_DESC sourceDescription{};
        if (!validateInputs(
                context,
                provider,
                reflectionFreeRadiance,
                sceneDepth,
                sceneConstants,
                usePublishedHistory,
                sourceDescription) ||
            !provider.beginUpdate()) {
            recordFailure();
            return false;
        }

        ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 4,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 2,
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

        std::array<ID3D11ShaderResourceView*, 4> captureSources{
            reflectionFreeRadiance,
            sceneDepth,
            usePublishedHistory ? provider.publishedEnvironment() : nullptr,
            usePublishedHistory ? provider.publishedValidity() : nullptr,
        };
        std::array<ID3D11UnorderedAccessView*, 2> captureDestinations{
            resources_.capturedRadianceOutput.Get(),
            resources_.capturedValidityOutput.Get(),
        };
        std::array<ID3D11Buffer*, 2> captureConstants{
            resources_.captureConstants.Get(),
            sceneConstants,
        };
        const UpdateConstants updateConstants{
            sourceDescription.Width,
            sourceDescription.Height,
            resources_.extent,
            usePublishedHistory ? 1U : 0U,
            kHistoryDecay,
            kVisibleDirectionHistoryBlend,
            {},
        };
        context->UpdateSubresource(
            resources_.captureConstants.Get(),
            0,
            nullptr,
            &updateConstants,
            0,
            0);
        auto* sampler = resources_.sampler.Get();
        context->CSSetShader(resources_.captureShader.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(captureSources.size()),
            captureSources.data());
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(captureDestinations.size()),
            captureDestinations.data(),
            nullptr);
        context->CSSetSamplers(0, 1, &sampler);
        context->CSSetConstantBuffers(
            11,
            static_cast<UINT>(captureConstants.size()),
            captureConstants.data());
        context->Dispatch(
            (resources_.extent + kThreadGroupExtent - 1) /
                kThreadGroupExtent,
            (resources_.extent + kThreadGroupExtent - 1) /
                kThreadGroupExtent,
            kEnvironmentCubeFaceCount);

        std::array<ID3D11UnorderedAccessView*, 2> nullDestinations{};
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(nullDestinations.size()),
            nullDestinations.data(),
            nullptr);
        std::array<ID3D11ShaderResourceView*, 4> nullSources{};
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());

        std::array<ID3D11ShaderResourceView*, 2> filterSources{
            resources_.capturedRadianceView.Get(),
            resources_.capturedValidityView.Get(),
        };
        context->CSSetShader(resources_.filterShader.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(filterSources.size()),
            filterSources.data());
        auto* filterConstants = resources_.filterConstants.Get();
        context->CSSetConstantBuffers(11, 1, &filterConstants);

        const auto providerSnapshot = provider.snapshot();
        bool completed = true;
        for (std::uint32_t mipLevel = 0;
             mipLevel < providerSnapshot.mipCount;
             ++mipLevel) {
            const auto targetExtent = std::max(
                1U,
                providerSnapshot.extent >> mipLevel);
            const FilterConstants constants{
                targetExtent,
                mipLevel,
                providerSnapshot.mipCount,
                providerSnapshot.mipCount > 1 ?
                    static_cast<float>(mipLevel) /
                        static_cast<float>(providerSnapshot.mipCount - 1) :
                    0.0f,
            };
            context->UpdateSubresource(
                resources_.filterConstants.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);
            std::array<ID3D11UnorderedAccessView*, 2> destinations{
                provider.writableMip(mipLevel),
                provider.writableValidityMip(mipLevel),
            };
            if (!destinations[0] || !destinations[1]) {
                completed = false;
                break;
            }
            context->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(destinations.size()),
                destinations.data(),
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

        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(nullDestinations.size()),
            nullDestinations.data(),
            nullptr);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());
        if (!completed || !restore.restore()) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }

        auto* writableRadiance = provider.writableTexture();
        auto* writableValidity = provider.writableValidityTexture();
        if (!writableRadiance || !writableValidity) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        for (UINT face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            const auto sourceSubresource = D3D11CalcSubresource(
                0,
                face,
                providerSnapshot.mipCount);
            const auto stagingSubresource = D3D11CalcSubresource(0, face, 1);
            context->CopySubresourceRegion(
                resources_.stagingRadiance.Get(),
                stagingSubresource,
                0,
                0,
                0,
                writableRadiance,
                sourceSubresource,
                nullptr);
            context->CopySubresourceRegion(
                resources_.stagingValidity.Get(),
                stagingSubresource,
                0,
                0,
                0,
                writableValidity,
                sourceSubresource,
                nullptr);
        }
        context->End(resources_.completionEvent.Get());

        summary_.pending = true;
        summary_.historyUsed = usePublishedHistory;
        summary_.generation = provider.snapshot().activeUpdateGeneration;
        ++summary_.dispatches;
        return true;
    }

    EnvironmentUpdateConsumeResult EnvironmentUpdater::consumeUpdate(
        ID3D11DeviceContext* context,
        EnvironmentProvider& provider) noexcept
    {
        if (!summary_.pending) {
            return EnvironmentUpdateConsumeResult::idle;
        }
        const auto providerSnapshot = provider.snapshot();
        if (!context || !resources_.device ||
            !sameDevice(context, resources_.device.Get()) ||
            providerSnapshot.state != EnvironmentProviderState::updating ||
            providerSnapshot.activeUpdateGeneration != summary_.generation) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }

        BOOL complete{};
        const auto queryResult = context->GetData(
            resources_.completionEvent.Get(),
            &complete,
            sizeof(complete),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (queryResult == S_FALSE || complete != TRUE) {
            return EnvironmentUpdateConsumeResult::pending;
        }
        if (FAILED(queryResult)) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }

        double red{};
        double green{};
        double blue{};
        double validityTotal{};
        std::array<double, kEnvironmentCubeFaceCount> faceLuminance{};
        std::array<double, kEnvironmentCubeFaceCount> faceValidity{};
        std::array<double, kEnvironmentCubeFaceCount> faceSolidAngle{};
        float peak{};
        std::uint32_t nonBlack{};
        std::uint32_t covered{};
        DiffuseSHFit diffuseFit{};
        bool validGeneration = true;
        const auto faceSampleCount = resources_.extent * resources_.extent;
        for (UINT face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            const auto subresource = D3D11CalcSubresource(0, face, 1);
            D3D11_MAPPED_SUBRESOURCE mappedRadiance{};
            D3D11_MAPPED_SUBRESOURCE mappedValidity{};
            if (FAILED(context->Map(
                    resources_.stagingRadiance.Get(),
                    subresource,
                    D3D11_MAP_READ,
                    0,
                    &mappedRadiance)) ||
                !mappedRadiance.pData ||
                mappedRadiance.RowPitch <
                    resources_.extent * sizeof(std::uint32_t)) {
                if (mappedRadiance.pData) {
                    context->Unmap(
                        resources_.stagingRadiance.Get(),
                        subresource);
                }
                validGeneration = false;
                break;
            }
            if (FAILED(context->Map(
                    resources_.stagingValidity.Get(),
                    subresource,
                    D3D11_MAP_READ,
                    0,
                    &mappedValidity)) ||
                !mappedValidity.pData ||
                mappedValidity.RowPitch <
                    resources_.extent * sizeof(float)) {
                context->Unmap(
                    resources_.stagingRadiance.Get(),
                    subresource);
                if (mappedValidity.pData) {
                    context->Unmap(
                        resources_.stagingValidity.Get(),
                        subresource);
                }
                validGeneration = false;
                break;
            }

            for (std::uint32_t y = 0; y < resources_.extent; ++y) {
                const auto* radianceRow =
                    reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::byte*>(mappedRadiance.pData) +
                        static_cast<std::size_t>(y) *
                            mappedRadiance.RowPitch);
                const auto* validityRow = reinterpret_cast<const float*>(
                    static_cast<const std::byte*>(mappedValidity.pData) +
                    static_cast<std::size_t>(y) * mappedValidity.RowPitch);
                for (std::uint32_t x = 0; x < resources_.extent; ++x) {
                    const auto validity = validityRow[x];
                    const auto sample = decodeR11G11B10Float(radianceRow[x]);
                    if (!std::isfinite(validity) || validity < 0.0f ||
                        validity > 1.0001f || !std::isfinite(sample.red) ||
                        !std::isfinite(sample.green) ||
                        !std::isfinite(sample.blue) || sample.red < 0.0f ||
                        sample.green < 0.0f || sample.blue < 0.0f) {
                        validGeneration = false;
                        continue;
                    }
                    red += sample.red;
                    green += sample.green;
                    blue += sample.blue;
                    validityTotal += validity;
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
                    if (validity > kCoveredThreshold) {
                        ++covered;
                    }
                    const auto horizontal =
                        ((static_cast<float>(x) + 0.5f) /
                            static_cast<float>(resources_.extent)) *
                            2.0f -
                        1.0f;
                    const auto vertical =
                        ((static_cast<float>(y) + 0.5f) /
                            static_cast<float>(resources_.extent)) *
                            2.0f -
                        1.0f;
                    const auto solidAngleWeight =
                        cubeTexelSolidAngleWeight(horizontal, vertical);
                    faceValidity[face] +=
                        static_cast<double>(validity) * solidAngleWeight;
                    faceSolidAngle[face] += solidAngleWeight;
                    accumulateDiffuseSHFit(
                        diffuseFit,
                        environmentCubeDirection(
                            static_cast<EnvironmentCubeFace>(face),
                            horizontal,
                            vertical),
                        { sample.red, sample.green, sample.blue },
                        validity,
                        solidAngleWeight);
                }
            }
            context->Unmap(resources_.stagingValidity.Get(), subresource);
            context->Unmap(resources_.stagingRadiance.Get(), subresource);
        }

        const auto totalSamples = faceSampleCount * kEnvironmentCubeFaceCount;
        validGeneration = validGeneration && covered > 0;
        if (!validGeneration || !provider.publishUpdate()) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }

        const auto inverseTotal = 1.0 / static_cast<double>(totalSamples);
        summary_.average = {
            static_cast<float>(red * inverseTotal),
            static_cast<float>(green * inverseTotal),
            static_cast<float>(blue * inverseTotal),
        };
        for (std::uint32_t face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            summary_.faceAverageLuminance[face] = static_cast<float>(
                faceLuminance[face] / static_cast<double>(faceSampleCount));
            summary_.faceAverageValidity[face] = faceSolidAngle[face] > 0.0 ?
                static_cast<float>(
                    faceValidity[face] / faceSolidAngle[face]) :
                0.0f;
        }
        summary_.peak = peak;
        summary_.averageValidity = static_cast<float>(
            validityTotal * inverseTotal);
        summary_.nonBlackSamples = nonBlack;
        summary_.coveredSamples = covered;
        summary_.sampleCount = totalSamples;
        summary_.diffuseSHCoverage = diffuseSHFitCoverage(diffuseFit);
        summary_.diffuseSH = {};
        summary_.diffuseSHState = solveDiffuseSHFit(
                                      diffuseFit,
                                      summary_.diffuseSH) ?
            classifyDiffuseSH(summary_.diffuseSH) :
            DiffuseSHState::invalid;
        summary_.pending = false;
        ++summary_.completedReadbacks;
        ++summary_.publishedUpdates;
        return EnvironmentUpdateConsumeResult::completed;
    }

    void EnvironmentUpdater::recordFailure() noexcept
    {
        ++summary_.failedUpdates;
    }
}
