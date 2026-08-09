#include "render/D3D11Hooks.h"

#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "render/D3D11HookRepairGate.h"
#include "support/Logger.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace community_shaders::render
{
    namespace
    {
        using D3D11CreateDeviceAndSwapChainFunction = HRESULT(WINAPI*)(
            IDXGIAdapter*,
            D3D_DRIVER_TYPE,
            HMODULE,
            UINT,
            const D3D_FEATURE_LEVEL*,
            UINT,
            UINT,
            const DXGI_SWAP_CHAIN_DESC*,
            IDXGISwapChain**,
            ID3D11Device**,
            D3D_FEATURE_LEVEL*,
            ID3D11DeviceContext**);
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
        // Windows SDK 10.0.22621.0 d3d11.h declares 115 entries from
        // IUnknown::QueryInterface through FinishCommandList.
        constexpr std::size_t kDeviceContextVtableEntryCount = 115;

        D3D11CreateDeviceAndSwapChainFunction originalCreateDeviceAndSwapChain{};
        CreatePixelShaderFunction originalCreatePixelShader{};
        PSSetShaderFunction initialPSSetShader{};
        std::atomic<PSSetShaderFunction> downstreamPSSetShader{};
        void** deviceCreationImportCell{};
        void** createPixelShaderCell{};
        void** immediateContextVtableCell{};
        void** initialContextVtable{};
        alignas(void*) std::array<void*, kDeviceContextVtableEntryCount>
            immediateContextVtableShadow{};
        std::atomic_bool deviceCreationImportInstalled{};
        std::atomic_bool deviceCaptured{};
        std::atomic_bool deviceHooksInstalled{};
        std::atomic_uint64_t deviceCreationCalls{};
        std::atomic_uint64_t pixelShaderCreationCalls{};
        std::atomic_uint64_t pixelShaderBindCalls{};
        std::atomic_uint64_t pixelShaderBindRepairs{};
        std::atomic_uint64_t pixelShaderBindRepairFailures{};
        std::atomic_uint64_t pixelShaderBindRecursions{};
        std::atomic_flag pixelShaderBindRepairInProgress = ATOMIC_FLAG_INIT;
        d3d11_hook_repair::State pixelShaderBindRepairState{};
        thread_local bool insidePSSetShaderHook{};

        class AtomicFlagClear final
        {
        public:
            explicit AtomicFlagClear(std::atomic_flag& flag) noexcept :
                flag_(flag)
            {}

            ~AtomicFlagClear()
            {
                flag_.clear(std::memory_order_release);
            }

            AtomicFlagClear(const AtomicFlagClear&) = delete;
            AtomicFlagClear& operator=(const AtomicFlagClear&) = delete;

        private:
            std::atomic_flag& flag_;
        };

        [[nodiscard]] bool isExecutableAddress(const void* address) noexcept
        {
            if (!address) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            if (information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            constexpr DWORD executableProtection =
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & executableProtection) != 0;
        }

        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            if (information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            if (begin < regionBegin || size > UINTPTR_MAX - begin ||
                information.RegionSize > UINTPTR_MAX - regionBegin) {
                return false;
            }
            return begin + size <= regionBegin + information.RegionSize;
        }

        [[nodiscard]] bool patchPointer(
            void** target,
            void* expected,
            void* replacement) noexcept
        {
            if (!target || !expected || !replacement || *target != expected) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    target,
                    sizeof(*target),
                    PAGE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            const auto* observed = InterlockedCompareExchangePointer(
                target,
                replacement,
                expected);
            auto installed = observed == expected;
            DWORD restoredProtection{};
            auto restored = VirtualProtect(
                target,
                sizeof(*target),
                oldProtection,
                &restoredProtection);
            if (installed && restored == FALSE) {
                // Do not report failure while leaving a live hook whose
                // caller may clear its downstream target. The page is still
                // writable here, so atomically roll back before retrying the
                // original protection.
                (void)InterlockedCompareExchangePointer(
                    target,
                    expected,
                    replacement);
                installed = false;
                restored = VirtualProtect(
                    target,
                    sizeof(*target),
                    oldProtection,
                    &restoredProtection);
            }
            FlushInstructionCache(GetCurrentProcess(), target, sizeof(*target));
            return installed && restored != FALSE;
        }

        [[nodiscard]] void* readPointerCell(void** cell) noexcept
        {
            return cell ? ReadPointerAcquire(
                              reinterpret_cast<void* const volatile*>(cell)) :
                          nullptr;
        }

        [[nodiscard]] void** readImmediateContextVtable() noexcept
        {
            return reinterpret_cast<void**>(
                readPointerCell(immediateContextVtableCell));
        }

        [[nodiscard]] std::array<char, MAX_PATH> modulePathForAddress(
            const void* address) noexcept
        {
            std::array<char, MAX_PATH> path{};
            HMODULE module{};
            if (!address || !GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(address),
                    &module)) {
                std::memcpy(path.data(), "<unknown>", 10);
                return path;
            }
            if (GetModuleFileNameA(
                    module,
                    path.data(),
                    static_cast<DWORD>(path.size())) == 0) {
                std::memcpy(path.data(), "<unknown>", 10);
            }
            path.back() = '\0';
            return path;
        }

        [[nodiscard]] void** findMainModuleImport(
            const char* importedModule,
            const char* importedFunction) noexcept
        {
            auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
            if (!image) {
                return nullptr;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
                return nullptr;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                image + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                return nullptr;
            }
            const auto& importDirectory =
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (!importDirectory.VirtualAddress || !importDirectory.Size) {
                return nullptr;
            }

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                image + importDirectory.VirtualAddress);
            for (; descriptor->Name; ++descriptor) {
                const auto* moduleName = reinterpret_cast<const char*>(
                    image + descriptor->Name);
                if (_stricmp(moduleName, importedModule) != 0 ||
                    !descriptor->OriginalFirstThunk ||
                    !descriptor->FirstThunk) {
                    continue;
                }

                auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                    image + descriptor->OriginalFirstThunk);
                auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                    image + descriptor->FirstThunk);
                for (; nameThunk->u1.AddressOfData;
                     ++nameThunk, ++addressThunk) {
                    if (IMAGE_SNAP_BY_ORDINAL64(nameThunk->u1.Ordinal)) {
                        continue;
                    }
                    const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                        image + nameThunk->u1.AddressOfData);
                    if (std::strcmp(
                            reinterpret_cast<const char*>(imported->Name),
                            importedFunction) == 0) {
                        return reinterpret_cast<void**>(&addressThunk->u1.Function);
                    }
                }
            }
            return nullptr;
        }

        HRESULT STDMETHODCALLTYPE hookCreatePixelShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11ClassLinkage* classLinkage,
            ID3D11PixelShader** shader) noexcept
        {
            pixelShaderCreationCalls.fetch_add(1, std::memory_order_relaxed);
            if (!originalCreatePixelShader) {
                return E_UNEXPECTED;
            }
            const auto result = originalCreatePixelShader(
                device,
                bytecode,
                bytecodeLength,
                classLinkage,
                shader);
            if (SUCCEEDED(result) && shader && *shader) {
                linear_lighting::Runtime::get().onPixelShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
            }
            return result;
        }

        void STDMETHODCALLTYPE hookPSSetShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* shader,
            ID3D11ClassInstance* const* classInstances,
            UINT classInstanceCount) noexcept
        {
            pixelShaderBindCalls.fetch_add(1, std::memory_order_relaxed);
            const auto downstream =
                downstreamPSSetShader.load(std::memory_order_acquire);
            if (!downstream) {
                return;
            }
            if (insidePSSetShaderHook) {
                pixelShaderBindRecursions.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (initialPSSetShader &&
                    initialPSSetShader != &hookPSSetShader) {
                    initialPSSetShader(
                        context,
                        shader,
                        classInstances,
                        classInstanceCount);
                }
                return;
            }

            insidePSSetShaderHook = true;
            auto* selected = linear_lighting::Runtime::get().selectPixelShader(
                context,
                shader);
            downstream(
                context,
                selected,
                classInstances,
                classInstanceCount);
            insidePSSetShaderHook = false;
        }

        [[nodiscard]] bool installDeviceVtableHooks(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept
        {
            if (!device || !context) {
                return false;
            }
            auto** deviceVtable = *reinterpret_cast<void***>(device);
            auto** contextVtable = *reinterpret_cast<void***>(context);
            if (!deviceVtable || !contextVtable) {
                return false;
            }
            if (!isReadableRange(
                    contextVtable,
                    immediateContextVtableShadow.size() * sizeof(void*))) {
                logging::error(
                    "D3D11 immediate-context vtable range is not readable; shader hooks remain fail-closed.");
                return false;
            }

            auto* createPixelShader = deviceVtable[kCreatePixelShaderVtableIndex];
            auto* psSetShader = contextVtable[kPSSetShaderVtableIndex];
            if (!isExecutableAddress(createPixelShader) ||
                !isExecutableAddress(psSetShader)) {
                logging::error(
                    "D3D11 vtable identity gate rejected non-executable hook targets.");
                return false;
            }

            originalCreatePixelShader =
                reinterpret_cast<CreatePixelShaderFunction>(createPixelShader);
            initialPSSetShader =
                reinterpret_cast<PSSetShaderFunction>(psSetShader);
            downstreamPSSetShader.store(
                initialPSSetShader,
                std::memory_order_release);
            if (!patchPointer(
                    &deviceVtable[kCreatePixelShaderVtableIndex],
                    createPixelShader,
                    reinterpret_cast<void*>(&hookCreatePixelShader))) {
                originalCreatePixelShader = nullptr;
                initialPSSetShader = nullptr;
                downstreamPSSetShader.store(nullptr, std::memory_order_release);
                logging::error("D3D11 CreatePixelShader vtable patch failed.");
                return false;
            }
            createPixelShaderCell =
                &deviceVtable[kCreatePixelShaderVtableIndex];

            std::copy_n(
                contextVtable,
                immediateContextVtableShadow.size(),
                immediateContextVtableShadow.begin());
            immediateContextVtableShadow[kPSSetShaderVtableIndex] =
                reinterpret_cast<void*>(&hookPSSetShader);
            initialContextVtable = contextVtable;
            immediateContextVtableCell = reinterpret_cast<void**>(context);
            const auto* observed = InterlockedCompareExchangePointer(
                immediateContextVtableCell,
                immediateContextVtableShadow.data(),
                contextVtable);
            if (observed != contextVtable) {
                initialPSSetShader = nullptr;
                downstreamPSSetShader.store(nullptr, std::memory_order_release);
                initialContextVtable = nullptr;
                immediateContextVtableCell = nullptr;
                logging::error(
                    "D3D11 immediate-context vtable shadow installation raced another writer; bind hook remains fail-closed.");
                return false;
            }
            logging::info(
                "Installed isolated D3D11 immediate-context vtable shadow (115 entries, PSSetShader slot 9).");
            return true;
        }

        [[nodiscard]] bool maintainPixelShaderBindHook(
            const char* trigger) noexcept
        {
            if (pixelShaderBindRepairInProgress.test_and_set(
                    std::memory_order_acquire)) {
                return false;
            }
            const AtomicFlagClear clearRepairFlag(
                pixelShaderBindRepairInProgress);

            const auto hookInstalled =
                deviceHooksInstalled.load(std::memory_order_acquire);
            auto** currentVtable = readImmediateContextVtable();
            const auto hookAddress = reinterpret_cast<void*>(&hookPSSetShader);
            const auto shadowOwned =
                currentVtable == immediateContextVtableShadow.data() &&
                readPointerCell(
                    &immediateContextVtableShadow[kPSSetShaderVtableIndex]) ==
                    hookAddress;
            auto* current = isReadableRange(
                                    currentVtable,
                                    (kPSSetShaderVtableIndex + 1) *
                                        sizeof(void*)) ?
                currentVtable[kPSSetShaderVtableIndex] :
                nullptr;
            const auto decision = d3d11_hook_repair::advance(
                pixelShaderBindRepairState,
                hookInstalled,
                shadowOwned,
                reinterpret_cast<std::uintptr_t>(current),
                pixelShaderBindCalls.load(std::memory_order_relaxed));
            if (decision == d3d11_hook_repair::Decision::noAction) {
                return hookInstalled && shadowOwned;
            }

            const auto path = modulePathForAddress(current);
            if (decision ==
                d3d11_hook_repair::Decision::observeDisplacement) {
                logging::warn(
                    "Observed displaced D3D11 immediate-context vtable with PSSetShader at '{}' (trigger={}, target={}); waiting for a second proof boundary before restoring the shadow.",
                    path.data(),
                    trigger ? trigger : "unknown",
                    current);
                return false;
            }
            if (decision ==
                d3d11_hook_repair::Decision::preserveReachableChain) {
                logging::info(
                    "Preserving compatible downstream D3D11 immediate-context chain at '{}' (trigger={}, target={}); bind calls still reach Community Shaders.",
                    path.data(),
                    trigger ? trigger : "unknown",
                    current);
                return true;
            }
            if (currentVtable != initialContextVtable ||
                !isExecutableAddress(current)) {
                pixelShaderBindRepairFailures.fetch_add(
                    1,
                    std::memory_order_relaxed);
                logging::error(
                    "Rejected displaced D3D11 immediate-context vtable at '{}' (trigger={}, vtable={}, target={}): only the verified initial table can be restored.",
                    path.data(),
                    trigger ? trigger : "unknown",
                    reinterpret_cast<void*>(currentVtable),
                    current);
                return false;
            }

            const auto previousDownstream =
                downstreamPSSetShader.exchange(
                    reinterpret_cast<PSSetShaderFunction>(current),
                    std::memory_order_acq_rel);
            const auto* observed = InterlockedCompareExchangePointer(
                immediateContextVtableCell,
                immediateContextVtableShadow.data(),
                currentVtable);
            if (observed != currentVtable) {
                downstreamPSSetShader.store(
                    previousDownstream,
                    std::memory_order_release);
                pixelShaderBindRepairFailures.fetch_add(
                    1,
                    std::memory_order_relaxed);
                logging::error(
                    "Failed to restore D3D11 immediate-context vtable shadow at '{}' (trigger={}, observed={}, expected={}).",
                    path.data(),
                    trigger ? trigger : "unknown",
                    observed,
                    reinterpret_cast<void*>(currentVtable));
                return false;
            }

            const auto repairs = pixelShaderBindRepairs.fetch_add(
                                     1,
                                     std::memory_order_relaxed) +
                1;
            pixelShaderBindRepairState = {};
            logging::warn(
                "Restored D3D11 immediate-context vtable shadow over '{}' (trigger={}, downstream={}, repairs={}).",
                path.data(),
                trigger ? trigger : "unknown",
                current,
                repairs);
            return true;
        }

        HRESULT WINAPI hookCreateDeviceAndSwapChain(
            IDXGIAdapter* adapter,
            D3D_DRIVER_TYPE driverType,
            HMODULE software,
            UINT flags,
            const D3D_FEATURE_LEVEL* featureLevels,
            UINT featureLevelCount,
            UINT sdkVersion,
            const DXGI_SWAP_CHAIN_DESC* swapChainDescription,
            IDXGISwapChain** swapChain,
            ID3D11Device** device,
            D3D_FEATURE_LEVEL* selectedFeatureLevel,
            ID3D11DeviceContext** immediateContext) noexcept
        {
            deviceCreationCalls.fetch_add(1, std::memory_order_relaxed);
            if (!originalCreateDeviceAndSwapChain) {
                return E_UNEXPECTED;
            }
            const auto result = originalCreateDeviceAndSwapChain(
                adapter,
                driverType,
                software,
                flags,
                featureLevels,
                featureLevelCount,
                sdkVersion,
                swapChainDescription,
                swapChain,
                device,
                selectedFeatureLevel,
                immediateContext);
            if (FAILED(result) || !device || !*device ||
                !immediateContext || !*immediateContext) {
                return result;
            }

            auto expectedCapture = false;
            if (!deviceCaptured.compare_exchange_strong(
                    expectedCapture,
                    true,
                    std::memory_order_acq_rel)) {
                logging::info(
                    "Ignored additional Fallout4VR D3D11 device creation; the first captured immediate context retains shader-hook ownership.");
                return result;
            }
            if (!installDeviceVtableHooks(*device, *immediateContext)) {
                logging::error(
                    "D3D11 device captured, but shader hooks remain fail-closed.");
                return result;
            }
            deviceHooksInstalled.store(true, std::memory_order_release);
            linear_lighting::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            logging::info(
                "D3D11 device captured through Fallout4VR's verified creation import; shader hooks installed.");
            return result;
        }
    }

    bool installEarlyD3D11Hooks() noexcept
    {
        try {
            if (deviceCreationImportInstalled.load(std::memory_order_acquire)) {
                return true;
            }
            auto** import = findMainModuleImport(
                "d3d11.dll",
                "D3D11CreateDeviceAndSwapChain");
            const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
            const auto exported = d3d11 ? GetProcAddress(
                d3d11,
                "D3D11CreateDeviceAndSwapChain") : nullptr;
            if (!import || !exported || *import != exported ||
                !isExecutableAddress(exported)) {
                logging::error(
                    "Fallout4VR D3D11 creation import identity gate failed; rendering remains vanilla.");
                return false;
            }

            originalCreateDeviceAndSwapChain =
                reinterpret_cast<D3D11CreateDeviceAndSwapChainFunction>(exported);
            if (!patchPointer(
                    import,
                    exported,
                    reinterpret_cast<void*>(&hookCreateDeviceAndSwapChain))) {
                originalCreateDeviceAndSwapChain = nullptr;
                logging::error(
                    "Fallout4VR D3D11 creation import patch failed; rendering remains vanilla.");
                return false;
            }
            deviceCreationImportCell = import;
            deviceCreationImportInstalled.store(true, std::memory_order_release);
            logging::info(
                "Installed exact D3D11CreateDeviceAndSwapChain import hook.");
            return true;
        } catch (const std::exception& error) {
            logging::error("D3D11 hook installation failed: {}", error.what());
        } catch (...) {
            logging::error(
                "D3D11 hook installation failed with an unknown exception.");
        }
        return false;
    }

    bool maintainD3D11ShaderBindHook(const char* trigger) noexcept
    {
        try {
            return maintainPixelShaderBindHook(trigger);
        } catch (const std::exception& error) {
            pixelShaderBindRepairFailures.fetch_add(
                1,
                std::memory_order_relaxed);
            logging::error(
                "D3D11 PSSetShader ownership maintenance failed: {}",
                error.what());
        } catch (...) {
            pixelShaderBindRepairFailures.fetch_add(
                1,
                std::memory_order_relaxed);
            logging::error(
                "D3D11 PSSetShader ownership maintenance failed with an unknown exception.");
        }
        return false;
    }

    HookSnapshot d3d11HookSnapshot() noexcept
    {
        const auto importInstalled =
            deviceCreationImportInstalled.load(std::memory_order_acquire);
        const auto hooksInstalled =
            deviceHooksInstalled.load(std::memory_order_acquire);
        return {
            .deviceCreationImportInstalled = importInstalled,
            .deviceCreationImportOwned = importInstalled &&
                readPointerCell(deviceCreationImportCell) ==
                    reinterpret_cast<void*>(&hookCreateDeviceAndSwapChain),
            .deviceCaptured = deviceCaptured.load(std::memory_order_acquire),
            .deviceHooksInstalled = hooksInstalled,
            .createPixelShaderCellOwned = hooksInstalled &&
                readPointerCell(createPixelShaderCell) ==
                    reinterpret_cast<void*>(&hookCreatePixelShader),
            .pixelShaderBindCellOwned = hooksInstalled &&
                readImmediateContextVtable() ==
                    immediateContextVtableShadow.data() &&
                readPointerCell(
                    &immediateContextVtableShadow[kPSSetShaderVtableIndex]) ==
                    reinterpret_cast<void*>(&hookPSSetShader),
            .pixelShaderBindRepairs =
                pixelShaderBindRepairs.load(std::memory_order_relaxed),
            .pixelShaderBindRepairFailures =
                pixelShaderBindRepairFailures.load(std::memory_order_relaxed),
            .pixelShaderBindRecursions =
                pixelShaderBindRecursions.load(std::memory_order_relaxed),
            .deviceCreationCalls = deviceCreationCalls.load(std::memory_order_relaxed),
            .pixelShaderCreationCalls =
                pixelShaderCreationCalls.load(std::memory_order_relaxed),
            .pixelShaderBindCalls =
                pixelShaderBindCalls.load(std::memory_order_relaxed),
        };
    }
}
