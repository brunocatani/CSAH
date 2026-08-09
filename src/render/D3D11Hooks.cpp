#include "render/D3D11Hooks.h"

#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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

        struct DetourPatchIdentity
        {
            const std::byte* patchAddress{};
            const void* destination{};
        };

        D3D11CreateDeviceAndSwapChainFunction originalCreateDeviceAndSwapChain{};
        CreatePixelShaderFunction originalCreatePixelShader{};
        PSSetShaderFunction originalPSSetShader{};
        void** deviceCreationImportCell{};
        void* createPixelShaderTarget{};
        void* pixelShaderBindTarget{};
        DetourPatchIdentity createPixelShaderPatch{};
        DetourPatchIdentity pixelShaderBindPatch{};
        std::atomic_bool deviceCreationImportInstalled{};
        std::atomic_bool deviceCaptured{};
        std::atomic_bool deviceHooksInstalled{};
        std::atomic_bool shaderInterceptionActive{};
        std::atomic_bool createPixelShaderDetourEnabled{};
        std::atomic_bool pixelShaderBindDetourEnabled{};
        std::atomic_uint64_t shaderHookInstallFailures{};
        std::atomic_uint64_t shaderHookValidationFailures{};
        std::atomic_uint64_t pixelShaderBindRecursions{};
        std::atomic_uint64_t deviceCreationCalls{};
        std::atomic_uint64_t pixelShaderCreationCalls{};
        std::atomic_uint64_t pixelShaderBindCalls{};
        thread_local bool insidePSSetShaderHook{};

        class RecursionGuard final
        {
        public:
            explicit RecursionGuard(bool& active) noexcept :
                active_(active)
            {
                active_ = true;
            }

            ~RecursionGuard()
            {
                active_ = false;
            }

            RecursionGuard(const RecursionGuard&) = delete;
            RecursionGuard& operator=(const RecursionGuard&) = delete;

        private:
            bool& active_;
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

        [[nodiscard]] HMODULE moduleForAddress(const void* address) noexcept
        {
            HMODULE module{};
            if (!address || !GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(address),
                    &module)) {
                return nullptr;
            }
            return module;
        }

        [[nodiscard]] bool addressBelongsToModule(
            const void* address,
            HMODULE expectedModule) noexcept
        {
            return expectedModule && moduleForAddress(address) == expectedModule;
        }

        [[nodiscard]] std::array<char, MAX_PATH> modulePathForAddress(
            const void* address) noexcept
        {
            std::array<char, MAX_PATH> path{};
            const auto module = moduleForAddress(address);
            if (!module || GetModuleFileNameA(
                               module,
                               path.data(),
                               static_cast<DWORD>(path.size())) == 0) {
                std::memcpy(path.data(), "<unknown>", 10);
            }
            path.back() = '\0';
            return path;
        }

        [[nodiscard]] bool addRelativeDisplacement(
            std::uintptr_t nextInstruction,
            std::int32_t displacement,
            std::uintptr_t& destination) noexcept
        {
            if (displacement >= 0) {
                const auto positive = static_cast<std::uintptr_t>(displacement);
                if (positive >
                    std::numeric_limits<std::uintptr_t>::max() -
                        nextInstruction) {
                    return false;
                }
                destination = nextInstruction + positive;
                return true;
            }
            const auto magnitude = static_cast<std::uintptr_t>(
                -static_cast<std::int64_t>(displacement));
            if (magnitude > nextInstruction) {
                return false;
            }
            destination = nextInstruction - magnitude;
            return true;
        }

        [[nodiscard]] bool captureMinHookPatchIdentity(
            const void* target,
            DetourPatchIdentity& identity) noexcept
        {
            identity = {};
            if (!isReadableRange(target, 5)) {
                return false;
            }

            auto* entry = static_cast<const std::byte*>(target);
            const std::byte* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t shortDisplacement{};
                std::memcpy(&shortDisplacement, entry + 1, sizeof(shortDisplacement));
                if (shortDisplacement != -7) {
                    return false;
                }
                const auto entryAddress = reinterpret_cast<std::uintptr_t>(entry);
                if (entryAddress < 5) {
                    return false;
                }
                patch = reinterpret_cast<const std::byte*>(entryAddress - 5);
                if (!isReadableRange(patch, 5)) {
                    return false;
                }
            }
            if (patch[0] != std::byte{ 0xE9 }) {
                return false;
            }

            std::int32_t displacement{};
            std::memcpy(&displacement, patch + 1, sizeof(displacement));
            const auto patchAddress = reinterpret_cast<std::uintptr_t>(patch);
            std::uintptr_t destinationAddress{};
            if (patchAddress >
                    std::numeric_limits<std::uintptr_t>::max() - 5 ||
                !addRelativeDisplacement(
                    patchAddress + 5,
                    displacement,
                    destinationAddress)) {
                return false;
            }
            const auto* destination =
                reinterpret_cast<const void*>(destinationAddress);
            if (!isExecutableAddress(destination)) {
                return false;
            }
            identity = {
                .patchAddress = patch,
                .destination = destination,
            };
            return true;
        }

        [[nodiscard]] bool detourPatchOwned(
            const void* target,
            const DetourPatchIdentity& expected) noexcept
        {
            DetourPatchIdentity current{};
            return expected.patchAddress && expected.destination &&
                captureMinHookPatchIdentity(target, current) &&
                current.patchAddress == expected.patchAddress &&
                current.destination == expected.destination;
        }

        [[nodiscard]] const char* minHookStatusName(MH_STATUS status) noexcept
        {
            const auto* name = MH_StatusToString(status);
            return name ? name : "MH_UNKNOWN_STATUS";
        }

        [[nodiscard]] bool disableCreatedDetour(void* target) noexcept
        {
            if (!target) {
                return true;
            }
            const auto status = MH_DisableHook(target);
            return status == MH_OK || status == MH_ERROR_DISABLED;
        }

        void rollbackMethodDetours(
            bool createPixelShaderCreated,
            bool pixelShaderBindCreated) noexcept
        {
            shaderInterceptionActive.store(false, std::memory_order_release);
            const auto createDisabled = !createPixelShaderCreated ||
                disableCreatedDetour(createPixelShaderTarget);
            const auto bindDisabled = !pixelShaderBindCreated ||
                disableCreatedDetour(pixelShaderBindTarget);
            if (!createDisabled || !bindDisabled) {
                logging::critical(
                    "D3D11 detour rollback could not prove both hooks disabled; resident hooks remain strict pass-through for process lifetime.");
                return;
            }

            if (createPixelShaderCreated) {
                (void)MH_RemoveHook(createPixelShaderTarget);
            }
            if (pixelShaderBindCreated) {
                (void)MH_RemoveHook(pixelShaderBindTarget);
            }
            (void)MH_Uninitialize();
            originalCreatePixelShader = nullptr;
            originalPSSetShader = nullptr;
            createPixelShaderTarget = nullptr;
            pixelShaderBindTarget = nullptr;
            createPixelShaderPatch = {};
            pixelShaderBindPatch = {};
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
                    const auto* imported =
                        reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                            image + nameThunk->u1.AddressOfData);
                    if (std::strcmp(
                            reinterpret_cast<const char*>(imported->Name),
                            importedFunction) == 0) {
                        return reinterpret_cast<void**>(
                            &addressThunk->u1.Function);
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
            const auto original = originalCreatePixelShader;
            if (!original) {
                return E_UNEXPECTED;
            }
            const auto result = original(
                device,
                bytecode,
                bytecodeLength,
                classLinkage,
                shader);
            if (shaderInterceptionActive.load(std::memory_order_acquire) &&
                SUCCEEDED(result) && shader && *shader) {
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
            const auto original = originalPSSetShader;
            if (!original) {
                return;
            }
            if (!shaderInterceptionActive.load(std::memory_order_acquire)) {
                original(context, shader, classInstances, classInstanceCount);
                return;
            }
            if (insidePSSetShaderHook) {
                pixelShaderBindRecursions.fetch_add(1, std::memory_order_relaxed);
                original(context, shader, classInstances, classInstanceCount);
                return;
            }

            const RecursionGuard recursionGuard(insidePSSetShaderHook);
            auto* selected = linear_lighting::Runtime::get().selectPixelShader(
                context,
                shader);
            original(
                context,
                selected,
                classInstances,
                classInstanceCount);
        }

        [[nodiscard]] bool installDeviceMethodDetours(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept
        {
            if (!device || !context) {
                return false;
            }
            auto** deviceVtable = *reinterpret_cast<void***>(device);
            auto** contextVtable = *reinterpret_cast<void***>(context);
            if (!isReadableRange(
                    deviceVtable,
                    (kCreatePixelShaderVtableIndex + 1) * sizeof(void*)) ||
                !isReadableRange(
                    contextVtable,
                    (kPSSetShaderVtableIndex + 1) * sizeof(void*))) {
                logging::error(
                    "D3D11 method tables failed the readable-range gate; shader interception remains vanilla.");
                return false;
            }

            createPixelShaderTarget =
                deviceVtable[kCreatePixelShaderVtableIndex];
            pixelShaderBindTarget = contextVtable[kPSSetShaderVtableIndex];
            const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
            if (!isExecutableAddress(createPixelShaderTarget) ||
                !isExecutableAddress(pixelShaderBindTarget) ||
                !addressBelongsToModule(createPixelShaderTarget, d3d11) ||
                !addressBelongsToModule(pixelShaderBindTarget, d3d11)) {
                const auto createPath =
                    modulePathForAddress(createPixelShaderTarget);
                const auto bindPath = modulePathForAddress(pixelShaderBindTarget);
                logging::error(
                    "D3D11 method identity gate rejected shader targets (CreatePixelShader='{}' {}, PSSetShader='{}' {}); interception remains vanilla.",
                    createPath.data(),
                    createPixelShaderTarget,
                    bindPath.data(),
                    pixelShaderBindTarget);
                return false;
            }

            auto status = MH_Initialize();
            if (status != MH_OK) {
                logging::error(
                    "MinHook initialization failed: {} ({}).",
                    minHookStatusName(status),
                    static_cast<int>(status));
                return false;
            }

            bool createPixelShaderCreated{};
            bool pixelShaderBindCreated{};
            void* createPixelShaderTrampoline{};
            status = MH_CreateHook(
                createPixelShaderTarget,
                reinterpret_cast<void*>(&hookCreatePixelShader),
                &createPixelShaderTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(createPixelShaderTrampoline)) {
                logging::error(
                    "CreatePixelShader detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    createPixelShaderTrampoline);
                rollbackMethodDetours(false, false);
                return false;
            }
            createPixelShaderCreated = true;
            originalCreatePixelShader =
                reinterpret_cast<CreatePixelShaderFunction>(
                    createPixelShaderTrampoline);

            void* pixelShaderBindTrampoline{};
            status = MH_CreateHook(
                pixelShaderBindTarget,
                reinterpret_cast<void*>(&hookPSSetShader),
                &pixelShaderBindTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(pixelShaderBindTrampoline)) {
                logging::error(
                    "PSSetShader detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    pixelShaderBindTrampoline);
                rollbackMethodDetours(createPixelShaderCreated, false);
                return false;
            }
            pixelShaderBindCreated = true;
            originalPSSetShader = reinterpret_cast<PSSetShaderFunction>(
                pixelShaderBindTrampoline);

            const auto createQueueStatus =
                MH_QueueEnableHook(createPixelShaderTarget);
            const auto bindQueueStatus =
                MH_QueueEnableHook(pixelShaderBindTarget);
            if (createQueueStatus != MH_OK || bindQueueStatus != MH_OK) {
                logging::error(
                    "D3D11 detour queue failed: CreatePixelShader={} ({}), PSSetShader={} ({}).",
                    minHookStatusName(createQueueStatus),
                    static_cast<int>(createQueueStatus),
                    minHookStatusName(bindQueueStatus),
                    static_cast<int>(bindQueueStatus));
                rollbackMethodDetours(
                    createPixelShaderCreated,
                    pixelShaderBindCreated);
                return false;
            }

            status = MH_ApplyQueued();
            if (status != MH_OK ||
                !captureMinHookPatchIdentity(
                    createPixelShaderTarget,
                    createPixelShaderPatch) ||
                !captureMinHookPatchIdentity(
                    pixelShaderBindTarget,
                    pixelShaderBindPatch)) {
                logging::error(
                    "D3D11 detour activation/ownership validation failed: {} ({}).",
                    minHookStatusName(status),
                    static_cast<int>(status));
                rollbackMethodDetours(
                    createPixelShaderCreated,
                    pixelShaderBindCreated);
                return false;
            }

            createPixelShaderDetourEnabled.store(true, std::memory_order_release);
            pixelShaderBindDetourEnabled.store(true, std::memory_order_release);
            logging::info(
                "Installed validated d3d11.dll method detours (CreatePixelShader target={}, PSSetShader target={}); complete native COM vtables remain untouched.",
                createPixelShaderTarget,
                pixelShaderBindTarget);
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
                    "Ignored additional Fallout4VR D3D11 device creation; the first captured device retains shader-hook ownership.");
                return result;
            }
            if (!installDeviceMethodDetours(*device, *immediateContext)) {
                shaderHookInstallFailures.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "D3D11 device captured, but method detours remain fail-closed and rendering stays vanilla.");
                return result;
            }

            linear_lighting::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            shaderInterceptionActive.store(true, std::memory_order_release);
            deviceHooksInstalled.store(true, std::memory_order_release);
            logging::info(
                "D3D11 device captured through Fallout4VR's verified creation import; shader interception is active.");
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

    bool validateD3D11ShaderHooks(const char* trigger) noexcept
    {
        try {
            const auto active =
                shaderInterceptionActive.load(std::memory_order_acquire);
            const auto createOwned = active && detourPatchOwned(
                createPixelShaderTarget,
                createPixelShaderPatch);
            const auto bindOwned = active && detourPatchOwned(
                pixelShaderBindTarget,
                pixelShaderBindPatch);
            createPixelShaderDetourEnabled.store(
                createOwned,
                std::memory_order_release);
            pixelShaderBindDetourEnabled.store(
                bindOwned,
                std::memory_order_release);
            if (createOwned && bindOwned) {
                return true;
            }

            const auto failures = shaderHookValidationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if (failures == 1 || (failures & (failures - 1)) == 0) {
                logging::error(
                    "D3D11 shader detour ownership validation failed (trigger={}, active={}, createOwned={}, bindOwned={}, failures={}); no hook repair was attempted.",
                    trigger ? trigger : "unknown",
                    active,
                    createOwned,
                    bindOwned,
                    failures);
            }
            return false;
        } catch (const std::exception& error) {
            shaderHookValidationFailures.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "D3D11 shader detour validation failed: {}",
                error.what());
        } catch (...) {
            shaderHookValidationFailures.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "D3D11 shader detour validation failed with an unknown exception.");
        }
        return false;
    }

    HookSnapshot d3d11HookSnapshot() noexcept
    {
        const auto importInstalled =
            deviceCreationImportInstalled.load(std::memory_order_acquire);
        const auto active =
            shaderInterceptionActive.load(std::memory_order_acquire);
        return {
            .deviceCreationImportInstalled = importInstalled,
            .deviceCreationImportOwned = importInstalled &&
                readPointerCell(deviceCreationImportCell) ==
                    reinterpret_cast<void*>(&hookCreateDeviceAndSwapChain),
            .deviceCaptured = deviceCaptured.load(std::memory_order_acquire),
            .deviceHooksInstalled =
                deviceHooksInstalled.load(std::memory_order_acquire),
            .shaderInterceptionActive = active,
            .createPixelShaderDetourEnabled =
                createPixelShaderDetourEnabled.load(std::memory_order_acquire),
            .pixelShaderBindDetourEnabled =
                pixelShaderBindDetourEnabled.load(std::memory_order_acquire),
            .shaderHookInstallFailures =
                shaderHookInstallFailures.load(std::memory_order_relaxed),
            .shaderHookValidationFailures =
                shaderHookValidationFailures.load(std::memory_order_relaxed),
            .pixelShaderBindRecursions =
                pixelShaderBindRecursions.load(std::memory_order_relaxed),
            .deviceCreationCalls =
                deviceCreationCalls.load(std::memory_order_relaxed),
            .pixelShaderCreationCalls =
                pixelShaderCreationCalls.load(std::memory_order_relaxed),
            .pixelShaderBindCalls =
                pixelShaderBindCalls.load(std::memory_order_relaxed),
        };
    }
}
