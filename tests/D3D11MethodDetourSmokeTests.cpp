#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace
{
    using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
        ID3D11Device*,
        const void*,
        SIZE_T,
        ID3D11ClassLinkage*,
        ID3D11PixelShader**);
    using PSSetShaderFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*,
        ID3D11PixelShader*,
        ID3D11ClassInstance* const*,
        UINT);

    constexpr std::size_t kCreatePixelShaderVtableIndex = 15;
    constexpr std::size_t kPSSetShaderVtableIndex = 9;

    CreatePixelShaderFunction originalCreatePixelShader{};
    PSSetShaderFunction originalPSSetShader{};
    std::atomic_uint64_t createCalls{};
    std::atomic_uint64_t bindCalls{};

    HRESULT STDMETHODCALLTYPE hookCreatePixelShader(
        ID3D11Device* device,
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11ClassLinkage* classLinkage,
        ID3D11PixelShader** shader) noexcept
    {
        createCalls.fetch_add(1, std::memory_order_relaxed);
        return originalCreatePixelShader(
            device,
            bytecode,
            bytecodeLength,
            classLinkage,
            shader);
    }

    void STDMETHODCALLTYPE hookPSSetShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* shader,
        ID3D11ClassInstance* const* classInstances,
        UINT classInstanceCount) noexcept
    {
        bindCalls.fetch_add(1, std::memory_order_relaxed);
        originalPSSetShader(
            context,
            shader,
            classInstances,
            classInstanceCount);
    }

    [[nodiscard]] bool belongsToD3D11(const void* address) noexcept
    {
        HMODULE owner{};
        return address && GetModuleHandleExA(
                              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(address),
                              &owner) &&
            owner == GetModuleHandleW(L"d3d11.dll");
    }

    void reportFailure(const char* message)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
}

int main()
{
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    D3D_FEATURE_LEVEL featureLevel{};
    const auto createDeviceResult = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);
    if (FAILED(createDeviceResult) || !device || !context) {
        reportFailure("D3D11 WARP device creation failed");
        return EXIT_FAILURE;
    }

    auto** deviceVtable = *reinterpret_cast<void***>(device);
    auto** contextVtable = *reinterpret_cast<void***>(context);
    auto* createTarget = deviceVtable[kCreatePixelShaderVtableIndex];
    auto* bindTarget = contextVtable[kPSSetShaderVtableIndex];
    if (!belongsToD3D11(createTarget) || !belongsToD3D11(bindTarget)) {
        reportFailure("live shader methods are not owned by d3d11.dll");
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    auto status = MH_Initialize();
    if (status != MH_OK) {
        reportFailure("MinHook initialization failed");
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    void* createTrampoline{};
    void* bindTrampoline{};
    const auto createHookStatus = MH_CreateHook(
        createTarget,
        reinterpret_cast<void*>(&hookCreatePixelShader),
        &createTrampoline);
    const auto bindHookStatus = MH_CreateHook(
        bindTarget,
        reinterpret_cast<void*>(&hookPSSetShader),
        &bindTrampoline);
    if (createHookStatus != MH_OK || bindHookStatus != MH_OK ||
        !createTrampoline || !bindTrampoline) {
        reportFailure("D3D11 method prologue relocation failed");
        (void)MH_Uninitialize();
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }
    originalCreatePixelShader =
        reinterpret_cast<CreatePixelShaderFunction>(createTrampoline);
    originalPSSetShader = reinterpret_cast<PSSetShaderFunction>(bindTrampoline);

    const auto createQueueStatus = MH_QueueEnableHook(createTarget);
    const auto bindQueueStatus = MH_QueueEnableHook(bindTarget);
    status = MH_ApplyQueued();
    if (createQueueStatus != MH_OK || bindQueueStatus != MH_OK ||
        status != MH_OK) {
        reportFailure("queued D3D11 method detour activation failed");
        (void)MH_Uninitialize();
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    ID3D11PixelShader* invalidShader{};
    (void)device->CreatePixelShader(nullptr, 0, nullptr, &invalidShader);
    context->PSSetShader(nullptr, nullptr, 0);
    const auto callsObserved =
        createCalls.load(std::memory_order_relaxed) == 1 &&
        bindCalls.load(std::memory_order_relaxed) == 1;

    const auto disableStatus = MH_DisableHook(MH_ALL_HOOKS);
    const auto removeCreateStatus = MH_RemoveHook(createTarget);
    const auto removeBindStatus = MH_RemoveHook(bindTarget);
    const auto uninitializeStatus = MH_Uninitialize();
    context->Release();
    device->Release();

    if (!callsObserved) {
        reportFailure("detoured D3D11 calls did not reach both hooks");
        return EXIT_FAILURE;
    }
    if (disableStatus != MH_OK || removeCreateStatus != MH_OK ||
        removeBindStatus != MH_OK || uninitializeStatus != MH_OK) {
        reportFailure("D3D11 method detour cleanup failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
