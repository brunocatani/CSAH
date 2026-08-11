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
                if (!loggedReadbackFailure_) {
                    loggedReadbackFailure_ = true;
                    logging::warn(
                        "IBL asynchronous SH readback failed with HRESULT 0x{:08X}; the last valid coefficients remain published.",
                        static_cast<unsigned>(result));
                }
                continue;
            }
            if (!mapped.pData || mapped.RowPitch < sizeof(DiffuseSH)) {
                context_->Unmap(slot.texture.Get(), 0);
                invalidReadbacks_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            DiffuseSH coefficients{};
            std::memcpy(&coefficients, mapped.pData, sizeof(coefficients));
            context_->Unmap(slot.texture.Get(), 0);
            if (!validDiffuseSH(coefficients)) {
                invalidReadbacks_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (slot.generation > lastPublishedGeneration_) {
                publish(coefficients);
                lastPublishedGeneration_ = slot.generation;
            }
            completedReadbacks_.fetch_add(1, std::memory_order_relaxed);
            if (!loggedFirstReadback_) {
                loggedFirstReadback_ = true;
                logging::info(
                    "IBL first nonblocking native-cubemap SH readback completed: generation={}, L0 RGB=({}, {}, {}).",
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

    void Runtime::publish(const DiffuseSH& coefficients) noexcept
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
            .invalidReadbacks = invalidReadbacks_.load(
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
            const auto after = publishedSequence_.load(
                std::memory_order_acquire);
            if (before == after && (after & 1u) == 0) {
                result.latestDiffuseSH = coefficients;
                break;
            }
        }
        return result;
    }

    void Runtime::resetResources() noexcept
    {
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
        lastPublishedGeneration_ = 0;
        nextCadenceTickMilliseconds_ = 0;
        loggedSourceReady_ = false;
        loggedFirstReadback_ = false;
        loggedSourceUnavailable_ = false;
        loggedReadbackFailure_ = false;
    }
}
