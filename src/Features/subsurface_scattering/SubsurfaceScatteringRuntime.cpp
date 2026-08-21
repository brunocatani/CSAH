#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"

#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include "SubsurfaceScatteringCS.h"
#include "SubsurfaceTileClassifyCS.h"

#include <array>
#include <utility>

namespace community_shaders::subsurface_scattering
{
    namespace
    {
        constexpr UINT kAlbedoSlot = 0;
        constexpr UINT kDepthSlot = 3;

        struct alignas(16) GpuSettings
        {
            float width{};
            float height{};
            float directionX{};
            float directionY{};
            float radiusPixels{};
            float strength{};
            float depthRejection{};
            float skinCode{ 3.0f / 255.0f };
            float classTolerance{ 0.5f / 255.0f };
            float reserved0{};
            float reserved1{};
            float reserved2{};
        };
        static_assert(sizeof(GpuSettings) == 48);

        [[nodiscard]] bool supportedFormat(DXGI_FORMAT format) noexcept
        {
            return format == DXGI_FORMAT_R11G11B10_FLOAT ||
                format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                format == DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        classifyShader_.Reset();
        shader_.Reset();
        constants_.Reset();
        scratchA_.Reset();
        scratchB_.Reset();
        scratchAView_.Reset();
        scratchBView_.Reset();
        scratchAOutput_.Reset();
        scratchBOutput_.Reset();
        sourceResource_.Reset();
        sourceOutput_.Reset();
        activeTiles_.Reset();
        activeTilesView_.Reset();
        activeTilesOutput_.Reset();
        tileDispatchArguments_.Reset();
        tileDispatchArgumentsOutput_.Reset();
        sourceSubresource_ = 0;
        tileCapacity_ = 0;
        width_ = 0;
        height_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        firstExecutionLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto classifyResult = device->CreateComputeShader(
            fo4vr_cs_subsurface_tile_classify,
            sizeof(fo4vr_cs_subsurface_tile_classify),
            nullptr,
            classifyShader_.ReleaseAndGetAddressOf());
        const auto shaderResult = SUCCEEDED(classifyResult) ?
            device->CreateComputeShader(
            fo4vr_cs_subsurface_scattering,
            sizeof(fo4vr_cs_subsurface_scattering),
            nullptr,
            shader_.ReleaseAndGetAddressOf()) : E_FAIL;
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto bufferResult = SUCCEEDED(shaderResult) ?
            device->CreateBuffer(
                &description, nullptr, constants_.ReleaseAndGetAddressOf()) :
            E_FAIL;
        if (FAILED(classifyResult) || FAILED(shaderResult) ||
            FAILED(bufferResult) || !classifyShader_ || !shader_ ||
            !constants_) {
            classifyShader_.Reset();
            shader_.Reset();
            constants_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Subsurface Scattering GPU resource creation failed (classify=0x{:08X}, shader=0x{:08X}, constants=0x{:08X}).",
                static_cast<std::uint32_t>(classifyResult),
                static_cast<std::uint32_t>(shaderResult),
                static_cast<std::uint32_t>(bufferResult));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Subsurface Scattering skin-tile classification and indirect stereo compute pipeline is ready; execution remains class-mask-gated after exact directional DFLight.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        strength_.store(safe.strength, std::memory_order_relaxed);
        radiusPixels_.store(safe.radiusPixels, std::memory_order_relaxed);
        depthRejection_.store(safe.depthRejection, std::memory_order_relaxed);
        surface_classification::Runtime::get().setConsumerEnabled(
            surface_classification::Consumer::subsurfaceScattering,
            safe.enabled);
    }

    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::ensureScratch(
        ID3D11RenderTargetView* target,
        ID3D11Texture2D** source,
        UINT* sourceSubresource) noexcept
    {
        if (!device_ || !target || !source || !sourceSubresource) {
            return false;
        }
        *source = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        target->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture)) || !texture) {
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDescription{};
        D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
        texture->GetDesc(&sourceDescription);
        target->GetDesc(&viewDescription);
        UINT mipSlice{};
        UINT arraySlice{};
        if (viewDescription.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
            mipSlice = viewDescription.Texture2D.MipSlice;
        } else if (
            viewDescription.ViewDimension ==
                D3D11_RTV_DIMENSION_TEXTURE2DARRAY &&
            viewDescription.Texture2DArray.ArraySize == 1) {
            mipSlice = viewDescription.Texture2DArray.MipSlice;
            arraySlice = viewDescription.Texture2DArray.FirstArraySlice;
        } else {
            return false;
        }
        const auto width = sourceDescription.Width >> mipSlice;
        const auto height = sourceDescription.Height >> mipSlice;
        const auto format = viewDescription.Format == DXGI_FORMAT_UNKNOWN ?
            sourceDescription.Format : viewDescription.Format;
        if (width < 2 || height == 0 || (width & 1u) != 0 ||
            sourceDescription.SampleDesc.Count != 1 ||
            !supportedFormat(format)) {
            return false;
        }
        UINT support{};
        if (FAILED(device_->CheckFormatSupport(format, &support)) ||
            (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) == 0 ||
            (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0) {
            return false;
        }
        if (!scratchA_ || !scratchB_ || !scratchAView_ || !scratchBView_ ||
            !scratchAOutput_ || !scratchBOutput_ || width_ != width ||
            height_ != height || format_ != format) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> nextA;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> nextB;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextAView;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextBView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextAOutput;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextBOutput;
            auto result = device_->CreateTexture2D(
                &description, nullptr, nextA.GetAddressOf());
            result = SUCCEEDED(result) ? device_->CreateTexture2D(
                &description, nullptr, nextB.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateShaderResourceView(
                nextA.Get(), nullptr, nextAView.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateShaderResourceView(
                nextB.Get(), nullptr, nextBView.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateUnorderedAccessView(
                nextA.Get(), nullptr, nextAOutput.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateUnorderedAccessView(
                nextB.Get(), nullptr, nextBOutput.GetAddressOf()) : result;
            if (FAILED(result) || !nextA || !nextB || !nextAView ||
                !nextBView || !nextAOutput || !nextBOutput) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Subsurface Scattering scratch allocation failed for {}x{} format {} (HRESULT=0x{:08X}).",
                    width, height, static_cast<unsigned>(format),
                    static_cast<std::uint32_t>(result));
                return false;
            }
            scratchA_ = std::move(nextA);
            scratchB_ = std::move(nextB);
            scratchAView_ = std::move(nextAView);
            scratchBView_ = std::move(nextBView);
            scratchAOutput_ = std::move(nextAOutput);
            scratchBOutput_ = std::move(nextBOutput);
            width_ = width;
            height_ = height;
            format_ = format;
            resourceRebuilds_.fetch_add(1, std::memory_order_relaxed);
            logging::info(
                "Subsurface Scattering stereo scratch pair allocated at {}x{} format {}.",
                width_, height_, static_cast<unsigned>(format_));
        }
        const auto tileCapacity =
            ((width + 15u) / 16u) * ((height + 15u) / 16u);
        if (!activeTiles_ || !activeTilesView_ || !activeTilesOutput_ ||
            !tileDispatchArguments_ || !tileDispatchArgumentsOutput_ ||
            tileCapacity_ != tileCapacity) {
            D3D11_BUFFER_DESC tileDescription{};
            tileDescription.ByteWidth = tileCapacity * sizeof(UINT) * 2u;
            tileDescription.Usage = D3D11_USAGE_DEFAULT;
            tileDescription.BindFlags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            tileDescription.MiscFlags =
                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            tileDescription.StructureByteStride = sizeof(UINT) * 2u;
            Microsoft::WRL::ComPtr<ID3D11Buffer> nextTiles;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextTileView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextTileOutput;
            auto result = device_->CreateBuffer(
                &tileDescription,
                nullptr,
                nextTiles.GetAddressOf());
            D3D11_SHADER_RESOURCE_VIEW_DESC tileViewDescription{};
            tileViewDescription.Format = DXGI_FORMAT_UNKNOWN;
            tileViewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            tileViewDescription.Buffer.NumElements = tileCapacity;
            result = SUCCEEDED(result) ?
                device_->CreateShaderResourceView(
                    nextTiles.Get(),
                    &tileViewDescription,
                    nextTileView.GetAddressOf()) : result;
            D3D11_UNORDERED_ACCESS_VIEW_DESC tileOutputDescription{};
            tileOutputDescription.Format = DXGI_FORMAT_UNKNOWN;
            tileOutputDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            tileOutputDescription.Buffer.NumElements = tileCapacity;
            result = SUCCEEDED(result) ?
                device_->CreateUnorderedAccessView(
                    nextTiles.Get(),
                    &tileOutputDescription,
                    nextTileOutput.GetAddressOf()) : result;

            D3D11_BUFFER_DESC argumentDescription{};
            argumentDescription.ByteWidth = sizeof(UINT) * 3u;
            argumentDescription.Usage = D3D11_USAGE_DEFAULT;
            argumentDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            argumentDescription.MiscFlags =
                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS |
                D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            Microsoft::WRL::ComPtr<ID3D11Buffer> nextArguments;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
                nextArgumentOutput;
            result = SUCCEEDED(result) ? device_->CreateBuffer(
                &argumentDescription,
                nullptr,
                nextArguments.GetAddressOf()) : result;
            D3D11_UNORDERED_ACCESS_VIEW_DESC argumentOutputDescription{};
            argumentOutputDescription.Format = DXGI_FORMAT_R32_TYPELESS;
            argumentOutputDescription.ViewDimension =
                D3D11_UAV_DIMENSION_BUFFER;
            argumentOutputDescription.Buffer.NumElements = 3;
            argumentOutputDescription.Buffer.Flags =
                D3D11_BUFFER_UAV_FLAG_RAW;
            result = SUCCEEDED(result) ?
                device_->CreateUnorderedAccessView(
                    nextArguments.Get(),
                    &argumentOutputDescription,
                    nextArgumentOutput.GetAddressOf()) : result;
            if (FAILED(result) || !nextTiles || !nextTileView ||
                !nextTileOutput || !nextArguments ||
                !nextArgumentOutput) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Subsurface Scattering skin-tile infrastructure allocation failed for {} tiles (HRESULT=0x{:08X}).",
                    tileCapacity,
                    static_cast<std::uint32_t>(result));
                return false;
            }
            activeTiles_ = std::move(nextTiles);
            activeTilesView_ = std::move(nextTileView);
            activeTilesOutput_ = std::move(nextTileOutput);
            tileDispatchArguments_ = std::move(nextArguments);
            tileDispatchArgumentsOutput_ = std::move(nextArgumentOutput);
            tileCapacity_ = tileCapacity;
            logging::info(
                "Subsurface Scattering allocated a fixed {}-tile indirect skin worklist.",
                tileCapacity_);
        }
        const auto calculatedSubresource = D3D11CalcSubresource(
            mipSlice, arraySlice, sourceDescription.MipLevels);
        if (!sourceOutput_ || sourceResource_.Get() != resource.Get() ||
            sourceSubresource_ != calculatedSubresource) {
            sourceOutput_.Reset();
            sourceResource_.Reset();
            if ((sourceDescription.BindFlags &
                    D3D11_BIND_UNORDERED_ACCESS) != 0) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC outputDescription{};
                outputDescription.Format = format;
                if (viewDescription.ViewDimension ==
                    D3D11_RTV_DIMENSION_TEXTURE2D) {
                    outputDescription.ViewDimension =
                        D3D11_UAV_DIMENSION_TEXTURE2D;
                    outputDescription.Texture2D.MipSlice = mipSlice;
                } else {
                    outputDescription.ViewDimension =
                        D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                    outputDescription.Texture2DArray.MipSlice = mipSlice;
                    outputDescription.Texture2DArray.FirstArraySlice =
                        arraySlice;
                    outputDescription.Texture2DArray.ArraySize = 1;
                }
                if (SUCCEEDED(device_->CreateUnorderedAccessView(
                        texture.Get(),
                        &outputDescription,
                        sourceOutput_.ReleaseAndGetAddressOf())) &&
                    sourceOutput_) {
                    sourceResource_ = resource;
                    sourceSubresource_ = calculatedSubresource;
                    logging::info(
                        "Subsurface Scattering will write its vertical pass directly to the directional-light target; the full-frame copy-back is bypassed.");
                }
            }
        }
        *sourceSubresource = calculatedSubresource;
        *source = texture.Detach();
        return true;
    }

    bool Runtime::updateConstants(
        ID3D11DeviceContext* context,
        float directionX,
        float directionY) noexcept
    {
        if (!context || !constants_ || width_ == 0 || height_ == 0) {
            return false;
        }
        const GpuSettings settings{
            .width = static_cast<float>(width_),
            .height = static_cast<float>(height_),
            .directionX = directionX,
            .directionY = directionY,
            .radiusPixels = radiusPixels_.load(std::memory_order_relaxed),
            .strength = strength_.load(std::memory_order_relaxed),
            .depthRejection = depthRejection_.load(std::memory_order_relaxed),
        };
        context->UpdateSubresource(
            constants_.Get(), 0, nullptr, &settings, 0, 0);
        return true;
    }

    bool Runtime::executeAfterDirectionalLight(
        ID3D11DeviceContext* context) noexcept
    {
        if (!requested() || !context || context != context_.Get() ||
            !classifyShader_ || !shader_ || !constants_) {
            return false;
        }
        auto* surfaceClass =
            surface_classification::Runtime::get().shaderResourceView();
        if (!surfaceClass) {
            return false;
        }
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
        std::array<ID3D11RenderTargetView*,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencil;
        context->OMGetRenderTargets(
            static_cast<UINT>(rawTargets.size()), rawTargets.data(),
            depthStencil.GetAddressOf());
        UINT targetCount{};
        for (std::size_t index = 0; index < rawTargets.size(); ++index) {
            targets[index].Attach(rawTargets[index]);
            if (rawTargets[index]) {
                targetCount = static_cast<UINT>(index + 1);
            }
        }
        if (!targets[0]) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        UINT sourceSubresource{};
        ID3D11Texture2D* sourceRaw{};
        if (!ensureScratch(
                targets[0].Get(), &sourceRaw, &sourceSubresource) ||
            !sourceRaw || !activeTiles_ || !activeTilesView_ ||
            !activeTilesOutput_ || !tileDispatchArguments_ ||
            !tileDispatchArgumentsOutput_) {
            return false;
        }
        source.Attach(sourceRaw);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> albedo;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth;
        context->PSGetShaderResources(kAlbedoSlot, 1, albedo.GetAddressOf());
        context->PSGetShaderResources(kDepthSlot, 1, depth.GetAddressOf());
        if (!albedo || !depth) {
            return false;
        }
        D3D11_BOX sourceBox{ 0, 0, 0, width_, height_, 1 };
        context->CopySubresourceRegion(
            scratchA_.Get(), 0, 0, 0, 0, source.Get(), sourceSubresource,
            &sourceBox);
        context->OMSetRenderTargets(0, nullptr, nullptr);

        constexpr std::array<UINT, 3> initialDispatchArguments{ 0, 1, 1 };
        context->UpdateSubresource(
            tileDispatchArguments_.Get(),
            0,
            nullptr,
            initialDispatchArguments.data(),
            0,
            0);

        render::ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 5,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 2,
                .firstConstantBuffer = 0,
                .constantBufferCount = 1,
            });
        if (!restore.captured()) {
            context->OMSetRenderTargets(
                targetCount, rawTargets.data(), depthStencil.Get());
            return false;
        }
        if (!updateConstants(context, 0.0f, 0.0f)) {
            (void)restore.restore();
            context->OMSetRenderTargets(
                targetCount, rawTargets.data(), depthStencil.Get());
            return false;
        }
        auto* classifyInput = surfaceClass;
        const std::array<ID3D11UnorderedAccessView*, 2> classifyOutputs{
            activeTilesOutput_.Get(),
            tileDispatchArgumentsOutput_.Get(),
        };
        auto* constants = constants_.Get();
        context->CSSetShader(classifyShader_.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 1, &classifyInput);
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(classifyOutputs.size()),
            classifyOutputs.data(),
            nullptr);
        context->CSSetConstantBuffers(0, 1, &constants);
        context->Dispatch(
            (width_ + 15u) / 16u,
            (height_ + 15u) / 16u,
            1);
        const std::array<ID3D11UnorderedAccessView*, 2> noClassifyOutputs{};
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(noClassifyOutputs.size()),
            noClassifyOutputs.data(),
            nullptr);
        auto dispatch = [this, context, albedo = albedo.Get(),
                            depth = depth.Get(), surfaceClass](
                            ID3D11ShaderResourceView* input,
                            ID3D11UnorderedAccessView* output,
                            float directionX,
                            float directionY) noexcept {
            if (!updateConstants(context, directionX, directionY)) {
                return false;
            }
            std::array<ID3D11ShaderResourceView*, 5> inputs{
                input, depth, surfaceClass, albedo, activeTilesView_.Get(),
            };
            auto* constants = constants_.Get();
            context->CSSetShader(shader_.Get(), nullptr, 0);
            context->CSSetShaderResources(
                0, static_cast<UINT>(inputs.size()), inputs.data());
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->CSSetConstantBuffers(0, 1, &constants);
            context->DispatchIndirect(tileDispatchArguments_.Get(), 0);
            std::array<ID3D11ShaderResourceView*, 5> nullInputs{};
            ID3D11UnorderedAccessView* nullOutput{};
            context->CSSetShaderResources(
                0, static_cast<UINT>(nullInputs.size()), nullInputs.data());
            context->CSSetUnorderedAccessViews(
                0, 1, &nullOutput, nullptr);
            dispatches_.fetch_add(1, std::memory_order_relaxed);
            return true;
        };
        const auto horizontal = dispatch(
            scratchAView_.Get(), scratchBOutput_.Get(), 1.0f, 0.0f);
        auto* verticalOutput = sourceOutput_ ?
            sourceOutput_.Get() : scratchAOutput_.Get();
        const auto vertical = horizontal && dispatch(
            scratchBView_.Get(), verticalOutput, 0.0f, 1.0f);
        const auto restored = restore.restore();
        if (vertical && restored && !sourceOutput_) {
            context->CopySubresourceRegion(
                source.Get(),
                sourceSubresource,
                0,
                0,
                0,
                scratchA_.Get(),
                0,
                nullptr);
        }
        context->OMSetRenderTargets(
            targetCount, rawTargets.data(), depthStencil.Get());
        if (!vertical || !restored) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        executions_.fetch_add(1, std::memory_order_relaxed);
        if (!firstExecutionLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::info(
                "Subsurface Scattering completed its first class-mask-gated, albedo-preserving stereo pass (class=3, strength={:.3f}, reference radius={:.3f}); producer coverage is reported separately.",
                strength_.load(std::memory_order_relaxed),
                radiusPixels_.load(std::memory_order_relaxed));
        }
        return true;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .strength = strength_.load(std::memory_order_relaxed),
                .radiusPixels = radiusPixels_.load(std::memory_order_relaxed),
                .depthRejection = depthRejection_.load(
                    std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .executions = executions_.load(std::memory_order_relaxed),
            .dispatches = dispatches_.load(std::memory_order_relaxed),
            .resourceRebuilds = resourceRebuilds_.load(
                std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
