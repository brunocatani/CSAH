#include "Features/vanilla_fixes/FocusShadowRuntime.h"

#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::uintptr_t kProducerRva = 0x29126C0;
        constexpr std::uintptr_t kTaskPrepareRva = 0x28AAC40;
        constexpr std::uintptr_t kMapPublishRva = 0x2912500;
        constexpr std::uintptr_t kMapTaskExecuteRva = 0x28CB6C0;
        constexpr UINT kShadowMapSlot = 5;
        constexpr UINT kShadowMapWidth = 8192;
        constexpr UINT kShadowMapHeight = 8192;
        constexpr UINT kShadowMapArraySize = 4;
        constexpr std::size_t kPendingTaskCount = 64;
        constexpr std::size_t kTrackedFocusShaderCount = 16;

        constexpr std::array<std::uint8_t, 24> kProducerEntry{
            0x48, 0x8B, 0xC4,
            0x48, 0x89, 0x58, 0x10,
            0x48, 0x89, 0x70, 0x18,
            0x48, 0x89, 0x78, 0x20,
            0x55,
            0x41, 0x54,
            0x41, 0x55,
            0x41, 0x56,
            0x41, 0x57,
        };
        constexpr std::array<std::uint8_t, 17> kTaskPrepareEntry{
            0x40, 0x53,
            0x48, 0x83, 0xEC, 0x30,
            0x48, 0x8B, 0xD9,
            0x48, 0x8B, 0xD1,
            0x48, 0x8D, 0x4C, 0x24, 0x20,
        };
        constexpr std::array<std::uint8_t, 17> kMapPublishEntry{
            0x48, 0x89, 0x5C, 0x24, 0x08,
            0x57,
            0x48, 0x83, 0xEC, 0x20,
            0x48, 0x8B, 0x99, 0x98, 0x01, 0x00, 0x00,
        };
        constexpr std::array<std::uint8_t, 17> kMapTaskExecuteEntry{
            0x40, 0x53,
            0x48, 0x83, 0xEC, 0x20,
            0x48, 0x8B, 0x59, 0x08,
            0x0F, 0xB6, 0x51, 0x10,
            0x48, 0x8B, 0xCB,
        };

        using ProducerFunction = void(__fastcall*)(
            std::uintptr_t,
            const float*,
            const float*,
            std::uintptr_t);
        using TaskFunction = void(__fastcall*)(std::uintptr_t);

        ProducerFunction originalProducer{};
        TaskFunction originalTaskPrepare{};
        TaskFunction originalMapPublish{};
        TaskFunction originalMapTaskExecute{};
        std::atomic_bool hooksInstalled{};
        std::atomic_uintptr_t currentFocusEntry{};
        std::array<std::atomic_uintptr_t, kPendingTaskCount> pendingTasks{};
        thread_local bool creatingMapTask{};
        thread_local bool executingMapTask{};
        std::array<std::atomic_uintptr_t, kTrackedFocusShaderCount>
            focusShaders{};

        std::atomic_flag resourceWriter = ATOMIC_FLAG_INIT;
        Microsoft::WRL::ComPtr<ID3D11Resource> focusResource;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> focusView;
        std::atomic_uintptr_t resourceIdentity{};
        std::atomic_uint64_t exactTargetsObserved{};
        std::atomic_uint64_t viewsCreated{};
        std::atomic_uint64_t bindingsApplied{};
        std::atomic_uint64_t bindingFailures{};

        [[nodiscard]] bool executableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    address,
                    &information,
                    sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto protection = information.Protect & 0xFFu;
            const auto executable = protection == PAGE_EXECUTE ||
                protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            const auto regionEnd = regionBegin + information.RegionSize;
            return executable && begin >= regionBegin && begin <= regionEnd &&
                size <= regionEnd - begin;
        }

        template <std::size_t Size>
        [[nodiscard]] bool exactEntry(
            const void* address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            return executableRange(address, expected.size()) &&
                std::memcmp(address, expected.data(), expected.size()) == 0;
        }

        void registerPendingTask(const std::uintptr_t task) noexcept
        {
            if (!task) {
                return;
            }
            for (auto& pending : pendingTasks) {
                std::uintptr_t empty{};
                if (pending.compare_exchange_strong(
                        empty,
                        task,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    return;
                }
            }
        }

        [[nodiscard]] bool takePendingTask(
            const std::uintptr_t task) noexcept
        {
            if (!task) {
                return false;
            }
            for (auto& pending : pendingTasks) {
                auto expected = task;
                if (pending.compare_exchange_strong(
                        expected,
                        0,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    return true;
                }
            }
            return false;
        }

        void __fastcall hookProducer(
            const std::uintptr_t shadowEntry,
            const float* basis0,
            const float* basis1,
            const std::uintptr_t currentCamera) noexcept
        {
            if (!originalProducer) {
                return;
            }
            originalProducer(shadowEntry, basis0, basis1, currentCamera);
            currentFocusEntry.store(shadowEntry, std::memory_order_release);
        }

        void __fastcall hookMapPublish(
            const std::uintptr_t shadowEntry) noexcept
        {
            if (!originalMapPublish) {
                return;
            }
            const auto matched = shadowEntry &&
                shadowEntry == currentFocusEntry.load(
                    std::memory_order_acquire);
            const auto previous = creatingMapTask;
            creatingMapTask = matched;
            originalMapPublish(shadowEntry);
            creatingMapTask = previous;
        }

        void __fastcall hookTaskPrepare(const std::uintptr_t task) noexcept
        {
            if (!originalTaskPrepare) {
                return;
            }
            if (creatingMapTask) {
                registerPendingTask(task);
            }
            originalTaskPrepare(task);
        }

        void __fastcall hookMapTaskExecute(
            const std::uintptr_t callable) noexcept
        {
            if (!originalMapTaskExecute) {
                return;
            }
            std::uintptr_t task{};
            std::uint8_t mode{};
            if (callable) {
                std::memcpy(
                    &task,
                    reinterpret_cast<const void*>(callable + 0x8),
                    sizeof(task));
                std::memcpy(
                    &mode,
                    reinterpret_cast<const void*>(callable + 0x10),
                    sizeof(mode));
            }
            if (mode != 0x1E || !takePendingTask(task)) {
                originalMapTaskExecute(callable);
                return;
            }
            const auto previous = executingMapTask;
            executingMapTask = true;
            originalMapTaskExecute(callable);
            executingMapTask = previous;
        }

        [[nodiscard]] bool exactMap(
            const D3D11_DEPTH_STENCIL_VIEW_DESC& view,
            const D3D11_TEXTURE2D_DESC& texture) noexcept
        {
            return view.Format == DXGI_FORMAT_D16_UNORM &&
                view.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY &&
                view.Texture2DArray.MipSlice == 0 &&
                view.Texture2DArray.FirstArraySlice == 0 &&
                view.Texture2DArray.ArraySize == 1 &&
                texture.Format == DXGI_FORMAT_R16_TYPELESS &&
                texture.Width == kShadowMapWidth &&
                texture.Height == kShadowMapHeight &&
                texture.ArraySize == kShadowMapArraySize &&
                texture.MipLevels >= 1 &&
                texture.SampleDesc.Count == 1 &&
                (texture.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0 &&
                (texture.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
        }
    }

    bool installFocusShadowNativeHooks() noexcept
    {
        if (hooksInstalled.load(std::memory_order_acquire)) {
            return true;
        }
        const auto module = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (!module) {
            return false;
        }
        const std::array<void*, 4> targets{
            reinterpret_cast<void*>(module + kProducerRva),
            reinterpret_cast<void*>(module + kTaskPrepareRva),
            reinterpret_cast<void*>(module + kMapPublishRva),
            reinterpret_cast<void*>(module + kMapTaskExecuteRva),
        };
        if (!exactEntry(targets[0], kProducerEntry) ||
            !exactEntry(targets[1], kTaskPrepareEntry) ||
            !exactEntry(targets[2], kMapPublishEntry) ||
            !exactEntry(targets[3], kMapTaskExecuteEntry)) {
            logging::critical(
                "Vanilla Fixes focus-shadow native contract rejected; an entry is inaccessible, unknown, or already owned.");
            return false;
        }
        const std::array<void*, 4> detours{
            reinterpret_cast<void*>(&hookProducer),
            reinterpret_cast<void*>(&hookTaskPrepare),
            reinterpret_cast<void*>(&hookMapPublish),
            reinterpret_cast<void*>(&hookMapTaskExecute),
        };
        std::array<void*, 4> trampolines{};
        std::size_t created{};
        for (; created < targets.size(); ++created) {
            const auto status = MH_CreateHook(
                targets[created],
                detours[created],
                &trampolines[created]);
            if (status != MH_OK ||
                !executableRange(trampolines[created], 1)) {
                if (status == MH_OK) {
                    ++created;
                }
                break;
            }
        }
        if (created != targets.size()) {
            for (std::size_t index = 0; index < created; ++index) {
                (void)MH_RemoveHook(targets[index]);
            }
            return false;
        }
        originalProducer = reinterpret_cast<ProducerFunction>(trampolines[0]);
        originalTaskPrepare = reinterpret_cast<TaskFunction>(trampolines[1]);
        originalMapPublish = reinterpret_cast<TaskFunction>(trampolines[2]);
        originalMapTaskExecute =
            reinterpret_cast<TaskFunction>(trampolines[3]);
        const auto rollback = [&targets]() noexcept {
            for (auto* target : targets) {
                (void)MH_DisableHook(target);
                (void)MH_RemoveHook(target);
            }
            originalProducer = nullptr;
            originalTaskPrepare = nullptr;
            originalMapPublish = nullptr;
            originalMapTaskExecute = nullptr;
        };
        for (auto* target : targets) {
            if (MH_QueueEnableHook(target) != MH_OK) {
                rollback();
                return false;
            }
        }
        if (MH_ApplyQueued() != MH_OK) {
            rollback();
            return false;
        }
        const auto patched =
            std::memcmp(
                targets[0],
                kProducerEntry.data(),
                kProducerEntry.size()) != 0 &&
            std::memcmp(
                targets[1],
                kTaskPrepareEntry.data(),
                kTaskPrepareEntry.size()) != 0 &&
            std::memcmp(
                targets[2],
                kMapPublishEntry.data(),
                kMapPublishEntry.size()) != 0 &&
            std::memcmp(
                targets[3],
                kMapTaskExecuteEntry.data(),
                kMapTaskExecuteEntry.size()) != 0;
        if (!patched) {
            rollback();
            logging::critical(
                "Vanilla Fixes focus-shadow hooks activated without complete ownership proof; all four hooks were removed.");
            return false;
        }
        hooksInstalled.store(true, std::memory_order_release);
        logging::info(
            "Vanilla Fixes owns the verified focus-shadow producer, map-publish, task-prepare, and map-execute path.");
        return true;
    }

    void registerFocusShadowPixelShader(ID3D11PixelShader* shader) noexcept
    {
        const auto identity = reinterpret_cast<std::uintptr_t>(shader);
        if (!identity) {
            return;
        }
        for (auto& slot : focusShaders) {
            auto observed = slot.load(std::memory_order_acquire);
            if (observed == identity) {
                return;
            }
            if (observed == 0 && slot.compare_exchange_strong(
                    observed,
                    identity,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return;
            }
        }
    }

    bool isFocusShadowPixelShader(ID3D11PixelShader* shader) noexcept
    {
        const auto identity = reinterpret_cast<std::uintptr_t>(shader);
        if (!identity) {
            return false;
        }
        return std::ranges::any_of(
            focusShaders,
            [identity](const auto& slot) noexcept {
                return slot.load(std::memory_order_acquire) == identity;
            });
    }

    void observeFocusShadowRenderTargets(
        ID3D11DepthStencilView* depthTarget) noexcept
    {
        if (!executingMapTask || !depthTarget) {
            return;
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC viewDescription{};
        depthTarget->GetDesc(&viewDescription);
        ID3D11Resource* acquired{};
        depthTarget->GetResource(&acquired);
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        resource.Attach(acquired);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture))) {
            return;
        }
        D3D11_TEXTURE2D_DESC textureDescription{};
        texture->GetDesc(&textureDescription);
        if (!exactMap(viewDescription, textureDescription)) {
            return;
        }
        exactTargetsObserved.fetch_add(1, std::memory_order_relaxed);
        const auto identity = reinterpret_cast<std::uintptr_t>(resource.Get());
        if (resourceIdentity.load(std::memory_order_acquire) == identity) {
            return;
        }
        if (resourceWriter.test_and_set(std::memory_order_acquire)) {
            return;
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        resource->GetDevice(device.ReleaseAndGetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> candidate;
        if (device) {
            D3D11_SHADER_RESOURCE_VIEW_DESC description{};
            description.Format = DXGI_FORMAT_R16_UNORM;
            description.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            description.Texture2DArray.MostDetailedMip = 0;
            description.Texture2DArray.MipLevels = 1;
            description.Texture2DArray.FirstArraySlice = 0;
            description.Texture2DArray.ArraySize = kShadowMapArraySize;
            if (SUCCEEDED(device->CreateShaderResourceView(
                    resource.Get(),
                    &description,
                    candidate.ReleaseAndGetAddressOf())) &&
                candidate) {
                focusResource = resource;
                focusView = candidate;
                resourceIdentity.store(identity, std::memory_order_release);
                viewsCreated.fetch_add(1, std::memory_order_relaxed);
            }
        }
        resourceWriter.clear(std::memory_order_release);
    }

    ScopedFocusShadowBinding::ScopedFocusShadowBinding(
        ID3D11DeviceContext* context,
        const bool enabled) noexcept
    {
        if (!enabled || !context ||
            resourceWriter.test_and_set(std::memory_order_acquire)) {
            if (enabled && context) {
                bindingFailures.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        auto* replacement = focusView.Get();
        if (replacement) {
            replacement->AddRef();
        }
        resourceWriter.clear(std::memory_order_release);
        if (!replacement) {
            bindingFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        context->PSGetShaderResources(kShadowMapSlot, 1, &original_);
        if (original_ == replacement) {
            original_->Release();
            original_ = nullptr;
            replacement->Release();
            return;
        }
        context->PSSetShaderResources(kShadowMapSlot, 1, &replacement);
        replacement->Release();
        context_ = context;
        applied_ = true;
        bindingsApplied.fetch_add(1, std::memory_order_relaxed);
    }

    ScopedFocusShadowBinding::~ScopedFocusShadowBinding() noexcept
    {
        if (!applied_ || !context_) {
            return;
        }
        context_->PSSetShaderResources(kShadowMapSlot, 1, &original_);
        if (original_) {
            original_->Release();
        }
    }

    FocusShadowSnapshot focusShadowSnapshot() noexcept
    {
        return {
            .nativeHooksInstalled =
                hooksInstalled.load(std::memory_order_acquire),
            .exactMapTargetsObserved =
                exactTargetsObserved.load(std::memory_order_relaxed),
            .fullArrayViewsCreated =
                viewsCreated.load(std::memory_order_relaxed),
            .bindingsApplied =
                bindingsApplied.load(std::memory_order_relaxed),
            .bindingFailures =
                bindingFailures.load(std::memory_order_relaxed),
        };
    }
}
