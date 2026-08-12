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
#include <ranges>

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
        constexpr std::uint64_t kWorldCaptureProbeSettleMilliseconds = 5000;
        constexpr std::uint32_t kBlackReadbackWarningThreshold = 8;
        constexpr UINT kCaptureShaderResourceCount = 16;
        // Exact DFComposite DXBC samples t5/t6 in the final lighting path and
        // repeatedly samples t10 in the complex path. Live world evidence
        // exposes them at the packed-stereo output resolution. Their
        // production meaning remains untrusted until this probe records
        // non-black stereo radiance; these are evidence sources, not an IBL
        // integration contract.
        constexpr std::array<UINT, 3> kSceneRadianceCandidateSlots{
            5,
            6,
            10,
        };
        constexpr std::uint32_t kSceneRadianceMaximumReadbackPolls = 80;
        constexpr UINT kCaptureRenderTargetCount =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        constexpr std::array<DXGI_FORMAT, 2> kSceneRadianceProbeFormats{
            DXGI_FORMAT_R11G11B10_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
        };

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

        [[nodiscard]] const char* sceneProbeFormatName(
            DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R11G11B10_FLOAT:
                return "R11G11B10_FLOAT";
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return "R8G8B8A8_UNORM";
            default:
                return "unsupported";
            }
        }

        [[nodiscard]] bool copySceneProbeSamples(
            ID3D11DeviceContext* context,
            ID3D11Texture2D* source,
            ID3D11Texture2D* destination,
            const D3D11_TEXTURE2D_DESC& sourceDescription) noexcept
        {
            if (!context || !source || !destination ||
                sourceDescription.Width < 4 || sourceDescription.Height < 2 ||
                sourceDescription.MipLevels == 0 ||
                sourceDescription.ArraySize != 1 ||
                sourceDescription.SampleDesc.Count != 1 ||
                std::ranges::find(
                    kSceneRadianceProbeFormats,
                    sourceDescription.Format) ==
                    kSceneRadianceProbeFormats.end()) {
                return false;
            }

            const auto coordinates = sceneProbeCoordinates(
                sourceDescription.Width,
                sourceDescription.Height);
            for (std::size_t index = 0; index < coordinates.size(); ++index) {
                const auto& coordinate = coordinates[index];
                const D3D11_BOX sourceBox{
                    coordinate.x,
                    coordinate.y,
                    0,
                    coordinate.x + 1,
                    coordinate.y + 1,
                    1,
                };
                context->CopySubresourceRegion(
                    destination,
                    0,
                    static_cast<UINT>(index),
                    0,
                    0,
                    source,
                    0,
                    &sourceBox);
            }
            return true;
        }

        enum class SceneProbeReadResult
        {
            pending,
            ready,
            failed,
        };

        [[nodiscard]] SceneProbeReadResult readSceneProbeSamples(
            ID3D11DeviceContext* context,
            ID3D11Texture2D* texture,
            DXGI_FORMAT format,
            std::array<SceneProbeRgb, kSceneProbeSampleCount>& samples)
            noexcept
        {
            if (!context || !texture) {
                return SceneProbeReadResult::failed;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result = context->Map(
                texture,
                0,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &mapped);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
                return SceneProbeReadResult::pending;
            }
            if (FAILED(result) || !mapped.pData ||
                mapped.RowPitch < kSceneProbeSampleCount * sizeof(std::uint32_t)) {
                return SceneProbeReadResult::failed;
            }

            const auto* source = static_cast<const std::byte*>(mapped.pData);
            for (std::size_t index = 0; index < samples.size(); ++index) {
                std::uint32_t packed{};
                std::memcpy(
                    &packed,
                    source + index * sizeof(packed),
                    sizeof(packed));
                switch (format) {
                case DXGI_FORMAT_R11G11B10_FLOAT:
                    samples[index] = decodeR11G11B10Float(packed);
                    break;
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    samples[index] = decodeR8G8B8A8Unorm(packed);
                    break;
                default:
                    context->Unmap(texture, 0);
                    return SceneProbeReadResult::failed;
                }
            }
            context->Unmap(texture, 0);
            return SceneProbeReadResult::ready;
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

    void Runtime::beginWorldCaptureProbeSession() noexcept
    {
        const auto earliestTick =
            GetTickCount64() + kWorldCaptureProbeSettleMilliseconds;
        requestedCaptureProbeEarliestTickMilliseconds_.store(
            earliestTick,
            std::memory_order_release);
        auto sessionId = requestedCaptureProbeSessionId_.fetch_add(
                             1,
                             std::memory_order_release) +
            1;
        if (sessionId == 0) {
            sessionId = 1;
            requestedCaptureProbeSessionId_.store(
                sessionId,
                std::memory_order_release);
        }
        logging::info(
            "IBL world-capture diagnostic session {} armed; sampling begins after a {} ms world-settle interval and remains image-neutral.",
            sessionId,
            kWorldCaptureProbeSettleMilliseconds);
    }

    bool Runtime::activateWorldCaptureProbeSession() noexcept
    {
        const auto requestedSessionId =
            requestedCaptureProbeSessionId_.load(std::memory_order_acquire);
        if (requestedSessionId == 0) {
            return false;
        }
        if (requestedSessionId != activeCaptureProbeSessionId_) {
            activeCaptureProbeSessionId_ = requestedSessionId;
            activeCaptureProbeEarliestTickMilliseconds_ =
                requestedCaptureProbeEarliestTickMilliseconds_.load(
                    std::memory_order_relaxed);
            captureProbeSessionComplete_ = false;
            for (auto& logged : captureProbeLogged_) {
                logged.store(false, std::memory_order_relaxed);
            }
            for (auto& slot : sceneRadianceReadbackSlots_) {
                slot.sourceWidth = 0;
                slot.sourceHeight = 0;
                slot.contractPlusOne = 0;
                slot.checksumPrefix = 0;
                slot.pendingPolls = 0;
                slot.rollingReady = false;
                slot.rollingPixelShaderCopied.fill(false);
                slot.pending = false;
                slot.completed = false;
                slot.pixelShaderCopied.fill(false);
                slot.failureLogged = false;
            }
            completedCaptureProbes_.store(0, std::memory_order_relaxed);
        }
        return !captureProbeSessionComplete_ &&
            GetTickCount64() >= activeCaptureProbeEarliestTickMilliseconds_;
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        std::size_t bytecodeSize,
        ID3D11PixelShader* shader) noexcept
    {
        const auto binding = classifyCaptureProbeShader(
            bytecode,
            bytecodeSize);
        if (!binding.isDFComposite || !shader) {
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
                    binding.environmentContractPlusOne,
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

    CaptureProbeShaderBinding Runtime::captureProbeBindingForShader(
        ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return {};
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
                return {};
            }
            if (candidate == shader) {
                return {
                    true,
                    slot.contractPlusOne.load(std::memory_order_relaxed)
                };
            }
        }
        return {};
    }

    void Runtime::onCaptureProbeDraw(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
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

    void Runtime::onCaptureProbeDrawComplete(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
            return;
        }

        ID3D11RenderTargetView* outputViewRaw{};
        context->OMGetRenderTargets(1, &outputViewRaw, nullptr);
        ComPtr<ID3D11RenderTargetView> outputView;
        outputView.Attach(outputViewRaw);
        if (!outputView) {
            return;
        }

        ComPtr<ID3D11Resource> outputResource;
        outputView->GetResource(&outputResource);
        ComPtr<ID3D11Texture2D> outputTexture;
        if (!outputResource || FAILED(outputResource.As(&outputTexture)) ||
            !outputTexture) {
            return;
        }

        D3D11_TEXTURE2D_DESC outputDescription{};
        outputTexture->GetDesc(&outputDescription);
        if (outputDescription.ArraySize != 1 ||
            outputDescription.MipLevels == 0 ||
            outputDescription.SampleDesc.Count != 1) {
            return;
        }

        const auto readback = std::ranges::find_if(
            sceneRadianceReadbackSlots_,
            [&outputDescription](const auto& slot) {
                return slot.format == outputDescription.Format;
            });
        if (readback == sceneRadianceReadbackSlots_.end() ||
            readback->rollingReady || readback->pending ||
            readback->completed ||
            !readback->rollingCompositeTexture ||
            !std::ranges::all_of(
                readback->rollingPixelShaderTextures,
                [](const auto& texture) { return texture != nullptr; })) {
            return;
        }

        const auto acquireCandidate = [](ID3D11ShaderResourceView* rawView,
                                          ComPtr<ID3D11Texture2D>& texture,
                                          D3D11_TEXTURE2D_DESC& description) {
            ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(rawView);
            if (!view) {
                return;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (resource && SUCCEEDED(resource.As(&texture)) && texture) {
                texture->GetDesc(&description);
            }
        };
        std::array<ComPtr<ID3D11Texture2D>, kSceneRadianceCandidateCount>
            candidateTextures{};
        std::array<D3D11_TEXTURE2D_DESC, kSceneRadianceCandidateCount>
            candidateDescriptions{};
        for (std::size_t index = 0;
             index < kSceneRadianceCandidateSlots.size();
             ++index) {
            ID3D11ShaderResourceView* candidateView{};
            context->PSGetShaderResources(
                kSceneRadianceCandidateSlots[index],
                1,
                &candidateView);
            acquireCandidate(
                candidateView,
                candidateTextures[index],
                candidateDescriptions[index]);
        }

        const auto candidateCompatible = [&outputDescription](
                                             ID3D11Texture2D* source,
                                             const D3D11_TEXTURE2D_DESC& description) {
            return source &&
                description.Width == outputDescription.Width &&
                description.Height == outputDescription.Height &&
                description.Format == outputDescription.Format &&
                description.ArraySize == 1 && description.MipLevels > 0 &&
                description.SampleDesc.Count == 1;
        };
        constexpr auto t10CandidateIndex = std::size_t{ 2 };
        if (!candidateCompatible(
                candidateTextures[t10CandidateIndex].Get(),
                candidateDescriptions[t10CandidateIndex])) {
            return;
        }
        if (!copySceneProbeSamples(
                context,
                outputTexture.Get(),
                readback->rollingCompositeTexture.Get(),
                outputDescription)) {
            return;
        }
        for (std::size_t index = 0;
             index < kSceneRadianceCandidateSlots.size();
             ++index) {
            readback->rollingPixelShaderCopied[index] =
                candidateCompatible(
                    candidateTextures[index].Get(),
                    candidateDescriptions[index]) &&
                copySceneProbeSamples(
                    context,
                    candidateTextures[index].Get(),
                    readback->rollingPixelShaderTextures[index].Get(),
                    candidateDescriptions[index]);
        }

        const auto contractIndex = static_cast<std::size_t>(
            contractPlusOne - 1);
        const auto& contract = kCaptureProbeContracts[contractIndex];
        readback->sourceWidth = outputDescription.Width;
        readback->sourceHeight = outputDescription.Height;
        readback->contractPlusOne = contractPlusOne;
        readback->checksumPrefix =
            (static_cast<std::uint32_t>(contract.checksum[0]) << 24) |
            (static_cast<std::uint32_t>(contract.checksum[1]) << 16) |
            (static_cast<std::uint32_t>(contract.checksum[2]) << 8) |
            static_cast<std::uint32_t>(contract.checksum[3]);
        readback->rollingReady = true;
    }

    void Runtime::onCaptureProbePassComplete(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
            return;
        }

        for (auto& readback : sceneRadianceReadbackSlots_) {
            if (!readback.rollingReady || readback.pending ||
                readback.completed || !readback.rollingCompositeTexture ||
                !readback.compositeTexture) {
                continue;
            }

            context->CopyResource(
                readback.compositeTexture.Get(),
                readback.rollingCompositeTexture.Get());
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                readback.pixelShaderCopied[index] =
                    readback.rollingPixelShaderCopied[index] &&
                    readback.rollingPixelShaderTextures[index] &&
                    readback.pixelShaderTextures[index];
                if (readback.pixelShaderCopied[index]) {
                    context->CopyResource(
                        readback.pixelShaderTextures[index].Get(),
                        readback.rollingPixelShaderTextures[index].Get());
                }
            }

            readback.rollingReady = false;
            readback.pendingPolls = 0;
            readback.pending = true;
            sceneRadianceProbeCaptures_.fetch_add(
                1,
                std::memory_order_relaxed);
        }
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
        return createSceneRadianceProbeResources();
    }

    bool Runtime::createSceneRadianceProbeResources() noexcept
    {
        for (std::size_t index = 0;
             index < sceneRadianceReadbackSlots_.size();
             ++index) {
            auto& slot = sceneRadianceReadbackSlots_[index];
            slot = {};
            slot.format = kSceneRadianceProbeFormats[index];

            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(kSceneProbeSampleCount);
            description.Height = 1;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = slot.format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            for (auto& texture : slot.rollingPixelShaderTextures) {
                if (FAILED(device_->CreateTexture2D(
                        &description,
                        nullptr,
                        &texture))) {
                    return false;
                }
            }
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    &slot.rollingCompositeTexture))) {
                return false;
            }

            description.Usage = D3D11_USAGE_STAGING;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (auto& texture : slot.pixelShaderTextures) {
                if (FAILED(device_->CreateTexture2D(
                        &description,
                        nullptr,
                        &texture))) {
                    return false;
                }
            }
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    &slot.compositeTexture))) {
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

    void Runtime::consumeSceneRadianceProbeReadbacks() noexcept
    {
        for (auto& slot : sceneRadianceReadbackSlots_) {
            if (!slot.pending) {
                continue;
            }

            std::array<
                std::array<SceneProbeRgb, kSceneProbeSampleCount>,
                kSceneRadianceCandidateCount>
                pixelShaderCandidates{};
            std::array<SceneProbeRgb, kSceneProbeSampleCount> composite{};
            std::array<SceneProbeReadResult, kSceneRadianceCandidateCount>
                candidateResults{};
            candidateResults.fill(SceneProbeReadResult::ready);
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                if (slot.pixelShaderCopied[index]) {
                    candidateResults[index] = readSceneProbeSamples(
                        context_.Get(),
                        slot.pixelShaderTextures[index].Get(),
                        slot.format,
                        pixelShaderCandidates[index]);
                }
            }
            const auto compositeResult = readSceneProbeSamples(
                context_.Get(),
                slot.compositeTexture.Get(),
                slot.format,
                composite);
            if (std::ranges::find(
                    candidateResults,
                    SceneProbeReadResult::pending) !=
                    candidateResults.end() ||
                compositeResult == SceneProbeReadResult::pending) {
                ++slot.pendingPolls;
                if (slot.pendingPolls <
                    kSceneRadianceMaximumReadbackPolls) {
                    continue;
                }
                slot.pending = false;
                slot.completed = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (!slot.failureLogged) {
                    slot.failureLogged = true;
                    logging::warn(
                        "IBL scene-radiance boundary readback timed out after {} nonblocking polls for format {}; the diagnostic remains fail-closed.",
                        slot.pendingPolls,
                        sceneProbeFormatName(slot.format));
                }
                continue;
            }
            if (std::ranges::find(
                    candidateResults,
                    SceneProbeReadResult::failed) !=
                    candidateResults.end() ||
                compositeResult == SceneProbeReadResult::failed) {
                slot.pending = false;
                slot.completed = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (!slot.failureLogged) {
                    slot.failureLogged = true;
                    logging::warn(
                        "IBL scene-radiance boundary readback failed for format {}; the diagnostic remains fail-closed.",
                        sceneProbeFormatName(slot.format));
                }
                continue;
            }

            const auto compositeSummary = summarizeSceneProbe(composite);
            logging::info(
                "IBL scene-radiance pass-end final-draw snapshot DFComposite[{:02}] {:08x}: format={}, packedExtent={}x{}, samples={}; image unchanged.",
                slot.contractPlusOne,
                slot.checksumPrefix,
                sceneProbeFormatName(slot.format),
                slot.sourceWidth,
                slot.sourceHeight,
                kSceneProbeSampleCount);
            const auto logPixelShaderCandidate = [&composite](
                                                     UINT shaderSlot,
                                                     bool copied,
                                                     const auto& samples) {
                if (!copied) {
                    logging::info(
                        "IBL scene-radiance PS-t{}: no same-format full-resolution candidate was bound at this boundary.",
                        shaderSlot);
                    return;
                }
                const auto candidateSummary = summarizeSceneProbe(samples);
                logging::info(
                    "IBL scene-radiance PS-t{}: avg=({}, {}, {}), left=({}, {}, {}), right=({}, {}, {}), peak={}, nonBlack={}/{}, meanAbsDeltaToComposite={}.",
                    shaderSlot,
                    candidateSummary.average.red,
                    candidateSummary.average.green,
                    candidateSummary.average.blue,
                    candidateSummary.leftEyeAverage.red,
                    candidateSummary.leftEyeAverage.green,
                    candidateSummary.leftEyeAverage.blue,
                    candidateSummary.rightEyeAverage.red,
                    candidateSummary.rightEyeAverage.green,
                    candidateSummary.rightEyeAverage.blue,
                    candidateSummary.peak,
                    candidateSummary.nonBlackSamples,
                    candidateSummary.validSamples,
                    meanAbsoluteSceneProbeDifference(samples, composite));
            };
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                logPixelShaderCandidate(
                    kSceneRadianceCandidateSlots[index],
                    slot.pixelShaderCopied[index],
                    pixelShaderCandidates[index]);
            }
            logging::info(
                "IBL scene-radiance OM-composite: avg=({}, {}, {}), left=({}, {}, {}), right=({}, {}, {}), peak={}, nonBlack={}/{}.",
                compositeSummary.average.red,
                compositeSummary.average.green,
                compositeSummary.average.blue,
                compositeSummary.leftEyeAverage.red,
                compositeSummary.leftEyeAverage.green,
                compositeSummary.leftEyeAverage.blue,
                compositeSummary.rightEyeAverage.red,
                compositeSummary.rightEyeAverage.green,
                compositeSummary.rightEyeAverage.blue,
                compositeSummary.peak,
                compositeSummary.nonBlackSamples,
                compositeSummary.validSamples);

            slot.pending = false;
            slot.completed = true;
            sceneRadianceProbeReadbacks_.fetch_add(
                1,
                std::memory_order_relaxed);
        }
        if (std::ranges::all_of(
                sceneRadianceReadbackSlots_,
                [](const auto& slot) { return slot.completed; })) {
            captureProbeSessionComplete_ = true;
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
        consumeSceneRadianceProbeReadbacks();
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
            .sceneRadianceProbeCaptures =
                sceneRadianceProbeCaptures_.load(std::memory_order_acquire),
            .sceneRadianceProbeReadbacks =
                sceneRadianceProbeReadbacks_.load(std::memory_order_acquire),
            .sceneRadianceProbeFailures =
                sceneRadianceProbeFailures_.load(std::memory_order_acquire),
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
        for (auto& slot : sceneRadianceReadbackSlots_) {
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
        sceneRadianceProbeCaptures_.store(0, std::memory_order_relaxed);
        sceneRadianceProbeReadbacks_.store(0, std::memory_order_relaxed);
        sceneRadianceProbeFailures_.store(0, std::memory_order_relaxed);
        requestedCaptureProbeSessionId_.store(0, std::memory_order_relaxed);
        requestedCaptureProbeEarliestTickMilliseconds_.store(
            0,
            std::memory_order_relaxed);
        activeCaptureProbeSessionId_ = 0;
        activeCaptureProbeEarliestTickMilliseconds_ = 0;
        captureProbeSessionComplete_ = true;
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        publishedUsable_.store(false, std::memory_order_relaxed);
        publishedGeneration_.store(0, std::memory_order_relaxed);
        publishedTickMilliseconds_.store(0, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }
}
