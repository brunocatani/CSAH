#include "Features/ibl/IblRuntime.h"

#include "resources.h"
#include "support/Logger.h"

#include <Windows.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>

namespace community_shaders::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        // Fallout4VR.exe 1.2.72 raw-disassembly witnesses:
        // - 0x1428A4A60 registers the only cubemap target (index 0), and
        //   0x141D99C00 stores its six-face texture/RTVs/SRV at renderer
        //   +0x3048, with the SRV at +0x3080.
        // - 0x14289EF1F requests cubemap index 0 for the native PS t3 bind.
        // The renderer object begins at VA 0x146239340, making the SRV slot
        // VA 0x14623C3C0 / RVA 0x0623C3C0. The exact executable gate is owned
        // by XSEPlugin.cpp; this subsystem additionally validates the live
        // interface and texture description before any retained use.
        constexpr std::uintptr_t kNativeCubemapSrvRva = 0x0623C3C0;
        constexpr UINT kNativeCubemapExtent = 512;
        constexpr std::uint64_t kProjectionCadenceMilliseconds = 250;
        constexpr std::uint32_t kBlackReadbackWarningThreshold = 8;
        constexpr UINT kCaptureShaderResourceCount = 16;
        constexpr UINT kCaptureRenderTargetCount =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

        struct TextureViewRange
        {
            UINT firstMip{};
            UINT mipCount{};
            UINT firstSlice{};
            UINT sliceCount{};
        };

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_SHADER_RESOURCE_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_SRV_DIMENSION_TEXTURE2D:
                return {
                    description.Texture2D.MostDetailedMip,
                    description.Texture2D.MipLevels,
                    0,
                    1,
                };
            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MostDetailedMip,
                    description.Texture2DArray.MipLevels,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_SRV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            case D3D11_SRV_DIMENSION_TEXTURECUBE:
                return {
                    description.TextureCube.MostDetailedMip,
                    description.TextureCube.MipLevels,
                    0,
                    6,
                };
            case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
                return {
                    description.TextureCubeArray.MostDetailedMip,
                    description.TextureCubeArray.MipLevels,
                    description.TextureCubeArray.First2DArrayFace,
                    description.TextureCubeArray.NumCubes * 6,
                };
            default:
                return {};
            }
        }

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_RENDER_TARGET_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_RTV_DIMENSION_TEXTURE2D:
                return { description.Texture2D.MipSlice, 1, 0, 1 };
            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MipSlice,
                    1,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_RTV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            default:
                return {};
            }
        }

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_DEPTH_STENCIL_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_DSV_DIMENSION_TEXTURE2D:
                return { description.Texture2D.MipSlice, 1, 0, 1 };
            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MipSlice,
                    1,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_DSV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            default:
                return {};
            }
        }

        void logCaptureView(
            std::uint16_t contractPlusOne,
            std::uint32_t checksumPrefix,
            const char* binding,
            UINT slot,
            ID3D11View* view,
            DXGI_FORMAT viewFormat,
            UINT viewDimension,
            TextureViewRange range) noexcept
        {
            if (!view) {
                return;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource) {
                logging::info(
                    "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: viewFormat={}, viewDimension={}, resource unavailable.",
                    contractPlusOne,
                    checksumPrefix,
                    binding,
                    slot,
                    static_cast<unsigned>(viewFormat),
                    viewDimension);
                return;
            }

            D3D11_RESOURCE_DIMENSION resourceDimension{};
            resource->GetType(&resourceDimension);
            ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED(resource.As(&texture)) && texture) {
                D3D11_TEXTURE2D_DESC textureDescription{};
                texture->GetDesc(&textureDescription);
                logging::info(
                    "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: resource=0x{:X}, viewFormat={}, viewDimension={}, mip={}+{}, slice={}+{}, texture={}x{}, format={}, mips={}, array={}, samples={}, bind=0x{:X}, misc=0x{:X}.",
                    contractPlusOne,
                    checksumPrefix,
                    binding,
                    slot,
                    reinterpret_cast<std::uintptr_t>(resource.Get()),
                    static_cast<unsigned>(viewFormat),
                    viewDimension,
                    range.firstMip,
                    range.mipCount,
                    range.firstSlice,
                    range.sliceCount,
                    textureDescription.Width,
                    textureDescription.Height,
                    static_cast<unsigned>(textureDescription.Format),
                    textureDescription.MipLevels,
                    textureDescription.ArraySize,
                    textureDescription.SampleDesc.Count,
                    textureDescription.BindFlags,
                    textureDescription.MiscFlags);
                return;
            }

            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: resource=0x{:X}, viewFormat={}, viewDimension={}, resourceDimension={}.",
                contractPlusOne,
                checksumPrefix,
                binding,
                slot,
                reinterpret_cast<std::uintptr_t>(resource.Get()),
                static_cast<unsigned>(viewFormat),
                viewDimension,
                static_cast<unsigned>(resourceDimension));
        }

        struct EmbeddedShader
        {
            const void* data{};
            std::size_t size{};
        };

        [[nodiscard]] EmbeddedShader loadEmbeddedShader(
            int resourceId) noexcept
        {
            HMODULE module{};
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&loadEmbeddedShader),
                    &module)) {
                return {};
            }
            const auto resource = FindResourceW(
                module,
                MAKEINTRESOURCEW(resourceId),
                RT_RCDATA);
            if (!resource) {
                return {};
            }
            const auto loaded = LoadResource(module, resource);
            if (!loaded) {
                return {};
            }
            const auto size = SizeofResource(module, resource);
            return { LockResource(loaded), static_cast<std::size_t>(size) };
        }

        [[nodiscard]] bool readableRange(
            const void* address,
            std::size_t size,
            const void* expectedAllocationBase = nullptr) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
                (expectedAllocationBase &&
                    information.AllocationBase != expectedAllocationBase)) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            const auto regionStart =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            if (start < regionStart ||
                size > std::numeric_limits<std::uintptr_t>::max() - start) {
                return false;
            }
            return start + size <= regionStart + information.RegionSize;
        }

        [[nodiscard]] bool executableAddress(const void* address) noexcept
        {
            MEMORY_BASIC_INFORMATION information{};
            if (!address ||
                VirtualQuery(address, &information, sizeof(information)) == 0 ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            constexpr DWORD kExecutableProtection =
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & kExecutableProtection) != 0;
        }

        [[nodiscard]] bool plausibleComInterface(const void* object) noexcept
        {
            if (!readableRange(object, sizeof(void*))) {
                return false;
            }
            const auto* vtable = *static_cast<void* const* const*>(object);
            return readableRange(vtable, sizeof(void*) * 3) &&
                executableAddress(vtable[0]) && executableAddress(vtable[1]) &&
                executableAddress(vtable[2]);
        }

        [[nodiscard]] bool validateNativeCubemap(
            ID3D11ShaderResourceView* candidate,
            D3D11_TEXTURE2D_DESC& textureDescription,
            D3D11_SHADER_RESOURCE_VIEW_DESC& viewDescription) noexcept
        {
            if (!candidate || !plausibleComInterface(candidate)) {
                return false;
            }
            candidate->GetDesc(&viewDescription);
            if (viewDescription.ViewDimension !=
                D3D11_SRV_DIMENSION_TEXTURECUBE) {
                return false;
            }

            ComPtr<ID3D11Resource> resource;
            candidate->GetResource(&resource);
            if (!resource) {
                return false;
            }
            D3D11_RESOURCE_DIMENSION dimension{};
            resource->GetType(&dimension);
            if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                return false;
            }
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDesc(&textureDescription);
            return textureDescription.Width == kNativeCubemapExtent &&
                textureDescription.Height == kNativeCubemapExtent &&
                textureDescription.ArraySize == 6 &&
                textureDescription.MipLevels >= 1 &&
                textureDescription.SampleDesc.Count == 1 &&
                (textureDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
                (textureDescription.MiscFlags &
                    D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
        }

        class ScopedComputeState final
        {
        public:
            explicit ScopedComputeState(ID3D11DeviceContext* context) noexcept :
                context_(context)
            {
                if (!context_) {
                    return;
                }
                classInstanceCount_ = static_cast<UINT>(classInstances_.size());
                context_->CSGetShader(
                    &shader_,
                    classInstances_.data(),
                    &classInstanceCount_);
                context_->CSGetShaderResources(0, 1, &shaderResource_);
                context_->CSGetUnorderedAccessViews(0, 1, &unorderedAccess_);
                context_->CSGetSamplers(0, 1, &sampler_);
            }

            ~ScopedComputeState()
            {
                if (!context_) {
                    return;
                }
                context_->CSSetShader(
                    shader_,
                    classInstances_.data(),
                    classInstanceCount_);
                context_->CSSetShaderResources(0, 1, &shaderResource_);
                context_->CSSetUnorderedAccessViews(
                    0,
                    1,
                    &unorderedAccess_,
                    nullptr);
                context_->CSSetSamplers(0, 1, &sampler_);
                if (sampler_) {
                    sampler_->Release();
                }
                if (unorderedAccess_) {
                    unorderedAccess_->Release();
                }
                if (shaderResource_) {
                    shaderResource_->Release();
                }
                for (UINT index = 0; index < classInstanceCount_; ++index) {
                    if (classInstances_[index]) {
                        classInstances_[index]->Release();
                    }
                }
                if (shader_) {
                    shader_->Release();
                }
            }

            ScopedComputeState(const ScopedComputeState&) = delete;
            ScopedComputeState& operator=(const ScopedComputeState&) = delete;

        private:
            ID3D11DeviceContext* context_{};
            ID3D11ComputeShader* shader_{};
            std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES>
                classInstances_{};
            UINT classInstanceCount_{};
            ID3D11ShaderResourceView* shaderResource_{};
            ID3D11UnorderedAccessView* unorderedAccess_{};
            ID3D11SamplerState* sampler_{};
        };
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* immediateContext) noexcept
    {
        resetResources();
        if (!device || !immediateContext) {
            logging::error(
                "IBL projection initialization rejected a null D3D11 device/context.");
            return;
        }
        device_ = device;
        context_ = immediateContext;
        if (!createResources()) {
            logging::error(
                "IBL projection resources could not be created; the subsystem remains fail-closed and has no visual effect.");
            resetResources();
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "IBL projection foundation initialized in observe-only mode; native lighting remains unchanged.");
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        std::size_t bytecodeSize,
        ID3D11PixelShader* shader) noexcept
    {
        const auto contractPlusOne = matchCaptureProbeContract(
            bytecode,
            bytecodeSize);
        if (!contractPlusOne || !shader) {
            return;
        }

        try {
            const std::scoped_lock lock(captureShaderMutex_);
            const auto firstSlot =
                (reinterpret_cast<std::uintptr_t>(shader) >> 4) %
                captureShaderSlots_.size();
            for (std::size_t probe = 0;
                 probe < captureShaderSlots_.size();
                 ++probe) {
                auto& slot = captureShaderSlots_[
                    (firstSlot + probe) % captureShaderSlots_.size()];
                const auto existing = slot.shader.load(
                    std::memory_order_acquire);
                if (existing == shader) {
                    return;
                }
                if (existing) {
                    continue;
                }
                slot.owner = shader;
                slot.contractPlusOne.store(
                    contractPlusOne,
                    std::memory_order_relaxed);
                slot.shader.store(shader, std::memory_order_release);
                matchingCaptureShaders_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return;
            }
        } catch (...) {
            logging::warn(
                "IBL capture shader registration failed; the diagnostic remains fail-closed.");
            return;
        }

        if (!captureRegistryOverflowLogged_.exchange(
                true,
                std::memory_order_acq_rel)) {
            logging::warn(
                "IBL capture shader registry exhausted its {} fixed slots; later matches will remain unobserved.",
                captureShaderSlots_.size());
        }
    }

    std::uint16_t Runtime::captureProbeForShader(
        ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return 0;
        }
        const auto firstSlot =
            (reinterpret_cast<std::uintptr_t>(shader) >> 4) %
            captureShaderSlots_.size();
        for (std::size_t probe = 0;
             probe < captureShaderSlots_.size();
             ++probe) {
            const auto& slot = captureShaderSlots_[
                (firstSlot + probe) % captureShaderSlots_.size()];
            const auto candidate = slot.shader.load(std::memory_order_acquire);
            if (!candidate) {
                return 0;
            }
            if (candidate == shader) {
                return slot.contractPlusOne.load(std::memory_order_relaxed);
            }
        }
        return 0;
    }

    void Runtime::onCaptureProbeDraw(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size()) {
            return;
        }
        const auto contractIndex =
            static_cast<std::size_t>(contractPlusOne - 1);
        if (captureProbeLogged_[contractIndex].exchange(
                true,
                std::memory_order_acq_rel)) {
            return;
        }

        std::array<ID3D11ShaderResourceView*, kCaptureShaderResourceCount>
            shaderResources{};
        context->PSGetShaderResources(
            0,
            static_cast<UINT>(shaderResources.size()),
            shaderResources.data());

        std::array<ID3D11RenderTargetView*, kCaptureRenderTargetCount>
            renderTargets{};
        ID3D11DepthStencilView* depthStencil{};
        context->OMGetRenderTargets(
            static_cast<UINT>(renderTargets.size()),
            renderTargets.data(),
            &depthStencil);

        ID3D11Buffer* sceneConstantsRaw{};
        context->PSGetConstantBuffers(12, 1, &sceneConstantsRaw);
        ComPtr<ID3D11Buffer> sceneConstants;
        sceneConstants.Attach(sceneConstantsRaw);

        std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
            viewports{};
        UINT viewportCount = static_cast<UINT>(viewports.size());
        context->RSGetViewports(&viewportCount, viewports.data());

        const auto shaderResourceCount = static_cast<unsigned>(
            std::ranges::count_if(
                shaderResources,
                [] (const auto* view) { return view != nullptr; }));
        const auto renderTargetCount = static_cast<unsigned>(
            std::ranges::count_if(
                renderTargets,
                [] (const auto* view) { return view != nullptr; }));
        const auto& contract = kCaptureProbeContracts[contractIndex];
        const auto checksumPrefix =
            (static_cast<std::uint32_t>(contract.checksum[0]) << 24) |
            (static_cast<std::uint32_t>(contract.checksum[1]) << 16) |
            (static_cast<std::uint32_t>(contract.checksum[2]) << 8) |
            static_cast<std::uint32_t>(contract.checksum[3]);
        logging::info(
            "IBL capture WORLD DFComposite[{:02}] {:08x} draw: PS-SRVs={}, RTVs={}, DSV={}, b12={}, viewports={}; state read only, image unchanged.",
            contractPlusOne,
            checksumPrefix,
            shaderResourceCount,
            renderTargetCount,
            depthStencil != nullptr,
            sceneConstants != nullptr,
            viewportCount);

        if (sceneConstants) {
            D3D11_BUFFER_DESC description{};
            sceneConstants->GetDesc(&description);
            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} PS-b12: resource=0x{:X}, bytes={}, usage={}, bind=0x{:X}, cpu=0x{:X}, misc=0x{:X}.",
                contractPlusOne,
                checksumPrefix,
                reinterpret_cast<std::uintptr_t>(sceneConstants.Get()),
                description.ByteWidth,
                static_cast<unsigned>(description.Usage),
                description.BindFlags,
                description.CPUAccessFlags,
                description.MiscFlags);
        }

        for (UINT viewportIndex = 0; viewportIndex < viewportCount;
             ++viewportIndex) {
            const auto& viewport = viewports[viewportIndex];
            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} viewport{}: origin=({}, {}), extent={}x{}, depth={}..{}.",
                contractPlusOne,
                checksumPrefix,
                viewportIndex,
                viewport.TopLeftX,
                viewport.TopLeftY,
                viewport.Width,
                viewport.Height,
                viewport.MinDepth,
                viewport.MaxDepth);
        }

        for (std::size_t slot = 0; slot < shaderResources.size(); ++slot) {
            ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(shaderResources[slot]);
            if (!view) {
                continue;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC description{};
            view->GetDesc(&description);
            logCaptureView(
                contractPlusOne,
                checksumPrefix,
                "PS-t",
                static_cast<UINT>(slot),
                view.Get(),
                description.Format,
                static_cast<UINT>(description.ViewDimension),
                textureViewRange(description));
        }
        for (std::size_t slot = 0; slot < renderTargets.size(); ++slot) {
            ComPtr<ID3D11RenderTargetView> view;
            view.Attach(renderTargets[slot]);
            if (!view) {
                continue;
            }
            D3D11_RENDER_TARGET_VIEW_DESC description{};
            view->GetDesc(&description);
            logCaptureView(
                contractPlusOne,
                checksumPrefix,
                "OM-RT",
                static_cast<UINT>(slot),
                view.Get(),
                description.Format,
                static_cast<UINT>(description.ViewDimension),
                textureViewRange(description));
        }
        {
            ComPtr<ID3D11DepthStencilView> view;
            view.Attach(depthStencil);
            if (view) {
                D3D11_DEPTH_STENCIL_VIEW_DESC description{};
                view->GetDesc(&description);
                logCaptureView(
                    contractPlusOne,
                    checksumPrefix,
                    "OM-DS",
                    0,
                    view.Get(),
                    description.Format,
                    static_cast<UINT>(description.ViewDimension),
                    textureViewRange(description));
            }
        }
        completedCaptureProbes_.fetch_add(1, std::memory_order_relaxed);
    }

    bool Runtime::createResources() noexcept
    {
        const auto embedded = loadEmbeddedShader(IDR_IBL_DIFFUSE_PROJECTION_CS);
        if (!embedded.data || embedded.size < 20 ||
            std::memcmp(embedded.data, "DXBC", 4) != 0 ||
            FAILED(device_->CreateComputeShader(
                embedded.data,
                embedded.size,
                nullptr,
                &projectionShader_))) {
            return false;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(
                &samplerDescription,
                &linearSampler_))) {
            return false;
        }

        D3D11_TEXTURE2D_DESC projectionDescription{};
        projectionDescription.Width = 3;
        projectionDescription.Height = 1;
        projectionDescription.MipLevels = 1;
        projectionDescription.ArraySize = 1;
        projectionDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        projectionDescription.SampleDesc.Count = 1;
        projectionDescription.Usage = D3D11_USAGE_DEFAULT;
        projectionDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device_->CreateTexture2D(
                &projectionDescription,
                nullptr,
                &projectionTexture_))) {
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
        uavDescription.Format = projectionDescription.Format;
        uavDescription.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDescription.Texture2D.MipSlice = 0;
        if (FAILED(device_->CreateUnorderedAccessView(
                projectionTexture_.Get(),
                &uavDescription,
                &projectionUav_))) {
            return false;
        }

        auto stagingDescription = projectionDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& slot : readbackRing_) {
            if (FAILED(device_->CreateTexture2D(
                    &stagingDescription,
                    nullptr,
                    &slot.texture))) {
                return false;
            }
        }
        return true;
    }

    bool Runtime::refreshNativeCubemap() noexcept
    {
        const auto gameModule = GetModuleHandleW(nullptr);
        if (!gameModule) {
            return false;
        }
        const auto* slot = reinterpret_cast<
            ID3D11ShaderResourceView* const*>(
            reinterpret_cast<std::uintptr_t>(gameModule) +
            kNativeCubemapSrvRva);
        if (!readableRange(slot, sizeof(*slot), gameModule)) {
            return false;
        }
        auto* candidate = *slot;
        if (candidate == nativeCubemapSrv_.Get() && candidate) {
            return true;
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        if (!validateNativeCubemap(
                candidate,
                textureDescription,
                viewDescription)) {
            nativeCubemapSrv_.Reset();
            nativeCubemapReady_.store(false, std::memory_order_release);
            if (publishedUsable_.load(std::memory_order_acquire)) {
                publishUnavailable(
                    lastProcessedGeneration_,
                    GetTickCount64());
            }
            if (!loggedSourceUnavailable_) {
                loggedSourceUnavailable_ = true;
                logging::warn(
                    "IBL native cubemap source is not ready at verified Fallout4VR RVA 0x{:X}; projection will retry without blocking rendering.",
                    kNativeCubemapSrvRva);
            }
            return false;
        }

        nativeCubemapSrv_ = candidate;
        nativeCubemapReady_.store(true, std::memory_order_release);
        loggedSourceUnavailable_ = false;
        if (!loggedSourceReady_) {
            loggedSourceReady_ = true;
            logging::info(
                "IBL acquired verified shared FO4VR cubemap: {}x{}, format={}, mips={}, array={}, PS-SRV cube view.",
                textureDescription.Width,
                textureDescription.Height,
                static_cast<unsigned>(textureDescription.Format),
                textureDescription.MipLevels,
                textureDescription.ArraySize);
        }
        return true;
    }

    void Runtime::consumeCompletedReadbacks() noexcept
    {
        for (auto& slot : readbackRing_) {
            if (!slot.pending) {
                continue;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result = context_->Map(
                slot.texture.Get(),
                0,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &mapped);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
                continue;
            }
            slot.pending = false;
            if (FAILED(result)) {
                invalidReadbacks_.fetch_add(1, std::memory_order_relaxed);
                if (slot.generation > lastProcessedGeneration_) {
                    lastProcessedGeneration_ = slot.generation;
                    consecutiveBlackReadbacks_ = 0;
                    publishUnavailable(slot.generation, GetTickCount64());
                }
                if (!loggedReadbackFailure_) {
                    loggedReadbackFailure_ = true;
                    logging::warn(
                        "IBL asynchronous SH readback failed with HRESULT 0x{:08X}; SH publication was invalidated.",
                        static_cast<unsigned>(result));
                }
                continue;
            }
            if (!mapped.pData || mapped.RowPitch < sizeof(DiffuseSH)) {
                context_->Unmap(slot.texture.Get(), 0);
                invalidReadbacks_.fetch_add(1, std::memory_order_relaxed);
                if (slot.generation > lastProcessedGeneration_) {
                    lastProcessedGeneration_ = slot.generation;
                    consecutiveBlackReadbacks_ = 0;
                    publishUnavailable(slot.generation, GetTickCount64());
                }
                continue;
            }

            DiffuseSH coefficients{};
            std::memcpy(&coefficients, mapped.pData, sizeof(coefficients));
            context_->Unmap(slot.texture.Get(), 0);
            if (slot.generation <= lastProcessedGeneration_) {
                continue;
            }
            lastProcessedGeneration_ = slot.generation;
            const auto state = classifyDiffuseSH(coefficients);
            const auto sampleTick = GetTickCount64();
            if (state == DiffuseSHState::invalid) {
                invalidReadbacks_.fetch_add(1, std::memory_order_relaxed);
                consecutiveBlackReadbacks_ = 0;
                publishUnavailable(slot.generation, sampleTick);
                continue;
            }
            completedReadbacks_.fetch_add(1, std::memory_order_relaxed);
            if (!loggedFirstReadback_) {
                loggedFirstReadback_ = true;
                logging::info(
                    "IBL first nonblocking native-cubemap SH readback completed: generation={}, state={}, L0 RGB=({}, {}, {}).",
                    slot.generation,
                    state == DiffuseSHState::usable ? "usable" : "black",
                    coefficients.rgb[0][0],
                    coefficients.rgb[1][0],
                    coefficients.rgb[2][0]);
            }
            if (state == DiffuseSHState::black) {
                blackReadbacks_.fetch_add(1, std::memory_order_relaxed);
                publishUnavailable(slot.generation, sampleTick);
                ++consecutiveBlackReadbacks_;
                if (!loggedBlackStreak_ &&
                    consecutiveBlackReadbacks_ >=
                        kBlackReadbackWarningThreshold) {
                    loggedBlackStreak_ = true;
                    logging::warn(
                        "IBL native cubemap remained black across {} completed projections; ambient integration remains fail-closed until radiance appears.",
                        consecutiveBlackReadbacks_);
                }
                continue;
            }

            consecutiveBlackReadbacks_ = 0;
            usableReadbacks_.fetch_add(1, std::memory_order_relaxed);
            publishUsable(coefficients, slot.generation, sampleTick);
            if (!loggedFirstUsableReadback_) {
                loggedFirstUsableReadback_ = true;
                logging::info(
                    "IBL first usable native-cubemap SH sample published: generation={}, L0 RGB=({}, {}, {}).",
                    slot.generation,
                    coefficients.rgb[0][0],
                    coefficients.rgb[1][0],
                    coefficients.rgb[2][0]);
            }
        }
    }

    void Runtime::dispatchProjection() noexcept
    {
        const auto available = std::ranges::find_if(
            readbackRing_,
            [] (const ReadbackSlot& slot) { return !slot.pending; });
        if (available == readbackRing_.end()) {
            return;
        }

        ScopedComputeState restore(context_.Get());
        auto* source = nativeCubemapSrv_.Get();
        auto* destination = projectionUav_.Get();
        auto* sampler = linearSampler_.Get();
        context_->CSSetShader(projectionShader_.Get(), nullptr, 0);
        context_->CSSetShaderResources(0, 1, &source);
        context_->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
        context_->CSSetSamplers(0, 1, &sampler);
        context_->Dispatch(1, 1, 1);

        ID3D11ShaderResourceView* nullSource{};
        ID3D11UnorderedAccessView* nullDestination{};
        context_->CSSetShaderResources(0, 1, &nullSource);
        context_->CSSetUnorderedAccessViews(
            0,
            1,
            &nullDestination,
            nullptr);
        context_->CopyResource(available->texture.Get(), projectionTexture_.Get());
        available->pending = true;
        available->generation = nextGeneration_++;
        projectionDispatches_.fetch_add(1, std::memory_order_relaxed);
    }

    void Runtime::onDFLightAmbientBind(ID3D11DeviceContext* context) noexcept
    {
        if (!context || context != context_.Get() ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return;
        }
        const auto now = GetTickCount64();
        if (now < nextCadenceTickMilliseconds_) {
            return;
        }
        nextCadenceTickMilliseconds_ = now + kProjectionCadenceMilliseconds;
        cadenceTicks_.fetch_add(1, std::memory_order_relaxed);
        consumeCompletedReadbacks();
        if (refreshNativeCubemap()) {
            dispatchProjection();
        }
    }

    void Runtime::publishUsable(
        const DiffuseSH& coefficients,
        std::uint64_t generation,
        std::uint64_t tickMilliseconds) noexcept
    {
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        std::size_t index{};
        for (const auto& channel : coefficients.rgb) {
            for (const auto coefficient : channel) {
                publishedCoefficientBits_[index++].store(
                    std::bit_cast<std::uint32_t>(coefficient),
                    std::memory_order_relaxed);
            }
        }
        publishedGeneration_.store(generation, std::memory_order_relaxed);
        publishedTickMilliseconds_.store(
            tickMilliseconds,
            std::memory_order_relaxed);
        publishedUsable_.store(true, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::publishUnavailable(
        std::uint64_t generation,
        std::uint64_t tickMilliseconds) noexcept
    {
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        publishedGeneration_.store(generation, std::memory_order_relaxed);
        publishedTickMilliseconds_.store(
            tickMilliseconds,
            std::memory_order_relaxed);
        publishedUsable_.store(false, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        RuntimeSnapshot result{
            .resourcesReady = resourcesReady_.load(std::memory_order_acquire),
            .nativeCubemapReady = nativeCubemapReady_.load(
                std::memory_order_acquire),
            .cadenceTicks = cadenceTicks_.load(std::memory_order_acquire),
            .projectionDispatches = projectionDispatches_.load(
                std::memory_order_acquire),
            .completedReadbacks = completedReadbacks_.load(
                std::memory_order_acquire),
            .blackReadbacks = blackReadbacks_.load(
                std::memory_order_acquire),
            .usableReadbacks = usableReadbacks_.load(
                std::memory_order_acquire),
            .invalidReadbacks = invalidReadbacks_.load(
                std::memory_order_acquire),
            .matchingCaptureShaders = matchingCaptureShaders_.load(
                std::memory_order_acquire),
            .completedCaptureProbes = completedCaptureProbes_.load(
                std::memory_order_acquire),
        };
        constexpr std::uint32_t kMaximumSnapshotAttempts = 3;
        for (std::uint32_t attempt = 0; attempt < kMaximumSnapshotAttempts;
             ++attempt) {
            const auto before = publishedSequence_.load(
                std::memory_order_acquire);
            if ((before & 1u) != 0) {
                continue;
            }
            DiffuseSH coefficients{};
            std::size_t index{};
            for (auto& channel : coefficients.rgb) {
                for (auto& coefficient : channel) {
                    coefficient = std::bit_cast<float>(
                        publishedCoefficientBits_[index++].load(
                            std::memory_order_relaxed));
                }
            }
            const auto usable = publishedUsable_.load(
                std::memory_order_relaxed);
            const auto generation = publishedGeneration_.load(
                std::memory_order_relaxed);
            const auto tickMilliseconds = publishedTickMilliseconds_.load(
                std::memory_order_relaxed);
            const auto after = publishedSequence_.load(
                std::memory_order_acquire);
            if (before == after && (after & 1u) == 0) {
                result.diffuseSHUsable = usable;
                result.latestSampleGeneration = generation;
                result.latestSampleTickMilliseconds = tickMilliseconds;
                result.latestDiffuseSH = coefficients;
                break;
            }
        }
        return result;
    }

    void Runtime::resetCaptureProbes() noexcept
    {
        try {
            const std::scoped_lock lock(captureShaderMutex_);
            for (auto& slot : captureShaderSlots_) {
                slot.shader.store(nullptr, std::memory_order_release);
                slot.contractPlusOne.store(0, std::memory_order_relaxed);
                slot.owner.Reset();
            }
        } catch (...) {
            logging::warn(
                "IBL capture shader registry reset failed; diagnostics remain fail-closed.");
        }
        for (auto& logged : captureProbeLogged_) {
            logged.store(false, std::memory_order_relaxed);
        }
        matchingCaptureShaders_.store(0, std::memory_order_relaxed);
        completedCaptureProbes_.store(0, std::memory_order_relaxed);
        captureRegistryOverflowLogged_.store(false, std::memory_order_relaxed);
    }

    void Runtime::resetResources() noexcept
    {
        resetCaptureProbes();
        resourcesReady_.store(false, std::memory_order_release);
        nativeCubemapReady_.store(false, std::memory_order_release);
        nativeCubemapSrv_.Reset();
        for (auto& slot : readbackRing_) {
            slot = {};
        }
        projectionUav_.Reset();
        projectionTexture_.Reset();
        linearSampler_.Reset();
        projectionShader_.Reset();
        context_.Reset();
        device_.Reset();
        nextGeneration_ = 1;
        lastProcessedGeneration_ = 0;
        nextCadenceTickMilliseconds_ = 0;
        consecutiveBlackReadbacks_ = 0;
        loggedSourceReady_ = false;
        loggedFirstReadback_ = false;
        loggedFirstUsableReadback_ = false;
        loggedBlackStreak_ = false;
        loggedSourceUnavailable_ = false;
        loggedReadbackFailure_ = false;
        cadenceTicks_.store(0, std::memory_order_relaxed);
        projectionDispatches_.store(0, std::memory_order_relaxed);
        completedReadbacks_.store(0, std::memory_order_relaxed);
        blackReadbacks_.store(0, std::memory_order_relaxed);
        usableReadbacks_.store(0, std::memory_order_relaxed);
        invalidReadbacks_.store(0, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        publishedUsable_.store(false, std::memory_order_relaxed);
        publishedGeneration_.store(0, std::memory_order_relaxed);
        publishedTickMilliseconds_.store(0, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }
}
