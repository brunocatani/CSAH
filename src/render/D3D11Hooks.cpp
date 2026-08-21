#include "render/D3D11Hooks.h"

#include "Features/basic_wetness/BasicWetnessRuntime.h"
#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/dlaa/DlaaD3D11Hooks.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/StreamlineBackend.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include "Features/hair_specular/HairSpecularRuntime.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/linear_lighting/DFTiledPointLightHook.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/skylighting/SkylightingNativeHooks.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "Features/vanilla_fixes/FocusShadowRuntime.h"
#include "Features/vanilla_fixes/VanillaFixesRuntime.h"
#include "Features/vanilla_fixes/VanillaShaderFixes.h"
#include "Features/wrapped_grass/WrappedGrassRuntime.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <intrin.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

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
        using CreateVertexShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11VertexShader**);
        using CreateComputeShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11ComputeShader**);
        using PSSetShaderFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*,
            ID3D11PixelShader*,
            ID3D11ClassInstance* const*,
            UINT);
        using VSSetShaderFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*,
            ID3D11VertexShader*,
            ID3D11ClassInstance* const*,
            UINT);
        using OMSetRenderTargetsFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*,
            UINT,
            ID3D11RenderTargetView* const*,
            ID3D11DepthStencilView*);
        using OMSetRenderTargetsAndUnorderedAccessViewsFunction =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                ID3D11RenderTargetView* const*,
                ID3D11DepthStencilView*,
                UINT,
                UINT,
                ID3D11UnorderedAccessView* const*,
                const UINT*);
        using DrawIndexedFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*, UINT, UINT, INT);
        using DrawFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*, UINT, UINT);
        using DrawIndexedInstancedFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
        using DrawInstancedFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*, UINT, UINT, UINT, UINT);

        constexpr std::size_t kCreateVertexShaderVtableIndex = 12;
        constexpr std::size_t kCreatePixelShaderVtableIndex = 15;
        constexpr std::size_t kCreateComputeShaderVtableIndex = 18;
        constexpr std::size_t kPSSetShaderVtableIndex = 9;
        constexpr std::size_t kVSSetShaderVtableIndex = 11;
        constexpr std::size_t kOMSetRenderTargetsVtableIndex = 33;
        constexpr std::size_t
            kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex = 34;
        constexpr std::size_t kDrawIndexedVtableIndex = 12;
        constexpr std::size_t kDrawVtableIndex = 13;
        constexpr std::size_t kDrawIndexedInstancedVtableIndex = 20;
        constexpr std::size_t kDrawInstancedVtableIndex = 21;

        enum class ColorDomainProbeCategory : std::uint8_t
        {
            materialBase,
            materialGradientRemap,
            materialBoneTint,
            materialScreen,
            materialDismemberment,
            materialMeatCuff,
            materialLandscapeFourLayer,
            materialLandscapeLod,
            distantTree,
            particle0,
            particle1,
            particle2,
            particle3,
            count,
        };

        constexpr auto kColorDomainProbeCategoryCount =
            static_cast<std::size_t>(ColorDomainProbeCategory::count);
        constexpr std::array<UINT, 1> kBaseColorSlots{ 0 };
        constexpr std::array<UINT, 1> kGradientRemapColorSlots{ 5 };
        constexpr std::array<UINT, 2> kBoneTintColorSlots{ 13, 14 };
        constexpr std::array<UINT, 1> kScreenColorSlots{ 4 };
        constexpr std::array<UINT, 2> kSpecialDiffuseColorSlots{ 0, 9 };
        constexpr std::array<UINT, 3> kLandscapeFourLayerColorSlots{
            0, 3, 13
        };
        constexpr std::array<UINT, 2> kLandscapeLodColorSlots{ 0, 13 };
        constexpr std::array<UINT, 2> kParticleColorSlots{ 0, 1 };
        constexpr std::array<const char*, 4> kParticleColorDomainLabels{
            "particle-0", "particle-1", "particle-2", "particle-3"
        };

        struct TextureBindingDescription
        {
            DXGI_FORMAT viewFormat{ DXGI_FORMAT_UNKNOWN };
            DXGI_FORMAT textureFormat{ DXGI_FORMAT_UNKNOWN };
            UINT viewDimension{};
            UINT width{};
            UINT height{};
            UINT arraySize{};
            UINT sampleCount{};
            bool present{};
        };

        struct DetourPatchIdentity
        {
            const std::byte* patchAddress{};
            const void* destination{};
        };

        D3D11CreateDeviceAndSwapChainFunction originalCreateDeviceAndSwapChain{};
        CreateVertexShaderFunction originalCreateVertexShader{};
        CreatePixelShaderFunction originalCreatePixelShader{};
        CreateComputeShaderFunction originalCreateComputeShader{};
        VSSetShaderFunction originalVSSetShader{};
        PSSetShaderFunction originalPSSetShader{};
        OMSetRenderTargetsFunction originalOMSetRenderTargets{};
        OMSetRenderTargetsAndUnorderedAccessViewsFunction
            originalOMSetRenderTargetsAndUnorderedAccessViews{};
        DrawIndexedFunction originalDrawIndexed{};
        DrawFunction originalDraw{};
        DrawIndexedInstancedFunction originalDrawIndexedInstanced{};
        DrawInstancedFunction originalDrawInstanced{};
        void** deviceCreationImportCell{};
        void* createVertexShaderTarget{};
        void* createPixelShaderTarget{};
        void* createComputeShaderTarget{};
        void* vertexShaderBindTarget{};
        void* pixelShaderBindTarget{};
        void* renderTargetBindTarget{};
        void* renderTargetAndUnorderedAccessBindTarget{};
        void* drawIndexedTarget{};
        void* drawTarget{};
        void* drawIndexedInstancedTarget{};
        void* drawInstancedTarget{};
        DetourPatchIdentity createVertexShaderPatch{};
        DetourPatchIdentity createPixelShaderPatch{};
        DetourPatchIdentity createComputeShaderPatch{};
        DetourPatchIdentity vertexShaderBindPatch{};
        DetourPatchIdentity pixelShaderBindPatch{};
        DetourPatchIdentity renderTargetBindPatch{};
        DetourPatchIdentity renderTargetAndUnorderedAccessBindPatch{};
        DetourPatchIdentity drawIndexedPatch{};
        DetourPatchIdentity drawPatch{};
        DetourPatchIdentity drawIndexedInstancedPatch{};
        DetourPatchIdentity drawInstancedPatch{};
        std::atomic_bool deviceCreationImportInstalled{};
        std::atomic_bool deviceCaptured{};
        std::atomic_bool deviceHooksInstalled{};
        std::atomic_bool shaderInterceptionActive{};
        std::atomic_bool createVertexShaderDetourEnabled{};
        std::atomic_bool createPixelShaderDetourEnabled{};
        std::atomic_bool createComputeShaderDetourEnabled{};
        std::atomic_bool vertexShaderBindDetourEnabled{};
        std::atomic_bool pixelShaderBindDetourEnabled{};
        std::atomic_bool renderTargetBindDetourEnabled{};
        std::atomic_bool renderTargetAndUnorderedAccessBindDetourEnabled{};
        std::atomic_bool qualificationDrawDetoursInstalled{};
        std::atomic_bool qualificationDrawDetoursOwned{};
        std::atomic_uint64_t shaderHookInstallFailures{};
        std::atomic_uint64_t shaderHookValidationFailures{};
        std::atomic_uint64_t qualificationDrawHookInstallFailures{};
        std::atomic_uint64_t qualificationDrawHookValidationFailures{};
        std::atomic_uint64_t pixelShaderBindRecursions{};
        std::atomic_uint64_t deviceCreationCalls{};
        std::atomic_uint64_t vertexShaderCreationCalls{};
        std::atomic_uint64_t pixelShaderCreationCalls{};
        std::atomic_uint64_t computeShaderCreationCalls{};
        std::atomic_uint64_t vertexShaderBindCalls{};
        std::atomic_uint64_t pixelShaderBindCalls{};
        std::atomic_uint64_t renderTargetBindCalls{};
        std::atomic_uint64_t renderTargetAndUnorderedAccessBindCalls{};
        thread_local bool activeFocusShadowPixel{};
        std::atomic_bool firstTrackedContactShaderBindLogged{};
        std::atomic_bool firstTerrainDrawCallerLogged{};
        std::atomic_bool firstDFPrePassDescriptorConsumeLogged{};
        std::atomic_bool firstDFPrePassDescriptorExpiryLogged{};
        std::atomic_bool qualificationSessionActive{};
        std::atomic_uint64_t qualificationSessionId{};
        std::atomic_uint64_t qualificationActivatedSessionId{};
        std::atomic_flag qualificationActivationGate = ATOMIC_FLAG_INIT;
        std::atomic_uint64_t qualificationGeometryUpdateBaseline{};
        std::atomic_uint64_t qualificationReplacementShaderBinds{};
        std::atomic_uint64_t qualificationDrawIndexedCalls{};
        std::atomic_uint64_t qualificationDrawCalls{};
        std::atomic_uint64_t qualificationDrawIndexedInstancedCalls{};
        std::atomic_uint64_t qualificationDrawInstancedCalls{};
        std::atomic_uint64_t qualificationReplacementDrawCalls{};
        std::atomic_uint64_t qualificationBindingStateChecks{};
        std::atomic_uint64_t qualificationBindingStateFailures{};
        std::atomic_uint64_t qualificationDrawStateChecks{};
        std::atomic_uint64_t qualificationDrawStateFailures{};
        std::atomic_uint64_t qualificationBindingsWithoutFreshGeometry{};
        std::atomic_uint64_t qualificationDrawsWithoutFreshGeometry{};
        linear_lighting::AtomicContractMask
            qualificationReplacementContractMask{};
        linear_lighting::AtomicContractMask
            qualificationBindingVerifiedContractMask{};
        linear_lighting::AtomicContractMask
            qualificationDrawVerifiedContractMask{};
        std::array<std::atomic_bool, kColorDomainProbeCategoryCount>
            qualificationColorDomainLogged{};
        std::atomic_uint32_t qualificationLastBindingState{};
        std::atomic_uint32_t qualificationLastDrawState{};
        thread_local bool insidePSSetShaderHook{};
        thread_local std::uint64_t activeQualificationSessionId{};
        thread_local linear_lighting::ReplacementShaderBinding
            activeReplacementBinding{};
        thread_local std::uint32_t activeSurfaceClassCode{};
        thread_local bool activeGrassVertexShader{};
        thread_local ID3D11PixelShader* activeReplacementOriginal{};
        thread_local ID3D11DeviceContext* activeReplacementContext{};
        struct PendingDFPrePassDescriptor
        {
            std::uint32_t descriptor{};
            bool active{};
        };
        constexpr std::size_t kMaxDFPrePassTechniqueDepth = 8;
        struct ActiveDFPrePassTechniques
        {
            std::array<std::uint32_t, kMaxDFPrePassTechniqueDepth>
                descriptors{};
            std::size_t depth{};
        };
        thread_local ActiveDFPrePassTechniques activeDFPrePassTechniques{};
        thread_local PendingDFPrePassDescriptor pendingDFPrePassDescriptor{};
        std::atomic_bool firstDFPrePassTechniqueOverflowLogged{};
        std::atomic_bool firstDFPrePassTechniqueMismatchLogged{};
        std::atomic_bool firstGrassVertexDrawRebindLogged{};
        thread_local contact_shadows::ShaderBinding
            activeContactShadowBinding{};
        thread_local filmic_tonemapping::ShaderBinding
            activeFilmicTonemappingBinding{};
        thread_local bool activeContactShadowsEnabled{};
        thread_local bool activeWrappedGrassEnabled{};
        thread_local bool activeHairSpecularEnabled{};
        thread_local bool activeSubsurfaceScatteringEnabled{};
        thread_local bool activeBasicWetnessEnabled{};
        thread_local bool activeCloudShadowsEnabled{};
        thread_local ibl::Runtime::MaterialShaderBinding
            activeIblMaterialBinding{};
        thread_local ibl::CaptureProbePassState activeIblCaptureProbePass{};

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
            bool createVertexShaderCreated,
            bool createPixelShaderCreated,
            bool vertexShaderBindCreated,
            bool pixelShaderBindCreated,
            bool renderTargetBindCreated,
            bool renderTargetAndUnorderedAccessBindCreated) noexcept
        {
            shaderInterceptionActive.store(false, std::memory_order_release);
            const auto createVertexDisabled = !createVertexShaderCreated ||
                disableCreatedDetour(createVertexShaderTarget);
            const auto createDisabled = !createPixelShaderCreated ||
                disableCreatedDetour(createPixelShaderTarget);
            const auto vertexBindDisabled = !vertexShaderBindCreated ||
                disableCreatedDetour(vertexShaderBindTarget);
            const auto bindDisabled = !pixelShaderBindCreated ||
                disableCreatedDetour(pixelShaderBindTarget);
            const auto renderTargetDisabled = !renderTargetBindCreated ||
                disableCreatedDetour(renderTargetBindTarget);
            const auto renderTargetAndUnorderedAccessDisabled =
                !renderTargetAndUnorderedAccessBindCreated ||
                disableCreatedDetour(
                    renderTargetAndUnorderedAccessBindTarget);
            if (!createVertexDisabled || !createDisabled ||
                !vertexBindDisabled || !bindDisabled || !renderTargetDisabled ||
                !renderTargetAndUnorderedAccessDisabled) {
                logging::critical(
                    "D3D11 detour rollback could not prove all hooks disabled; resident hooks remain strict pass-through for process lifetime.");
                return;
            }

            if (createVertexShaderCreated) {
                (void)MH_RemoveHook(createVertexShaderTarget);
            }
            if (createPixelShaderCreated) {
                (void)MH_RemoveHook(createPixelShaderTarget);
            }
            if (vertexShaderBindCreated) {
                (void)MH_RemoveHook(vertexShaderBindTarget);
            }
            if (pixelShaderBindCreated) {
                (void)MH_RemoveHook(pixelShaderBindTarget);
            }
            if (renderTargetBindCreated) {
                (void)MH_RemoveHook(renderTargetBindTarget);
            }
            if (renderTargetAndUnorderedAccessBindCreated) {
                (void)MH_RemoveHook(renderTargetAndUnorderedAccessBindTarget);
            }
            (void)MH_Uninitialize();
            originalCreateVertexShader = nullptr;
            originalCreatePixelShader = nullptr;
            originalCreateComputeShader = nullptr;
            originalVSSetShader = nullptr;
            originalPSSetShader = nullptr;
            originalOMSetRenderTargets = nullptr;
            originalOMSetRenderTargetsAndUnorderedAccessViews = nullptr;
            createVertexShaderTarget = nullptr;
            createPixelShaderTarget = nullptr;
            createComputeShaderTarget = nullptr;
            vertexShaderBindTarget = nullptr;
            pixelShaderBindTarget = nullptr;
            renderTargetBindTarget = nullptr;
            renderTargetAndUnorderedAccessBindTarget = nullptr;
            createVertexShaderPatch = {};
            createPixelShaderPatch = {};
            createComputeShaderPatch = {};
            vertexShaderBindPatch = {};
            pixelShaderBindPatch = {};
            renderTargetBindPatch = {};
            renderTargetAndUnorderedAccessBindPatch = {};
        }

        void rollbackQualificationDrawDetours(
            bool drawIndexedCreated,
            bool drawCreated,
            bool drawIndexedInstancedCreated,
            bool drawInstancedCreated) noexcept
        {
            qualificationSessionActive.store(false, std::memory_order_release);
            const std::array<std::pair<void*, bool>, 4> hooks{ {
                { drawIndexedTarget, drawIndexedCreated },
                { drawTarget, drawCreated },
                { drawIndexedInstancedTarget, drawIndexedInstancedCreated },
                { drawInstancedTarget, drawInstancedCreated },
            } };
            for (const auto& [target, created] : hooks) {
                if (created && target) {
                    (void)disableCreatedDetour(target);
                    (void)MH_RemoveHook(target);
                }
            }
            originalDrawIndexed = nullptr;
            originalDraw = nullptr;
            originalDrawIndexedInstanced = nullptr;
            originalDrawInstanced = nullptr;
            drawIndexedTarget = nullptr;
            drawTarget = nullptr;
            drawIndexedInstancedTarget = nullptr;
            drawInstancedTarget = nullptr;
            drawIndexedPatch = {};
            drawPatch = {};
            drawIndexedInstancedPatch = {};
            drawInstancedPatch = {};
            qualificationDrawDetoursInstalled.store(
                false,
                std::memory_order_release);
            qualificationDrawDetoursOwned.store(false, std::memory_order_release);
        }

        void resetQualificationCounters() noexcept
        {
            qualificationReplacementShaderBinds.store(0, std::memory_order_relaxed);
            qualificationDrawIndexedCalls.store(0, std::memory_order_relaxed);
            qualificationDrawCalls.store(0, std::memory_order_relaxed);
            qualificationDrawIndexedInstancedCalls.store(0, std::memory_order_relaxed);
            qualificationDrawInstancedCalls.store(0, std::memory_order_relaxed);
            qualificationReplacementDrawCalls.store(0, std::memory_order_relaxed);
            qualificationBindingStateChecks.store(0, std::memory_order_relaxed);
            qualificationBindingStateFailures.store(0, std::memory_order_relaxed);
            qualificationDrawStateChecks.store(0, std::memory_order_relaxed);
            qualificationDrawStateFailures.store(0, std::memory_order_relaxed);
            qualificationBindingsWithoutFreshGeometry.store(0, std::memory_order_relaxed);
            qualificationDrawsWithoutFreshGeometry.store(0, std::memory_order_relaxed);
            qualificationReplacementContractMask.clear(
                std::memory_order_relaxed);
            qualificationBindingVerifiedContractMask.clear(
                std::memory_order_relaxed);
            qualificationDrawVerifiedContractMask.clear(
                std::memory_order_relaxed);
            for (auto& logged : qualificationColorDomainLogged) {
                logged.store(false, std::memory_order_relaxed);
            }
            qualificationLastBindingState.store(0, std::memory_order_relaxed);
            qualificationLastDrawState.store(0, std::memory_order_relaxed);
        }

        void activatePendingQualificationSession() noexcept
        {
            const auto requested =
                qualificationSessionId.load(std::memory_order_acquire);
            if (requested == 0 || requested ==
                    qualificationActivatedSessionId.load(
                        std::memory_order_acquire)) {
                return;
            }
            if (qualificationActivationGate.test_and_set(
                    std::memory_order_acquire)) {
                return;
            }

            if (requested != qualificationActivatedSessionId.load(
                                 std::memory_order_relaxed)) {
                qualificationSessionActive.store(
                    false,
                    std::memory_order_release);
                resetQualificationCounters();
                qualificationActivatedSessionId.store(
                    requested,
                    std::memory_order_release);
                activeQualificationSessionId = requested;
                activeReplacementBinding = {};
                activeSurfaceClassCode = 0;
                activeIblCaptureProbePass = {};
                qualificationSessionActive.store(
                    true,
                    std::memory_order_release);
            }
            qualificationActivationGate.clear(std::memory_order_release);
        }

        [[nodiscard]] linear_lighting::ContractBit qualificationContractBit(
            linear_lighting::ReplacementShaderBinding binding) noexcept
        {
            if (binding.family !=
                linear_lighting::ReplacementShaderFamily::material) {
                return {};
            }
            const auto contractPlusOne = binding.contractPlusOne;
            return contractPlusOne > 0 &&
                    contractPlusOne <=
                        linear_lighting::Runtime::kShaderContractCount ?
                linear_lighting::contractBit(contractPlusOne - 1) :
                linear_lighting::ContractBit{};
        }

        [[nodiscard]] TextureBindingDescription describeShaderResource(
            ID3D11ShaderResourceView* view) noexcept
        {
            TextureBindingDescription result{};
            if (!view) {
                return result;
            }
            result.present = true;
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
            view->GetDesc(&viewDescription);
            result.viewFormat = viewDescription.Format;
            result.viewDimension =
                static_cast<UINT>(viewDescription.ViewDimension);

            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return result;
            }
            D3D11_TEXTURE2D_DESC textureDescription{};
            texture->GetDesc(&textureDescription);
            result.textureFormat = textureDescription.Format;
            result.width = textureDescription.Width;
            result.height = textureDescription.Height;
            result.arraySize = textureDescription.ArraySize;
            result.sampleCount = textureDescription.SampleDesc.Count;
            return result;
        }

        [[nodiscard]] TextureBindingDescription describeRenderTarget(
            ID3D11RenderTargetView* view) noexcept
        {
            TextureBindingDescription result{};
            if (!view) {
                return result;
            }
            result.present = true;
            D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
            view->GetDesc(&viewDescription);
            result.viewFormat = viewDescription.Format;
            result.viewDimension =
                static_cast<UINT>(viewDescription.ViewDimension);

            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return result;
            }
            D3D11_TEXTURE2D_DESC textureDescription{};
            texture->GetDesc(&textureDescription);
            result.textureFormat = textureDescription.Format;
            result.width = textureDescription.Width;
            result.height = textureDescription.Height;
            result.arraySize = textureDescription.ArraySize;
            result.sampleCount = textureDescription.SampleDesc.Count;
            return result;
        }

        template <std::size_t SlotCount>
        void logColorDomainBindings(
            ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding,
            const char* category,
            const char* contractName,
            const std::array<UINT, SlotCount>& slots) noexcept
        {
            ID3D11RenderTargetView* rawRenderTarget{};
            ID3D11DepthStencilView* rawDepthStencil{};
            context->OMGetRenderTargets(
                1,
                &rawRenderTarget,
                &rawDepthStencil);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
            renderTarget.Attach(rawRenderTarget);
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencil;
            depthStencil.Attach(rawDepthStencil);

            const auto output = describeRenderTarget(renderTarget.Get());
            logging::info(
                "Linear Lighting color-domain probe '{}': family={}, contract={}, name='{}', OM-RT0(present={}, viewFormat={}, textureFormat={}, viewDimension={}, extent={}x{}, array={}, samples={}); bindings observed only, image unchanged.",
                category,
                static_cast<unsigned>(binding.family),
                binding.contractPlusOne,
                contractName,
                output.present,
                static_cast<unsigned>(output.viewFormat),
                static_cast<unsigned>(output.textureFormat),
                output.viewDimension,
                output.width,
                output.height,
                output.arraySize,
                output.sampleCount);

            for (const auto slot : slots) {
                ID3D11ShaderResourceView* rawView{};
                context->PSGetShaderResources(slot, 1, &rawView);
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
                view.Attach(rawView);
                const auto input = describeShaderResource(view.Get());
                logging::info(
                    "Linear Lighting color-domain probe '{}': PS-t{}(present={}, viewFormat={}, textureFormat={}, viewDimension={}, extent={}x{}, array={}, samples={}).",
                    category,
                    slot,
                    input.present,
                    static_cast<unsigned>(input.viewFormat),
                    static_cast<unsigned>(input.textureFormat),
                    input.viewDimension,
                    input.width,
                    input.height,
                    input.arraySize,
                    input.sampleCount);
            }
        }

        template <std::size_t SlotCount>
        void tryObserveColorDomain(
            ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding,
            ColorDomainProbeCategory probe,
            const char* category,
            const char* contractName,
            const std::array<UINT, SlotCount>& slots) noexcept
        {
            const auto probeIndex = static_cast<std::size_t>(probe);
            if (probeIndex >= qualificationColorDomainLogged.size() ||
                qualificationColorDomainLogged[probeIndex].exchange(
                    true,
                    std::memory_order_acq_rel)) {
                return;
            }
            logColorDomainBindings(
                context, binding, category, contractName, slots);
        }

        void observeLinearLightingColorDomains(
            ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding) noexcept
        {
            if (!context || binding.contractPlusOne == 0) {
                return;
            }

            if (binding.family ==
                linear_lighting::ReplacementShaderFamily::material) {
                if (binding.contractPlusOne >
                    linear_lighting::Runtime::kShaderContractCount) {
                    return;
                }
                const auto* name = linear_lighting::Runtime::shaderContractName(
                    binding.contractPlusOne - 1);
                tryObserveColorDomain(
                    context,
                    binding,
                    ColorDomainProbeCategory::materialBase,
                    "material-base",
                    name,
                    kBaseColorSlots);
                if (std::strstr(name, "GradientRemap")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialGradientRemap,
                        "material-gradient-remap",
                        name,
                        kGradientRemapColorSlots);
                }
                if (std::strstr(name, "BoneTint")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialBoneTint,
                        "material-bone-tint",
                        name,
                        kBoneTintColorSlots);
                }
                if (std::strstr(name, "Screen")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialScreen,
                        "material-screen",
                        name,
                        kScreenColorSlots);
                }
                if (std::strstr(name, "Dismemberment")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialDismemberment,
                        "material-dismemberment",
                        name,
                        kSpecialDiffuseColorSlots);
                }
                if (std::strstr(name, "MeatCuff")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialMeatCuff,
                        "material-meat-cuff",
                        name,
                        kSpecialDiffuseColorSlots);
                }
                if (std::strstr(name, "LandscapeFourLayer")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialLandscapeFourLayer,
                        "material-landscape-four-layer",
                        name,
                        kLandscapeFourLayerColorSlots);
                } else if (std::strstr(name, "LandscapeLod")) {
                    tryObserveColorDomain(
                        context,
                        binding,
                        ColorDomainProbeCategory::materialLandscapeLod,
                        "material-landscape-lod",
                        name,
                        kLandscapeLodColorSlots);
                }
                return;
            }

            if (binding.family ==
                linear_lighting::ReplacementShaderFamily::distantTree) {
                tryObserveColorDomain(
                    context,
                    binding,
                    ColorDomainProbeCategory::distantTree,
                    "distant-tree",
                    "DistantTree",
                    kBaseColorSlots);
                return;
            }

            if (binding.family ==
                    linear_lighting::ReplacementShaderFamily::particle &&
                binding.contractPlusOne <=
                    linear_lighting::Runtime::kParticleShaderContractCount) {
                const auto contractIndex = binding.contractPlusOne - 1;
                const auto probe = static_cast<ColorDomainProbeCategory>(
                    static_cast<std::size_t>(ColorDomainProbeCategory::particle0) +
                    contractIndex);
                tryObserveColorDomain(
                    context,
                    binding,
                    probe,
                    kParticleColorDomainLabels[contractIndex],
                    kParticleColorDomainLabels[contractIndex],
                    kParticleColorSlots);
            }
        }

        void recordQualificationBinding(
            ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding) noexcept
        {
            if (!qualificationSessionActive.load(std::memory_order_acquire)) {
                return;
            }
            const auto bit = qualificationContractBit(binding);
            if (!bit) {
                return;
            }
            const auto contractPlusOne = binding.contractPlusOne;

            qualificationReplacementShaderBinds.fetch_add(
                1,
                std::memory_order_relaxed);
            qualificationReplacementContractMask.set(
                contractPlusOne - 1,
                std::memory_order_relaxed);
            const auto geometryGeneration =
                linear_lighting::Runtime::get().geometryUpdateGeneration();
            if (geometryGeneration <= qualificationGeometryUpdateBaseline.load(
                                          std::memory_order_acquire)) {
                qualificationBindingsWithoutFreshGeometry.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return;
            }
            if (qualificationBindingVerifiedContractMask.test(
                    contractPlusOne - 1,
                    std::memory_order_relaxed)) {
                return;
            }

            const auto state = linear_lighting::Runtime::get()
                                   .inspectReplacementPipelineState(
                                       context,
                                       binding,
                                       activeSurfaceClassCode);
            qualificationLastBindingState.store(state, std::memory_order_relaxed);
            qualificationBindingStateChecks.fetch_add(1, std::memory_order_relaxed);
            if ((state &
                    linear_lighting::PipelineBinding_SelectedReplacement) !=
                0) {
                qualificationBindingVerifiedContractMask.set(
                    contractPlusOne - 1,
                    std::memory_order_release);
            } else {
                qualificationBindingStateFailures.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
        }

        void recordQualificationDraw(ID3D11DeviceContext* context) noexcept
        {
            if (!qualificationSessionActive.load(std::memory_order_acquire)) {
                return;
            }
            const auto sessionId = qualificationActivatedSessionId.load(
                std::memory_order_acquire);
            if (activeQualificationSessionId != sessionId) {
                return;
            }
            observeLinearLightingColorDomains(context, activeReplacementBinding);

            const auto bit = qualificationContractBit(
                activeReplacementBinding);
            if (!bit) {
                return;
            }
            const auto contractPlusOne =
                activeReplacementBinding.contractPlusOne;

            qualificationReplacementDrawCalls.fetch_add(
                1,
                std::memory_order_relaxed);
            const auto geometryGeneration =
                linear_lighting::Runtime::get().geometryUpdateGeneration();
            if (geometryGeneration <= qualificationGeometryUpdateBaseline.load(
                                          std::memory_order_acquire)) {
                qualificationDrawsWithoutFreshGeometry.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return;
            }
            if (qualificationDrawVerifiedContractMask.test(
                    contractPlusOne - 1,
                    std::memory_order_relaxed)) {
                return;
            }

            const auto state = linear_lighting::Runtime::get()
                                   .inspectReplacementPipelineState(
                                       context,
                                       activeReplacementBinding,
                                       activeSurfaceClassCode);
            qualificationLastDrawState.store(state, std::memory_order_relaxed);
            qualificationDrawStateChecks.fetch_add(1, std::memory_order_relaxed);
            if (state == linear_lighting::PipelineBinding_All) {
                qualificationDrawVerifiedContractMask.set(
                    contractPlusOne - 1,
                    std::memory_order_release);
            } else {
                qualificationDrawStateFailures.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
        }

        [[nodiscard]] linear_lighting::ScopedReplacementPixelConstants
        scopeActiveReplacementPixelConstants(
            ID3D11DeviceContext* context) noexcept
        {
            return linear_lighting::Runtime::get()
                .scopeReplacementPixelConstants(
                    context,
                    activeReplacementBinding);
        }

        void retireDisabledFeatureBindings(
            ID3D11DeviceContext* context) noexcept
        {
            const auto skylightingOwnsAmbient =
                activeReplacementBinding.family ==
                    linear_lighting::ReplacementShaderFamily::dFLightAmbient &&
                skylighting::Runtime::get().requested();
            if (activeReplacementBinding.family !=
                    linear_lighting::ReplacementShaderFamily::none &&
                !skylightingOwnsAmbient &&
                !linear_lighting::Runtime::get()
                     .replacementFeaturesEnabled()) {
                if (originalPSSetShader && activeReplacementOriginal) {
                    originalPSSetShader(
                        context,
                        activeReplacementOriginal,
                        nullptr,
                        0);
                }
                activeReplacementBinding = {};
                activeSurfaceClassCode = 0;
                activeReplacementOriginal = nullptr;
                activeReplacementContext = nullptr;
            }
            auto& iblRuntime = ibl::Runtime::get();
            if (!iblRuntime.featureEnabled()) {
                activeIblCaptureProbePass = {};
            }
            if (activeIblMaterialBinding &&
                !iblRuntime.materialBindingActive(
                    activeIblMaterialBinding)) {
                if (originalPSSetShader) {
                    originalPSSetShader(
                        context,
                        activeIblMaterialBinding.original,
                        nullptr,
                        0);
                }
                activeIblMaterialBinding = {};
            }
            if (activeContactShadowBinding &&
                !contact_shadows::Runtime::get().compositorReady(
                    (wrapped_grass::Runtime::get().requested() ||
                        hair_specular::Runtime::get().requested() ||
                        subsurface_scattering::Runtime::get().requested() ||
                        basic_wetness::Runtime::get().requested() ||
                        cloud_shadows::Runtime::get().requested()) &&
                    linear_lighting::Runtime::get()
                        .linearLightingEnabled())) {
                if (originalPSSetShader) {
                    originalPSSetShader(
                        context,
                        activeContactShadowBinding.original,
                        nullptr,
                        0);
                }
                activeContactShadowBinding = {};
                activeContactShadowsEnabled = false;
                activeWrappedGrassEnabled = false;
                activeHairSpecularEnabled = false;
                activeSubsurfaceScatteringEnabled = false;
                activeBasicWetnessEnabled = false;
                activeCloudShadowsEnabled = false;
            }
            if (activeFilmicTonemappingBinding &&
                !filmic_tonemapping::Runtime::get().bindingActive(
                    activeFilmicTonemappingBinding)) {
                if (originalPSSetShader) {
                    originalPSSetShader(
                        context,
                        activeFilmicTonemappingBinding.original,
                        nullptr,
                        0);
                }
                activeFilmicTonemappingBinding = {};
            }
        }

        void recordDFPrePassDescriptorConsumed(
            std::uint32_t descriptor,
            const char* boundary) noexcept
        {
            if (firstDFPrePassDescriptorConsumeLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                return;
            }
            logging::info(
                "DFPrePass descriptor transaction consumed at {} (descriptor=0x{:08X}).",
                boundary,
                descriptor);
        }

        [[nodiscard]] PendingDFPrePassDescriptor
            activeDFPrePassDescriptor() noexcept
        {
            if (activeDFPrePassTechniques.depth == 0) {
                return {};
            }
            return {
                activeDFPrePassTechniques.descriptors[
                    activeDFPrePassTechniques.depth - 1],
                true,
            };
        }

        void consumePendingDFPrePassDescriptorAtDraw(
            ID3D11DeviceContext* context) noexcept
        {
            if (!pendingDFPrePassDescriptor.active) {
                return;
            }

            const auto descriptor = pendingDFPrePassDescriptor.descriptor;
            pendingDFPrePassDescriptor = {};
            if (!shaderInterceptionActive.load(std::memory_order_acquire) ||
                !originalPSSetShader || !activeReplacementContext ||
                activeReplacementContext != context ||
                !activeReplacementOriginal ||
                activeReplacementBinding.family !=
                    linear_lighting::ReplacementShaderFamily::material) {
                if (!firstDFPrePassDescriptorExpiryLogged.exchange(
                        true,
                        std::memory_order_relaxed)) {
                    logging::warn(
                        "DFPrePass descriptor transaction expired at its draw boundary without a reusable material owner (descriptor=0x{:08X}); the affected draw remains vanilla-classified.",
                        descriptor);
                }
                return;
            }

            const auto selection = linear_lighting::Runtime::get()
                                       .selectPixelShaderForDFPrePassDescriptor(
                                           context,
                                           activeReplacementOriginal,
                                           descriptor);
            auto* selectedShader = selection.shader ?
                selection.shader : activeReplacementOriginal;
            originalPSSetShader(context, selectedShader, nullptr, 0);
            if (selection.retainedForBind && selection.shader) {
                selection.shader->Release();
            }
            activeReplacementBinding = selection.binding;
            activeSurfaceClassCode = selection.surfaceClassCode;
            recordDFPrePassDescriptorConsumed(descriptor, "draw reuse");
        }

        void reconcileGrassVertexClassAtDraw(
            ID3D11DeviceContext* context) noexcept
        {
            constexpr auto grassClassCode = static_cast<std::uint32_t>(
                surface_classification::SurfaceClassCode::grass);
            if (!originalPSSetShader || !activeReplacementOriginal ||
                activeReplacementContext != context ||
                activeReplacementBinding.family !=
                    linear_lighting::ReplacementShaderFamily::material) {
                return;
            }
            const auto currentlyGrass =
                activeSurfaceClassCode == grassClassCode;
            if (currentlyGrass == activeGrassVertexShader) {
                return;
            }

            auto& runtime = linear_lighting::Runtime::get();
            const auto selection = activeGrassVertexShader ?
                runtime.selectPixelShaderForGrassVertex(
                    context,
                    activeReplacementOriginal) :
                runtime.selectPixelShader(
                    context,
                    activeReplacementOriginal);
            if (!selection.shader ||
                selection.binding.family !=
                    linear_lighting::ReplacementShaderFamily::material) {
                return;
            }
            originalPSSetShader(context, selection.shader, nullptr, 0);
            if (selection.retainedForBind) {
                selection.shader->Release();
            }
            activeReplacementBinding = selection.binding;
            activeSurfaceClassCode = selection.surfaceClassCode;
            if (activeGrassVertexShader &&
                !firstGrassVertexDrawRebindLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Surface classification reconciled its first exact grass-only vertex shader at the draw boundary.");
            }
        }

        void recordFirstTerrainDrawCaller(
            const void* caller,
            const char* drawMethod) noexcept
        {
            constexpr auto terrainClassCode = static_cast<std::uint32_t>(
                surface_classification::SurfaceClassCode::terrain);
            if (activeSurfaceClassCode != terrainClassCode ||
                firstTerrainDrawCallerLogged.load(std::memory_order_relaxed)) {
                return;
            }
            if (firstTerrainDrawCallerLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                return;
            }

            const auto moduleBase = reinterpret_cast<std::uintptr_t>(
                GetModuleHandleW(nullptr));
            const auto callerAddress = reinterpret_cast<std::uintptr_t>(caller);
            if (moduleBase == 0 || callerAddress < moduleBase) {
                logging::warn(
                    "Terrain Blending observed its first classified draw through {}, but the caller was outside the FO4VR image.",
                    drawMethod);
                return;
            }
            const auto callerRva = callerAddress - moduleBase;
            logging::info(
                "Terrain Blending observed its first classified draw through {} at FO4VR+0x{:X} (class=4).",
                drawMethod,
                callerRva);
        }

        [[nodiscard]] bool activeDrawInterceptionRequired() noexcept
        {
            return activeReplacementBinding.family !=
                    linear_lighting::ReplacementShaderFamily::none ||
                activeIblMaterialBinding ||
                activeContactShadowBinding ||
                activeFilmicTonemappingBinding ||
                activeIblCaptureProbePass.lastEnvironmentContractPlusOne != 0 ||
                qualificationSessionActive.load(std::memory_order_acquire);
        }

        void recordActiveIblCaptureProbe(
            ID3D11DeviceContext* context) noexcept
        {
            if (activeIblCaptureProbePass.lastEnvironmentContractPlusOne == 0) {
                return;
            }
            ibl::Runtime::get().onCaptureProbeDraw(
                context,
                activeIblCaptureProbePass.lastEnvironmentContractPlusOne);
        }

        void preserveActiveIblCaptureProbeDraw(
            ID3D11DeviceContext* context) noexcept
        {
            if (activeIblCaptureProbePass.lastEnvironmentContractPlusOne == 0) {
                return;
            }
            ibl::Runtime::get().onCaptureProbeDrawComplete(
                context,
                activeIblCaptureProbePass.lastEnvironmentContractPlusOne);
        }

        void completeIblCaptureProbePass(
            ID3D11DeviceContext* context,
            ibl::CaptureProbeShaderBinding nextBinding) noexcept
        {
            if (!ibl::shouldCaptureCompletedProbePass(
                    activeIblCaptureProbePass,
                    nextBinding)) {
                return;
            }
            ibl::Runtime::get().onCaptureProbePassComplete(
                context,
                activeIblCaptureProbePass.lastEnvironmentContractPlusOne);
        }

        template <class DrawCall>
        void issueDrawWithIblMaterial(
            ID3D11DeviceContext* context,
            DrawCall&& draw) noexcept
        {
            auto& runtime = ibl::Runtime::get();
            const auto contractPlusOne =
                activeIblCaptureProbePass.lastEnvironmentContractPlusOne;

            if (!activeIblMaterialBinding && contractPlusOne == 0) {
                draw();
                return;
            }

            if (activeIblMaterialBinding) {
                auto reflectionFree = runtime.beginReflectionFreeCapture(
                    context,
                    contractPlusOne);
                if (reflectionFree.active()) {
                auto disabled = runtime.scopeMaterialBindings(
                    context,
                    activeIblMaterialBinding,
                    false);
                    auto wetnessDisabled =
                        basic_wetness::Runtime::get().scopeDraw(
                            context,
                            false);
                    if (disabled.active() && wetnessDisabled.active()) {
                        draw();
                        const auto materialRestored = disabled.restore();
                        const auto reflectionRestored =
                            reflectionFree.restore();
                        runtime.onMaterialBindingsComplete(
                            activeIblMaterialBinding,
                            false,
                            materialRestored);
                        runtime.onReflectionFreeCaptureDrawComplete(
                            context,
                            contractPlusOne,
                            reflectionRestored && materialRestored);
                    } else {
                        (void)reflectionFree.restore();
                        runtime.onReflectionFreeCaptureDrawComplete(
                            context,
                            contractPlusOne,
                            false);
                    }
                }
            } else {
                auto reflectionFree = runtime.beginReflectionFreeCapture(
                    context,
                    contractPlusOne);
                if (reflectionFree.active()) {
                    draw();
                    const auto restored = reflectionFree.restore();
                    runtime.onReflectionFreeCaptureDrawComplete(
                        context,
                        contractPlusOne,
                        restored);
                }
            }

            if (activeIblMaterialBinding) {
                auto enabled = runtime.scopeMaterialBindings(
                    context,
                    activeIblMaterialBinding,
                    true);
                const auto wetnessActive =
                    basic_wetness::Runtime::get().requested() &&
                    linear_lighting::Runtime::get()
                        .linearLightingEnabled();
                auto wetness = basic_wetness::Runtime::get().scopeDraw(
                    context,
                    wetnessActive);
                if (enabled.active() && wetness.active()) {
                    draw();
                    const auto restored = enabled.restore();
                    runtime.onMaterialBindingsComplete(
                        activeIblMaterialBinding,
                        true,
                        restored);
                } else if (originalPSSetShader) {
                    basic_wetness::Runtime::get().recordDrawFallback();
                    originalPSSetShader(
                        context,
                        activeIblMaterialBinding.original,
                        nullptr,
                        0);
                    draw();
                    originalPSSetShader(
                        context,
                        activeIblMaterialBinding.replacement,
                        nullptr,
                        0);
                } else {
                    draw();
                }
            } else {
                draw();
            }
            preserveActiveIblCaptureProbeDraw(context);
        }

        template <class Draw>
        void issueDrawWithCloudShadows(
            ID3D11DeviceContext* context,
            Draw&& draw) noexcept
        {
            const auto capture = cloud_shadows::Runtime::get().scopeCapture(
                context, activeReplacementBinding);
            draw();
        }

        template <class Draw>
        void issueDrawWithContactShadows(
            ID3D11DeviceContext* context,
            Draw&& draw) noexcept
        {
            if (!activeContactShadowBinding) {
                draw();
                return;
            }
            auto& runtime = contact_shadows::Runtime::get();
            const auto bindings = runtime.scopeDraw(
                context,
                activeContactShadowBinding,
                activeContactShadowsEnabled,
                activeCloudShadowsEnabled);
            auto& wrappedRuntime = wrapped_grass::Runtime::get();
            const auto wrappedBindings = wrappedRuntime.scopeDraw(
                context,
                activeWrappedGrassEnabled);
            auto& hairRuntime = hair_specular::Runtime::get();
            const auto hairBindings = hairRuntime.scopeDraw(
                context,
                activeHairSpecularEnabled);
            auto& wetnessRuntime = basic_wetness::Runtime::get();
            const auto wetnessBindings = wetnessRuntime.scopeDraw(
                context,
                activeBasicWetnessEnabled);
            if (bindings.active() && wrappedBindings.active() &&
                hairBindings.active() && wetnessBindings.active()) {
                draw();
                if (activeSubsurfaceScatteringEnabled) {
                    (void)subsurface_scattering::Runtime::get()
                        .executeAfterDirectionalLight(context);
                }
                surface_classification::Runtime::get()
                    .markWorldFrameConsumed();
                return;
            }

            runtime.recordDrawFallback();
            wrappedRuntime.recordDrawFallback();
            hairRuntime.recordDrawFallback();
            wetnessRuntime.recordDrawFallback();
            if (!originalPSSetShader) {
                return;
            }
            originalPSSetShader(
                context,
                activeContactShadowBinding.original,
                nullptr,
                0);
            draw();
            if (activeSubsurfaceScatteringEnabled) {
                (void)subsurface_scattering::Runtime::get()
                    .executeAfterDirectionalLight(context);
            }
            surface_classification::Runtime::get()
                .markWorldFrameConsumed();
            originalPSSetShader(
                context,
                activeContactShadowBinding.replacement,
                nullptr,
                0);
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

        HRESULT STDMETHODCALLTYPE hookCreateVertexShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11ClassLinkage* classLinkage,
            ID3D11VertexShader** shader) noexcept
        {
            vertexShaderCreationCalls.fetch_add(1, std::memory_order_relaxed);
            const auto original = originalCreateVertexShader;
            if (!original) {
                return E_UNEXPECTED;
            }
            const auto selection = vanilla_fixes::selectVertexShader(
                bytecode,
                bytecodeLength);
            auto result = original(
                device,
                selection.data,
                selection.size,
                classLinkage,
                shader);
            auto replacementAccepted =
                selection.replaced() && SUCCEEDED(result) && shader && *shader;
            if (selection.replaced() && !replacementAccepted) {
                result = original(
                    device,
                    bytecode,
                    bytecodeLength,
                    classLinkage,
                    shader);
            }
            vanilla_fixes::reportShaderCreationResult(
                selection,
                replacementAccepted);
            if (shaderInterceptionActive.load(std::memory_order_acquire) &&
                SUCCEEDED(result) && shader && *shader) {
                linear_lighting::Runtime::get().onVertexShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
            }
            return result;
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
            const auto identity = vanilla_fixes::identifyShader(
                bytecode,
                bytecodeLength);
            const auto focusShadow =
                vanilla_fixes::isStockFocusShadowPixel(identity);
            std::vector<std::byte> patchStorage;
            const auto selection = vanilla_fixes::selectPixelShader(
                bytecode,
                bytecodeLength,
                patchStorage);
            auto result = original(
                device,
                selection.data,
                selection.size,
                classLinkage,
                shader);
            auto replacementAccepted =
                selection.replaced() && SUCCEEDED(result) && shader && *shader;
            if (selection.replaced() && !replacementAccepted) {
                result = original(
                    device,
                    bytecode,
                    bytecodeLength,
                    classLinkage,
                    shader);
            }
            vanilla_fixes::reportShaderCreationResult(
                selection,
                replacementAccepted);
            if (SUCCEEDED(result) && shader && *shader) {
                community_shaders::dlaa::Runtime::get().onPixelShaderCreated(
                    *shader,
                    identity.bytecodeSize,
                    identity.hash,
                    identity.checksum);
            }
            if (shaderInterceptionActive.load(std::memory_order_acquire) &&
                SUCCEEDED(result) && shader && *shader) {
                if (focusShadow) {
                    vanilla_fixes::registerFocusShadowPixelShader(*shader);
                    vanilla_fixes::reportFocusShaderCreated();
                }
                linear_lighting::Runtime::get().onPixelShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
                ibl::Runtime::get().onPixelShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
                contact_shadows::Runtime::get().onPixelShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
                filmic_tonemapping::Runtime::get().onPixelShaderCreated(
                    bytecode,
                    bytecodeLength,
                    *shader);
            }
            return result;
        }

        HRESULT STDMETHODCALLTYPE hookCreateComputeShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11ClassLinkage* classLinkage,
            ID3D11ComputeShader** shader) noexcept
        {
            computeShaderCreationCalls.fetch_add(1, std::memory_order_relaxed);
            const auto original = originalCreateComputeShader;
            if (!original) {
                return E_UNEXPECTED;
            }
            const auto selection = vanilla_fixes::selectComputeShader(
                bytecode,
                bytecodeLength);
            auto result = original(
                device,
                selection.data,
                selection.size,
                classLinkage,
                shader);
            auto replacementAccepted =
                selection.replaced() && SUCCEEDED(result) && shader && *shader;
            if (selection.replaced() && !replacementAccepted) {
                result = original(
                    device,
                    bytecode,
                    bytecodeLength,
                    classLinkage,
                    shader);
            }
            vanilla_fixes::reportShaderCreationResult(
                selection,
                replacementAccepted);
            return result;
        }

        void STDMETHODCALLTYPE hookVSSetShader(
            ID3D11DeviceContext* context,
            ID3D11VertexShader* shader,
            ID3D11ClassInstance* const* classInstances,
            UINT classInstanceCount) noexcept
        {
            vertexShaderBindCalls.fetch_add(1, std::memory_order_relaxed);
            const auto original = originalVSSetShader;
            if (!original) {
                return;
            }
            activeGrassVertexShader =
                shaderInterceptionActive.load(std::memory_order_acquire) &&
                classInstanceCount == 0 &&
                linear_lighting::Runtime::get().isGrassVertexShader(shader);
            original(context, shader, classInstances, classInstanceCount);
        }

        void STDMETHODCALLTYPE hookOMSetRenderTargets(
            ID3D11DeviceContext* context,
            UINT renderTargetCount,
            ID3D11RenderTargetView* const* renderTargets,
            ID3D11DepthStencilView* depthStencil) noexcept
        {
            renderTargetBindCalls.fetch_add(1, std::memory_order_relaxed);
            const auto original = originalOMSetRenderTargets;
            if (!original) {
                return;
            }
            vanilla_fixes::observeFocusShadowRenderTargets(depthStencil);

            auto& skylightingRuntime = skylighting::Runtime::get();
            auto* effectiveDepthStencil =
                skylightingRuntime.substituteDepthStencil(
                    context,
                    depthStencil);
            if (skylightingRuntime.privateCaptureActive()) {
                original(
                    context,
                    renderTargetCount,
                    renderTargets,
                    effectiveDepthStencil);
                return;
            }

            auto& surfaceRuntime = surface_classification::Runtime::get();
            if (shaderInterceptionActive.load(std::memory_order_acquire) &&
                linear_lighting::Runtime::get().linearLightingEnabled() &&
                surfaceRuntime.required()) {
                const auto binding = surfaceRuntime.prepareGBufferBinding(
                    context,
                    renderTargetCount,
                    renderTargets);
                if (binding) {
                    original(
                        context,
                        binding.renderTargetCount,
                        binding.renderTargets.data(),
                        effectiveDepthStencil);
                    return;
                }
            }
            original(
                context,
                renderTargetCount,
                renderTargets,
                effectiveDepthStencil);
        }

        void STDMETHODCALLTYPE hookOMSetRenderTargetsAndUnorderedAccessViews(
            ID3D11DeviceContext* context,
            UINT renderTargetCount,
            ID3D11RenderTargetView* const* renderTargets,
            ID3D11DepthStencilView* depthStencil,
            UINT unorderedAccessStartSlot,
            UINT unorderedAccessViewCount,
            ID3D11UnorderedAccessView* const* unorderedAccessViews,
            const UINT* initialCounts) noexcept
        {
            renderTargetAndUnorderedAccessBindCalls.fetch_add(
                1,
                std::memory_order_relaxed);
            const auto original =
                originalOMSetRenderTargetsAndUnorderedAccessViews;
            if (!original) {
                return;
            }
            vanilla_fixes::observeFocusShadowRenderTargets(depthStencil);

            auto& skylightingRuntime = skylighting::Runtime::get();
            auto* effectiveDepthStencil =
                skylightingRuntime.substituteDepthStencil(
                    context,
                    depthStencil);
            if (skylightingRuntime.privateCaptureActive()) {
                original(
                    context,
                    renderTargetCount,
                    renderTargets,
                    effectiveDepthStencil,
                    unorderedAccessStartSlot,
                    unorderedAccessViewCount,
                    unorderedAccessViews,
                    initialCounts);
                return;
            }

            auto& surfaceRuntime = surface_classification::Runtime::get();
            const auto appendDoesNotOverlapUavs =
                unorderedAccessViewCount == 0 ||
                (unorderedAccessViewCount !=
                        D3D11_KEEP_UNORDERED_ACCESS_VIEWS &&
                    unorderedAccessStartSlot >= 7);
            if (shaderInterceptionActive.load(std::memory_order_acquire) &&
                linear_lighting::Runtime::get().linearLightingEnabled() &&
                surfaceRuntime.required() && appendDoesNotOverlapUavs) {
                const auto binding = surfaceRuntime.prepareGBufferBinding(
                    context,
                    renderTargetCount,
                    renderTargets);
                if (binding) {
                    original(
                        context,
                        binding.renderTargetCount,
                        binding.renderTargets.data(),
                        effectiveDepthStencil,
                        unorderedAccessStartSlot,
                        unorderedAccessViewCount,
                        unorderedAccessViews,
                        initialCounts);
                    return;
                }
            }
            original(
                context,
                renderTargetCount,
                renderTargets,
                effectiveDepthStencil,
                unorderedAccessStartSlot,
                unorderedAccessViewCount,
                unorderedAccessViews,
                initialCounts);
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
            activeFocusShadowPixel =
                vanilla_fixes::isFocusShadowPixelShader(shader);
            if (!shaderInterceptionActive.load(std::memory_order_acquire)) {
                activeReplacementBinding = {};
                activeSurfaceClassCode = 0;
                activeReplacementOriginal = nullptr;
                activeReplacementContext = nullptr;
                pendingDFPrePassDescriptor = {};
                activeContactShadowBinding = {};
                activeFilmicTonemappingBinding = {};
                activeContactShadowsEnabled = false;
                activeWrappedGrassEnabled = false;
                activeHairSpecularEnabled = false;
                activeSubsurfaceScatteringEnabled = false;
                activeBasicWetnessEnabled = false;
                activeCloudShadowsEnabled = false;
                activeIblMaterialBinding = {};
                activeIblCaptureProbePass = {};
                original(context, shader, classInstances, classInstanceCount);
                return;
            }
            if (insidePSSetShaderHook) {
                pixelShaderBindRecursions.fetch_add(1, std::memory_order_relaxed);
                activeReplacementBinding = {};
                activeSurfaceClassCode = 0;
                activeReplacementOriginal = nullptr;
                activeReplacementContext = nullptr;
                pendingDFPrePassDescriptor = {};
                activeContactShadowBinding = {};
                activeFilmicTonemappingBinding = {};
                activeContactShadowsEnabled = false;
                activeWrappedGrassEnabled = false;
                activeHairSpecularEnabled = false;
                activeSubsurfaceScatteringEnabled = false;
                activeBasicWetnessEnabled = false;
                activeCloudShadowsEnabled = false;
                activeIblMaterialBinding = {};
                activeIblCaptureProbePass = {};
                original(context, shader, classInstances, classInstanceCount);
                return;
            }

            const RecursionGuard recursionGuard(insidePSSetShaderHook);
            activatePendingQualificationSession();
            auto& replacementRuntime = linear_lighting::Runtime::get();
            auto& skylightingRuntime = skylighting::Runtime::get();
            auto& iblRuntime = ibl::Runtime::get();
            auto& contactShadowRuntime = contact_shadows::Runtime::get();
            auto& wrappedGrassRuntime = wrapped_grass::Runtime::get();
            auto& hairSpecularRuntime = hair_specular::Runtime::get();
            auto& subsurfaceScatteringRuntime =
                subsurface_scattering::Runtime::get();
            auto& basicWetnessRuntime = basic_wetness::Runtime::get();
            auto& cloudShadowRuntime = cloud_shadows::Runtime::get();
            auto& filmicTonemappingRuntime =
                filmic_tonemapping::Runtime::get();
            const auto qualificationActive =
                qualificationSessionActive.load(std::memory_order_acquire);
            const auto replacementFeaturesActive =
                replacementRuntime.replacementFeaturesEnabled();
            const auto skylightingFeatureActive =
                skylightingRuntime.requested();
            const auto iblFeatureActive = iblRuntime.featureEnabled();
            const auto contactShadowFeatureActive =
                contactShadowRuntime.featureEnabled();
            const auto wrappedGrassFeatureActive =
                wrappedGrassRuntime.requested() &&
                replacementRuntime.linearLightingEnabled();
            const auto hairSpecularFeatureActive =
                hairSpecularRuntime.requested() &&
                replacementRuntime.linearLightingEnabled();
            const auto subsurfaceScatteringFeatureActive =
                subsurfaceScatteringRuntime.requested() &&
                replacementRuntime.linearLightingEnabled();
            const auto basicWetnessFeatureActive =
                basicWetnessRuntime.requested() &&
                replacementRuntime.linearLightingEnabled();
            const auto cloudShadowFeatureActive =
                cloudShadowRuntime.requested() &&
                replacementRuntime.linearLightingEnabled();
            const auto filmicTonemappingFeatureActive =
                filmicTonemappingRuntime.featureEnabled();
            const auto dflightCompositorActive =
                contactShadowRuntime.compositorReady(
                    wrappedGrassFeatureActive || hairSpecularFeatureActive ||
                    subsurfaceScatteringFeatureActive ||
                    basicWetnessFeatureActive || cloudShadowFeatureActive);
            if (contactShadowRuntime.tracksOriginal(shader) &&
                !firstTrackedContactShaderBindLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Contact Shadows observed its first tracked DFLight family bind (classInstances={}, compositorActive={}).",
                    classInstanceCount,
                    dflightCompositorActive);
            }
            if (!replacementFeaturesActive && !iblFeatureActive &&
                !skylightingFeatureActive &&
                !dflightCompositorActive &&
                !filmicTonemappingFeatureActive &&
                !qualificationActive) {
                activeReplacementBinding = {};
                activeSurfaceClassCode = 0;
                activeReplacementOriginal = nullptr;
                activeReplacementContext = nullptr;
                pendingDFPrePassDescriptor = {};
                activeContactShadowBinding = {};
                activeFilmicTonemappingBinding = {};
                activeContactShadowsEnabled = false;
                activeWrappedGrassEnabled = false;
                activeHairSpecularEnabled = false;
                activeSubsurfaceScatteringEnabled = false;
                activeBasicWetnessEnabled = false;
                activeCloudShadowsEnabled = false;
                activeIblMaterialBinding = {};
                activeIblCaptureProbePass = {};
                original(context, shader, classInstances, classInstanceCount);
                return;
            }

            const auto nextIblCaptureProbeBinding = iblFeatureActive ?
                iblRuntime.captureProbeBindingForShader(shader) :
                ibl::CaptureProbeShaderBinding{};
            if (iblFeatureActive) {
                completeIblCaptureProbePass(
                    context,
                    nextIblCaptureProbeBinding);
                activeIblCaptureProbePass = ibl::advanceCaptureProbePass(
                    activeIblCaptureProbePass,
                    nextIblCaptureProbeBinding);
            } else {
                activeIblCaptureProbePass = {};
            }
            auto contactShadowSelection =
                contact_shadows::PixelShaderSelection{ shader, {} };
            // The exact directional-light compositor is not one of the
            // material contracts qualified below. FO4VR may bind it once
            // while qualification is active and retain that bind for the
            // world session, so suppressing it here permanently loses every
            // feature composed through that pass.
            if (dflightCompositorActive && classInstanceCount == 0) {
                contactShadowSelection =
                    contactShadowRuntime.selectPixelShader(
                        shader,
                        wrappedGrassFeatureActive ||
                            hairSpecularFeatureActive ||
                            subsurfaceScatteringFeatureActive ||
                            basicWetnessFeatureActive ||
                            cloudShadowFeatureActive);
            }
            const auto descriptorPending = pendingDFPrePassDescriptor.active;
            const auto activeDescriptor = activeDFPrePassDescriptor();
            const auto descriptorAvailable =
                descriptorPending || activeDescriptor.active;
            const auto selectedDescriptor = descriptorPending ?
                pendingDFPrePassDescriptor.descriptor :
                activeDescriptor.descriptor;
            const auto selection = [&]() noexcept {
                auto selected =
                    !contactShadowSelection.binding &&
                        (replacementFeaturesActive || qualificationActive) ?
                    (activeGrassVertexShader ?
                        replacementRuntime.selectPixelShaderForGrassVertex(
                            context,
                            shader) :
                        (descriptorAvailable ?
                        replacementRuntime
                            .selectPixelShaderForDFPrePassDescriptor(
                                context,
                                shader,
                                selectedDescriptor) :
                        replacementRuntime.selectPixelShader(
                            context,
                            shader))) :
                    linear_lighting::PixelShaderSelection{
                        contactShadowSelection.shader,
                        {},
                    };
                if (!contactShadowSelection.binding &&
                    skylightingFeatureActive && classInstanceCount == 0 &&
                    selected.binding.family ==
                        linear_lighting::ReplacementShaderFamily::none) {
                    selected =
                        replacementRuntime.selectDFLightAmbientPixelShader(
                            context,
                            shader);
                }
                return selected;
            }();
            if (descriptorPending &&
                selection.binding.family ==
                    linear_lighting::ReplacementShaderFamily::material) {
                pendingDFPrePassDescriptor = {};
                recordDFPrePassDescriptorConsumed(
                    selectedDescriptor,
                    "material bind");
            } else if (!descriptorPending && activeDescriptor.active &&
                selection.binding.family ==
                    linear_lighting::ReplacementShaderFamily::material) {
                recordDFPrePassDescriptorConsumed(
                    selectedDescriptor,
                    "active technique material bind");
            }
            auto iblSelection = ibl::Runtime::MaterialPixelShaderSelection{
                selection.shader,
                {},
            };
            if (iblFeatureActive && classInstanceCount == 0 &&
                selection.shader == shader &&
                !contactShadowSelection.binding &&
                selection.binding.family ==
                    linear_lighting::ReplacementShaderFamily::none) {
                iblSelection = iblRuntime.selectMaterialPixelShader(
                    context,
                    shader);
            }
            if (selection.binding.family ==
                linear_lighting::ReplacementShaderFamily::dFLightAmbient) {
                ibl::Runtime::get().onDFLightAmbientBind(context);
            }
            auto filmicSelection = filmic_tonemapping::PixelShaderSelection{
                iblSelection.shader,
                {},
            };
            if (filmicTonemappingFeatureActive &&
                classInstanceCount == 0 && iblSelection.shader == shader &&
                !iblSelection.binding && !contactShadowSelection.binding &&
                selection.binding.family ==
                    linear_lighting::ReplacementShaderFamily::none) {
                filmicSelection = filmicTonemappingRuntime.selectPixelShader(
                    context,
                    shader);
            }
            original(
                context,
                filmicSelection.shader,
                classInstances,
                classInstanceCount);
            if (selection.retainedForBind && selection.shader) {
                selection.shader->Release();
            }
            activeReplacementBinding = selection.binding;
            activeSurfaceClassCode = selection.surfaceClassCode;
            activeReplacementOriginal = selection.binding.family !=
                    linear_lighting::ReplacementShaderFamily::none ?
                shader : nullptr;
            activeReplacementContext =
                activeReplacementOriginal && classInstanceCount == 0 ?
                context : nullptr;
            activeContactShadowBinding = contactShadowSelection.binding;
            activeFilmicTonemappingBinding = filmicSelection.binding;
            activeContactShadowsEnabled =
                contactShadowSelection.binding &&
                contactShadowFeatureActive;
            activeWrappedGrassEnabled =
                contactShadowSelection.binding &&
                wrappedGrassFeatureActive;
            activeHairSpecularEnabled =
                contactShadowSelection.binding &&
                hairSpecularFeatureActive;
            activeSubsurfaceScatteringEnabled =
                contactShadowSelection.binding &&
                subsurfaceScatteringFeatureActive;
            activeBasicWetnessEnabled =
                contactShadowSelection.binding &&
                basicWetnessFeatureActive;
            activeCloudShadowsEnabled =
                contactShadowSelection.binding &&
                cloudShadowFeatureActive;
            activeIblMaterialBinding = iblSelection.binding;

            if (!qualificationSessionActive.load(std::memory_order_acquire)) {
                return;
            }
            activeQualificationSessionId =
                qualificationActivatedSessionId.load(
                    std::memory_order_acquire);
            if (selection.binding.family ==
                linear_lighting::ReplacementShaderFamily::material) {
                recordQualificationBinding(
                    context,
                    selection.binding);
            }
        }

        void STDMETHODCALLTYPE hookDrawIndexed(
            ID3D11DeviceContext* context,
            UINT indexCount,
            UINT startIndexLocation,
            INT baseVertexLocation) noexcept
        {
            const vanilla_fixes::ScopedFocusShadowBinding focusShadowBinding(
                context,
                activeFocusShadowPixel &&
                    vanilla_fixes::focusShadowsEnabled());
            const auto caller = _ReturnAddress();
            consumePendingDFPrePassDescriptorAtDraw(context);
            reconcileGrassVertexClassAtDraw(context);
            retireDisabledFeatureBindings(context);
            recordFirstTerrainDrawCaller(caller, "DrawIndexed");
            if (!activeDrawInterceptionRequired()) {
                if (originalDrawIndexed) {
                    originalDrawIndexed(
                        context,
                        indexCount,
                        startIndexLocation,
                        baseVertexLocation);
                }
                return;
            }
            const auto constants =
                scopeActiveReplacementPixelConstants(context);
            const auto skylightingBindings =
                skylighting::Runtime::get().scopeAmbientDraw(
                    context,
                    activeReplacementBinding.family ==
                        linear_lighting::ReplacementShaderFamily::
                            dFLightAmbient);
            const auto filmicConstants =
                filmic_tonemapping::Runtime::get().scopeDraw(
                    context,
                    activeFilmicTonemappingBinding);
            recordActiveIblCaptureProbe(context);
            if (qualificationSessionActive.load(std::memory_order_acquire)) {
                qualificationDrawIndexedCalls.fetch_add(
                    1,
                    std::memory_order_relaxed);
                recordQualificationDraw(context);
            }
            if (originalDrawIndexed) {
                issueDrawWithCloudShadows(context, [&]() noexcept {
                    issueDrawWithContactShadows(context, [&]() noexcept {
                        issueDrawWithIblMaterial(context, [&]() noexcept {
                            originalDrawIndexed(
                                context,
                                indexCount,
                                startIndexLocation,
                                baseVertexLocation);
                        });
                    });
                });
            }
        }

        void STDMETHODCALLTYPE hookDraw(
            ID3D11DeviceContext* context,
            UINT vertexCount,
            UINT startVertexLocation) noexcept
        {
            const vanilla_fixes::ScopedFocusShadowBinding focusShadowBinding(
                context,
                activeFocusShadowPixel &&
                    vanilla_fixes::focusShadowsEnabled());
            const auto caller = _ReturnAddress();
            consumePendingDFPrePassDescriptorAtDraw(context);
            reconcileGrassVertexClassAtDraw(context);
            retireDisabledFeatureBindings(context);
            recordFirstTerrainDrawCaller(caller, "Draw");
            if (!activeDrawInterceptionRequired()) {
                if (originalDraw) {
                    originalDraw(context, vertexCount, startVertexLocation);
                }
                return;
            }
            const auto constants =
                scopeActiveReplacementPixelConstants(context);
            const auto skylightingBindings =
                skylighting::Runtime::get().scopeAmbientDraw(
                    context,
                    activeReplacementBinding.family ==
                        linear_lighting::ReplacementShaderFamily::
                            dFLightAmbient);
            const auto filmicConstants =
                filmic_tonemapping::Runtime::get().scopeDraw(
                    context,
                    activeFilmicTonemappingBinding);
            recordActiveIblCaptureProbe(context);
            if (qualificationSessionActive.load(std::memory_order_acquire)) {
                qualificationDrawCalls.fetch_add(1, std::memory_order_relaxed);
                recordQualificationDraw(context);
            }
            if (originalDraw) {
                issueDrawWithCloudShadows(context, [&]() noexcept {
                    issueDrawWithContactShadows(context, [&]() noexcept {
                        issueDrawWithIblMaterial(context, [&]() noexcept {
                            originalDraw(
                                context, vertexCount, startVertexLocation);
                        });
                    });
                });
            }
        }

        void STDMETHODCALLTYPE hookDrawIndexedInstanced(
            ID3D11DeviceContext* context,
            UINT indexCountPerInstance,
            UINT instanceCount,
            UINT startIndexLocation,
            INT baseVertexLocation,
            UINT startInstanceLocation) noexcept
        {
            const vanilla_fixes::ScopedFocusShadowBinding focusShadowBinding(
                context,
                activeFocusShadowPixel &&
                    vanilla_fixes::focusShadowsEnabled());
            const auto caller = _ReturnAddress();
            consumePendingDFPrePassDescriptorAtDraw(context);
            reconcileGrassVertexClassAtDraw(context);
            retireDisabledFeatureBindings(context);
            recordFirstTerrainDrawCaller(caller, "DrawIndexedInstanced");
            if (!activeDrawInterceptionRequired()) {
                if (originalDrawIndexedInstanced) {
                    originalDrawIndexedInstanced(
                        context,
                        indexCountPerInstance,
                        instanceCount,
                        startIndexLocation,
                        baseVertexLocation,
                        startInstanceLocation);
                }
                return;
            }
            const auto constants =
                scopeActiveReplacementPixelConstants(context);
            const auto skylightingBindings =
                skylighting::Runtime::get().scopeAmbientDraw(
                    context,
                    activeReplacementBinding.family ==
                        linear_lighting::ReplacementShaderFamily::
                            dFLightAmbient);
            const auto filmicConstants =
                filmic_tonemapping::Runtime::get().scopeDraw(
                    context,
                    activeFilmicTonemappingBinding);
            recordActiveIblCaptureProbe(context);
            if (qualificationSessionActive.load(std::memory_order_acquire)) {
                qualificationDrawIndexedInstancedCalls.fetch_add(
                    1,
                    std::memory_order_relaxed);
                recordQualificationDraw(context);
            }
            if (originalDrawIndexedInstanced) {
                issueDrawWithCloudShadows(context, [&]() noexcept {
                    issueDrawWithContactShadows(context, [&]() noexcept {
                        issueDrawWithIblMaterial(context, [&]() noexcept {
                            originalDrawIndexedInstanced(
                                context,
                                indexCountPerInstance,
                                instanceCount,
                                startIndexLocation,
                                baseVertexLocation,
                                startInstanceLocation);
                        });
                    });
                });
            }
        }

        void STDMETHODCALLTYPE hookDrawInstanced(
            ID3D11DeviceContext* context,
            UINT vertexCountPerInstance,
            UINT instanceCount,
            UINT startVertexLocation,
            UINT startInstanceLocation) noexcept
        {
            const vanilla_fixes::ScopedFocusShadowBinding focusShadowBinding(
                context,
                activeFocusShadowPixel &&
                    vanilla_fixes::focusShadowsEnabled());
            const auto caller = _ReturnAddress();
            consumePendingDFPrePassDescriptorAtDraw(context);
            reconcileGrassVertexClassAtDraw(context);
            retireDisabledFeatureBindings(context);
            recordFirstTerrainDrawCaller(caller, "DrawInstanced");
            if (!activeDrawInterceptionRequired()) {
                if (originalDrawInstanced) {
                    originalDrawInstanced(
                        context,
                        vertexCountPerInstance,
                        instanceCount,
                        startVertexLocation,
                        startInstanceLocation);
                }
                return;
            }
            const auto constants =
                scopeActiveReplacementPixelConstants(context);
            const auto skylightingBindings =
                skylighting::Runtime::get().scopeAmbientDraw(
                    context,
                    activeReplacementBinding.family ==
                        linear_lighting::ReplacementShaderFamily::
                            dFLightAmbient);
            const auto filmicConstants =
                filmic_tonemapping::Runtime::get().scopeDraw(
                    context,
                    activeFilmicTonemappingBinding);
            recordActiveIblCaptureProbe(context);
            if (qualificationSessionActive.load(std::memory_order_acquire)) {
                qualificationDrawInstancedCalls.fetch_add(
                    1,
                    std::memory_order_relaxed);
                recordQualificationDraw(context);
            }
            if (originalDrawInstanced) {
                issueDrawWithCloudShadows(context, [&]() noexcept {
                    issueDrawWithContactShadows(context, [&]() noexcept {
                        issueDrawWithIblMaterial(context, [&]() noexcept {
                            originalDrawInstanced(
                                context,
                                vertexCountPerInstance,
                                instanceCount,
                                startVertexLocation,
                                startInstanceLocation);
                        });
                    });
                });
            }
        }

        [[nodiscard]] bool createQualificationDrawDetour(
            void* target,
            void* hook,
            void*& trampoline,
            const char* name) noexcept
        {
            const auto status = MH_CreateHook(target, hook, &trampoline);
            if (status == MH_OK && isExecutableAddress(trampoline)) {
                return true;
            }
            logging::error(
                "{} qualification detour creation/prologue validation failed: {} ({}), trampoline={}.",
                name,
                minHookStatusName(status),
                static_cast<int>(status),
                trampoline);
            return false;
        }

        [[nodiscard]] bool installQualificationDrawDetours(
            ID3D11DeviceContext* context) noexcept
        {
            auto** contextVtable = *reinterpret_cast<void***>(context);
            if (!isReadableRange(
                    contextVtable,
                    (kDrawInstancedVtableIndex + 1) * sizeof(void*))) {
                logging::error(
                    "D3D11 qualification draw table failed the readable-range gate.");
                return false;
            }

            drawIndexedTarget = contextVtable[kDrawIndexedVtableIndex];
            drawTarget = contextVtable[kDrawVtableIndex];
            drawIndexedInstancedTarget =
                contextVtable[kDrawIndexedInstancedVtableIndex];
            drawInstancedTarget = contextVtable[kDrawInstancedVtableIndex];
            const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
            const std::array<void*, 4> targets{
                drawIndexedTarget,
                drawTarget,
                drawIndexedInstancedTarget,
                drawInstancedTarget,
            };
            for (const auto* target : targets) {
                if (!isExecutableAddress(target) ||
                    !addressBelongsToModule(target, d3d11)) {
                    logging::error(
                        "D3D11 qualification draw-method identity gate failed for target={}.",
                        target);
                    rollbackQualificationDrawDetours(false, false, false, false);
                    return false;
                }
            }

            bool drawIndexedCreated{};
            bool drawCreated{};
            bool drawIndexedInstancedCreated{};
            bool drawInstancedCreated{};
            void* trampoline{};
            if (!createQualificationDrawDetour(
                    drawIndexedTarget,
                    reinterpret_cast<void*>(&hookDrawIndexed),
                    trampoline,
                    "DrawIndexed")) {
                rollbackQualificationDrawDetours(false, false, false, false);
                return false;
            }
            drawIndexedCreated = true;
            originalDrawIndexed =
                reinterpret_cast<DrawIndexedFunction>(trampoline);

            trampoline = nullptr;
            if (!createQualificationDrawDetour(
                    drawTarget,
                    reinterpret_cast<void*>(&hookDraw),
                    trampoline,
                    "Draw")) {
                rollbackQualificationDrawDetours(true, false, false, false);
                return false;
            }
            drawCreated = true;
            originalDraw = reinterpret_cast<DrawFunction>(trampoline);

            trampoline = nullptr;
            if (!createQualificationDrawDetour(
                    drawIndexedInstancedTarget,
                    reinterpret_cast<void*>(&hookDrawIndexedInstanced),
                    trampoline,
                    "DrawIndexedInstanced")) {
                rollbackQualificationDrawDetours(true, true, false, false);
                return false;
            }
            drawIndexedInstancedCreated = true;
            originalDrawIndexedInstanced =
                reinterpret_cast<DrawIndexedInstancedFunction>(trampoline);

            trampoline = nullptr;
            if (!createQualificationDrawDetour(
                    drawInstancedTarget,
                    reinterpret_cast<void*>(&hookDrawInstanced),
                    trampoline,
                    "DrawInstanced")) {
                rollbackQualificationDrawDetours(true, true, true, false);
                return false;
            }
            drawInstancedCreated = true;
            originalDrawInstanced =
                reinterpret_cast<DrawInstancedFunction>(trampoline);

            const std::array<void*, 4> createdTargets{
                drawIndexedTarget,
                drawTarget,
                drawIndexedInstancedTarget,
                drawInstancedTarget,
            };
            for (const auto* target : createdTargets) {
                const auto status = MH_QueueEnableHook(
                    const_cast<void*>(target));
                if (status != MH_OK) {
                    logging::error(
                        "D3D11 qualification draw-detour queue failed: {} ({}), target={}.",
                        minHookStatusName(status),
                        static_cast<int>(status),
                        target);
                    rollbackQualificationDrawDetours(
                        drawIndexedCreated,
                        drawCreated,
                        drawIndexedInstancedCreated,
                        drawInstancedCreated);
                    return false;
                }
            }
            const auto status = MH_ApplyQueued();
            if (status != MH_OK ||
                !captureMinHookPatchIdentity(
                    drawIndexedTarget,
                    drawIndexedPatch) ||
                !captureMinHookPatchIdentity(drawTarget, drawPatch) ||
                !captureMinHookPatchIdentity(
                    drawIndexedInstancedTarget,
                    drawIndexedInstancedPatch) ||
                !captureMinHookPatchIdentity(
                    drawInstancedTarget,
                    drawInstancedPatch)) {
                logging::error(
                    "D3D11 qualification draw-detour activation/ownership validation failed: {} ({}).",
                    minHookStatusName(status),
                    static_cast<int>(status));
                rollbackQualificationDrawDetours(
                    drawIndexedCreated,
                    drawCreated,
                    drawIndexedInstancedCreated,
                    drawInstancedCreated);
                return false;
            }

            qualificationDrawDetoursInstalled.store(
                true,
                std::memory_order_release);
            qualificationDrawDetoursOwned.store(true, std::memory_order_release);
            logging::info(
                "Installed validated D3D11 qualification draw detours (DrawIndexed={}, Draw={}, DrawIndexedInstanced={}, DrawInstanced={}).",
                drawIndexedTarget,
                drawTarget,
                drawIndexedInstancedTarget,
                drawInstancedTarget);
            return true;
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
                    (kCreateComputeShaderVtableIndex + 1) * sizeof(void*)) ||
                !isReadableRange(
                    contextVtable,
                    (kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex + 1) *
                        sizeof(void*))) {
                logging::error(
                    "D3D11 method tables failed the readable-range gate; shader interception remains vanilla.");
                return false;
            }

            createVertexShaderTarget =
                deviceVtable[kCreateVertexShaderVtableIndex];
            createPixelShaderTarget =
                deviceVtable[kCreatePixelShaderVtableIndex];
            createComputeShaderTarget =
                deviceVtable[kCreateComputeShaderVtableIndex];
            vertexShaderBindTarget = contextVtable[kVSSetShaderVtableIndex];
            pixelShaderBindTarget = contextVtable[kPSSetShaderVtableIndex];
            renderTargetBindTarget =
                contextVtable[kOMSetRenderTargetsVtableIndex];
            renderTargetAndUnorderedAccessBindTarget = contextVtable[
                kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex];
            const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
            if (!isExecutableAddress(createVertexShaderTarget) ||
                !isExecutableAddress(createPixelShaderTarget) ||
                !isExecutableAddress(createComputeShaderTarget) ||
                !isExecutableAddress(vertexShaderBindTarget) ||
                !isExecutableAddress(pixelShaderBindTarget) ||
                !isExecutableAddress(renderTargetBindTarget) ||
                !isExecutableAddress(
                    renderTargetAndUnorderedAccessBindTarget) ||
                !addressBelongsToModule(createVertexShaderTarget, d3d11) ||
                !addressBelongsToModule(createPixelShaderTarget, d3d11) ||
                !addressBelongsToModule(createComputeShaderTarget, d3d11) ||
                !addressBelongsToModule(vertexShaderBindTarget, d3d11) ||
                !addressBelongsToModule(pixelShaderBindTarget, d3d11) ||
                !addressBelongsToModule(renderTargetBindTarget, d3d11) ||
                !addressBelongsToModule(
                    renderTargetAndUnorderedAccessBindTarget,
                    d3d11)) {
                const auto createVertexPath =
                    modulePathForAddress(createVertexShaderTarget);
                const auto createPath =
                    modulePathForAddress(createPixelShaderTarget);
                const auto createComputePath =
                    modulePathForAddress(createComputeShaderTarget);
                const auto vertexBindPath =
                    modulePathForAddress(vertexShaderBindTarget);
                const auto bindPath = modulePathForAddress(pixelShaderBindTarget);
                const auto renderTargetPath =
                    modulePathForAddress(renderTargetBindTarget);
                const auto renderTargetAndUnorderedAccessPath =
                    modulePathForAddress(
                        renderTargetAndUnorderedAccessBindTarget);
                logging::error(
                    "D3D11 method identity gate rejected targets (CreateVertexShader='{}' {}, CreatePixelShader='{}' {}, CreateComputeShader='{}' {}, VSSetShader='{}' {}, PSSetShader='{}' {}, OMSetRenderTargets='{}' {}, OMSetRenderTargetsAndUnorderedAccessViews='{}' {}); interception remains vanilla.",
                    createVertexPath.data(),
                    createVertexShaderTarget,
                    createPath.data(),
                    createPixelShaderTarget,
                    createComputePath.data(),
                    createComputeShaderTarget,
                    vertexBindPath.data(),
                    vertexShaderBindTarget,
                    bindPath.data(),
                    pixelShaderBindTarget,
                    renderTargetPath.data(),
                    renderTargetBindTarget,
                    renderTargetAndUnorderedAccessPath.data(),
                    renderTargetAndUnorderedAccessBindTarget);
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

            bool createVertexShaderCreated{};
            bool createPixelShaderCreated{};
            bool vertexShaderBindCreated{};
            bool pixelShaderBindCreated{};
            bool renderTargetBindCreated{};
            bool renderTargetAndUnorderedAccessBindCreated{};
            void* createVertexShaderTrampoline{};
            status = MH_CreateHook(
                createVertexShaderTarget,
                reinterpret_cast<void*>(&hookCreateVertexShader),
                &createVertexShaderTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(createVertexShaderTrampoline)) {
                logging::error(
                    "CreateVertexShader detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    createVertexShaderTrampoline);
                rollbackMethodDetours(
                    false,
                    false,
                    false,
                    false,
                    false,
                    false);
                return false;
            }
            createVertexShaderCreated = true;
            originalCreateVertexShader =
                reinterpret_cast<CreateVertexShaderFunction>(
                    createVertexShaderTrampoline);

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
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    false,
                    false,
                    false,
                    false,
                    false);
                return false;
            }
            createPixelShaderCreated = true;
            originalCreatePixelShader =
                reinterpret_cast<CreatePixelShaderFunction>(
                    createPixelShaderTrampoline);

            void* vertexShaderBindTrampoline{};
            status = MH_CreateHook(
                vertexShaderBindTarget,
                reinterpret_cast<void*>(&hookVSSetShader),
                &vertexShaderBindTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(vertexShaderBindTrampoline)) {
                logging::error(
                    "VSSetShader detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    vertexShaderBindTrampoline);
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    false,
                    false,
                    false,
                    false);
                return false;
            }
            vertexShaderBindCreated = true;
            originalVSSetShader = reinterpret_cast<VSSetShaderFunction>(
                vertexShaderBindTrampoline);

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
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    false,
                    false,
                    false);
                return false;
            }
            pixelShaderBindCreated = true;
            originalPSSetShader = reinterpret_cast<PSSetShaderFunction>(
                pixelShaderBindTrampoline);

            void* renderTargetBindTrampoline{};
            status = MH_CreateHook(
                renderTargetBindTarget,
                reinterpret_cast<void*>(&hookOMSetRenderTargets),
                &renderTargetBindTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(renderTargetBindTrampoline)) {
                logging::error(
                    "OMSetRenderTargets detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    renderTargetBindTrampoline);
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    false,
                    false);
                return false;
            }
            renderTargetBindCreated = true;
            originalOMSetRenderTargets =
                reinterpret_cast<OMSetRenderTargetsFunction>(
                    renderTargetBindTrampoline);

            void* renderTargetAndUnorderedAccessBindTrampoline{};
            status = MH_CreateHook(
                renderTargetAndUnorderedAccessBindTarget,
                reinterpret_cast<void*>(
                    &hookOMSetRenderTargetsAndUnorderedAccessViews),
                &renderTargetAndUnorderedAccessBindTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(
                    renderTargetAndUnorderedAccessBindTrampoline)) {
                logging::error(
                    "OMSetRenderTargetsAndUnorderedAccessViews detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    renderTargetAndUnorderedAccessBindTrampoline);
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    renderTargetBindCreated,
                    false);
                return false;
            }
            renderTargetAndUnorderedAccessBindCreated = true;
            originalOMSetRenderTargetsAndUnorderedAccessViews =
                reinterpret_cast<
                    OMSetRenderTargetsAndUnorderedAccessViewsFunction>(
                    renderTargetAndUnorderedAccessBindTrampoline);

            const auto createVertexQueueStatus =
                MH_QueueEnableHook(createVertexShaderTarget);
            const auto createQueueStatus =
                MH_QueueEnableHook(createPixelShaderTarget);
            const auto vertexBindQueueStatus =
                MH_QueueEnableHook(vertexShaderBindTarget);
            const auto bindQueueStatus =
                MH_QueueEnableHook(pixelShaderBindTarget);
            const auto renderTargetQueueStatus =
                MH_QueueEnableHook(renderTargetBindTarget);
            const auto renderTargetAndUnorderedAccessQueueStatus =
                MH_QueueEnableHook(
                    renderTargetAndUnorderedAccessBindTarget);
            if (createVertexQueueStatus != MH_OK ||
                createQueueStatus != MH_OK ||
                vertexBindQueueStatus != MH_OK ||
                bindQueueStatus != MH_OK ||
                renderTargetQueueStatus != MH_OK ||
                renderTargetAndUnorderedAccessQueueStatus != MH_OK) {
                logging::error(
                    "D3D11 detour queue failed: CreateVertexShader={} ({}), CreatePixelShader={} ({}), VSSetShader={} ({}), PSSetShader={} ({}), OMSetRenderTargets={} ({}), OMSetRenderTargetsAndUnorderedAccessViews={} ({}).",
                    minHookStatusName(createVertexQueueStatus),
                    static_cast<int>(createVertexQueueStatus),
                    minHookStatusName(createQueueStatus),
                    static_cast<int>(createQueueStatus),
                    minHookStatusName(vertexBindQueueStatus),
                    static_cast<int>(vertexBindQueueStatus),
                    minHookStatusName(bindQueueStatus),
                    static_cast<int>(bindQueueStatus),
                    minHookStatusName(renderTargetQueueStatus),
                    static_cast<int>(renderTargetQueueStatus),
                    minHookStatusName(
                        renderTargetAndUnorderedAccessQueueStatus),
                    static_cast<int>(
                        renderTargetAndUnorderedAccessQueueStatus));
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    renderTargetBindCreated,
                    renderTargetAndUnorderedAccessBindCreated);
                return false;
            }

            status = MH_ApplyQueued();
            if (status != MH_OK ||
                !captureMinHookPatchIdentity(
                    createVertexShaderTarget,
                    createVertexShaderPatch) ||
                !captureMinHookPatchIdentity(
                    createPixelShaderTarget,
                    createPixelShaderPatch) ||
                !captureMinHookPatchIdentity(
                    vertexShaderBindTarget,
                    vertexShaderBindPatch) ||
                !captureMinHookPatchIdentity(
                    pixelShaderBindTarget,
                    pixelShaderBindPatch) ||
                !captureMinHookPatchIdentity(
                    renderTargetBindTarget,
                    renderTargetBindPatch) ||
                !captureMinHookPatchIdentity(
                    renderTargetAndUnorderedAccessBindTarget,
                    renderTargetAndUnorderedAccessBindPatch)) {
                logging::error(
                    "D3D11 detour activation/ownership validation failed: {} ({}).",
                    minHookStatusName(status),
                    static_cast<int>(status));
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    renderTargetBindCreated,
                    renderTargetAndUnorderedAccessBindCreated);
                return false;
            }

            void* createComputeShaderTrampoline{};
            status = MH_CreateHook(
                createComputeShaderTarget,
                reinterpret_cast<void*>(&hookCreateComputeShader),
                &createComputeShaderTrampoline);
            if (status != MH_OK ||
                !isExecutableAddress(createComputeShaderTrampoline)) {
                logging::error(
                    "CreateComputeShader detour creation/prologue validation failed: {} ({}), trampoline={}.",
                    minHookStatusName(status),
                    static_cast<int>(status),
                    createComputeShaderTrampoline);
                if (status == MH_OK) {
                    (void)MH_RemoveHook(createComputeShaderTarget);
                }
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    renderTargetBindCreated,
                    renderTargetAndUnorderedAccessBindCreated);
                return false;
            }
            originalCreateComputeShader =
                reinterpret_cast<CreateComputeShaderFunction>(
                    createComputeShaderTrampoline);
            status = MH_EnableHook(createComputeShaderTarget);
            if (status != MH_OK ||
                !captureMinHookPatchIdentity(
                    createComputeShaderTarget,
                    createComputeShaderPatch)) {
                logging::error(
                    "CreateComputeShader activation/ownership validation failed: {} ({}).",
                    minHookStatusName(status),
                    static_cast<int>(status));
                (void)MH_DisableHook(createComputeShaderTarget);
                (void)MH_RemoveHook(createComputeShaderTarget);
                originalCreateComputeShader = nullptr;
                rollbackMethodDetours(
                    createVertexShaderCreated,
                    createPixelShaderCreated,
                    vertexShaderBindCreated,
                    pixelShaderBindCreated,
                    renderTargetBindCreated,
                    renderTargetAndUnorderedAccessBindCreated);
                return false;
            }

            createVertexShaderDetourEnabled.store(
                true,
                std::memory_order_release);
            createPixelShaderDetourEnabled.store(true, std::memory_order_release);
            createComputeShaderDetourEnabled.store(
                true,
                std::memory_order_release);
            vertexShaderBindDetourEnabled.store(
                true,
                std::memory_order_release);
            pixelShaderBindDetourEnabled.store(true, std::memory_order_release);
            renderTargetBindDetourEnabled.store(
                true,
                std::memory_order_release);
            renderTargetAndUnorderedAccessBindDetourEnabled.store(
                true,
                std::memory_order_release);
            if (!installQualificationDrawDetours(context)) {
                qualificationDrawHookInstallFailures.fetch_add(
                    1,
                    std::memory_order_relaxed);
                logging::warn(
                    "D3D11 qualification draw detours remain unavailable; shader replacement stays active, but the automated report will fail closed at draw proof.");
            }
            if (!dlaa::installD3D11Hooks(context)) {
                logging::warn(
                    "DLAA native Map/Unmap qualification hooks remain unavailable; DLAA stays disabled while every existing shader path remains active.");
            }
            if (!vanilla_fixes::installFocusShadowNativeHooks()) {
                logging::warn(
                    "Vanilla Fixes focus-shadow map ownership is unavailable; other Vanilla Fixes remain active.");
            }
            logging::info(
                "Installed validated d3d11.dll method detours (CreateVertexShader target={}, CreatePixelShader target={}, CreateComputeShader target={}, VSSetShader target={}, PSSetShader target={}, OMSetRenderTargets target={}, OMSetRenderTargetsAndUnorderedAccessViews target={}); complete native COM vtables remain untouched.",
                createVertexShaderTarget,
                createPixelShaderTarget,
                createComputeShaderTarget,
                vertexShaderBindTarget,
                pixelShaderBindTarget,
                renderTargetBindTarget,
                renderTargetAndUnorderedAccessBindTarget);
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
            // Streamline's production bootstrap loads and authenticates its
            // feature DLLs. Keep that nested loader work out of
            // F4SEPlugin_Load, but still complete it before Fallout invokes
            // its first D3D11 creation API as required by manual-hooking mode.
            if (!dlaa::initializeStreamlineBeforeDevice()) {
                logging::warn(
                    "DLAA Streamline bootstrap failed at the pre-D3D11 boundary; native device creation continues and vanilla TAA remains active.");
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
            if (!dlaa::bindStreamlineDeviceAndSwapChain(
                    adapter,
                    *device,
                    swapChain)) {
                logging::warn(
                    "DLAA Streamline device/swapchain binding was rejected; vanilla TAA remains active.");
            }
            if (!installDeviceMethodDetours(*device, *immediateContext)) {
                shaderHookInstallFailures.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "D3D11 device captured, but method detours remain fail-closed and rendering stays vanilla.");
                return result;
            }

            if (!linear_lighting::installDFTiledPointLightHook()) {
                logging::warn(
                    "Verified DFTiled/Effect producer hook remains unavailable; native 2.2 producer gamma is retained and replacement frame constants remain fail-closed.");
            }

            linear_lighting::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            skylighting::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            if (!skylighting::installNativeHooks()) {
                logging::warn(
                    "Verified Skylighting precipitation capture hook remains unavailable; Skylighting stays fail-closed while other renderer features continue.");
            }
            surface_classification::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            wrapped_grass::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            hair_specular::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            basic_wetness::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            cloud_shadows::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            subsurface_scattering::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext);
            ibl::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            firstTrackedContactShaderBindLogged.store(
                false,
                std::memory_order_relaxed);
            firstDFPrePassDescriptorConsumeLogged.store(
                false,
                std::memory_order_relaxed);
            firstDFPrePassDescriptorExpiryLogged.store(
                false,
                std::memory_order_relaxed);
            firstDFPrePassTechniqueOverflowLogged.store(
                false,
                std::memory_order_relaxed);
            firstDFPrePassTechniqueMismatchLogged.store(
                false,
                std::memory_order_relaxed);
            activeDFPrePassTechniques = {};
            pendingDFPrePassDescriptor = {};
            contact_shadows::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            filmic_tonemapping::Runtime::get().onDeviceCreated(
                *device,
                *immediateContext,
                originalCreatePixelShader);
            dlaa::Runtime::get().onDeviceCreated(*device, *immediateContext);
            shaderInterceptionActive.store(true, std::memory_order_release);
            deviceHooksInstalled.store(true, std::memory_order_release);
            logging::info(
                "D3D11 device captured through Fallout4VR's verified creation import; shader interception is active.");
            return result;
        }
    }

    void beginDFPrePassTechnique(std::uint32_t descriptor) noexcept
    {
        linear_lighting::Runtime::get().observeDFPrePassDescriptor(
            descriptor);
        if (activeDFPrePassTechniques.depth >=
            activeDFPrePassTechniques.descriptors.size()) {
            activeDFPrePassTechniques = {};
            pendingDFPrePassDescriptor = {};
            if (!firstDFPrePassTechniqueOverflowLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::error(
                    "DFPrePass technique descriptor stack overflowed; classification failed closed until the next verified technique boundary.");
            }
            return;
        }
        activeDFPrePassTechniques.descriptors[
            activeDFPrePassTechniques.depth++] = descriptor;
        pendingDFPrePassDescriptor = { descriptor, true };
    }

    void endDFPrePassTechnique(std::uint32_t descriptor) noexcept
    {
        pendingDFPrePassDescriptor = {};
        if (activeDFPrePassTechniques.depth == 0) {
            return;
        }
        const auto top = activeDFPrePassTechniques.descriptors[
            activeDFPrePassTechniques.depth - 1];
        if (top != descriptor) {
            activeDFPrePassTechniques = {};
            if (!firstDFPrePassTechniqueMismatchLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::error(
                    "DFPrePass technique descriptor restore mismatch (active=0x{:08X}, restored=0x{:08X}); classification failed closed.",
                    top,
                    descriptor);
            }
            return;
        }
        --activeDFPrePassTechniques.depth;
        if (activeDFPrePassTechniques.depth != 0) {
            pendingDFPrePassDescriptor = {
                activeDFPrePassTechniques.descriptors[
                    activeDFPrePassTechniques.depth - 1],
                true,
            };
        }
    }

    void publishDFPrePassDescriptor(std::uint32_t descriptor) noexcept
    {
        pendingDFPrePassDescriptor = { descriptor, true };
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
            const auto createVertexOwned = active && detourPatchOwned(
                createVertexShaderTarget,
                createVertexShaderPatch);
            const auto createOwned = active && detourPatchOwned(
                createPixelShaderTarget,
                createPixelShaderPatch);
            const auto createComputeOwned = active && detourPatchOwned(
                createComputeShaderTarget,
                createComputeShaderPatch);
            const auto vertexBindOwned = active && detourPatchOwned(
                vertexShaderBindTarget,
                vertexShaderBindPatch);
            const auto bindOwned = active && detourPatchOwned(
                pixelShaderBindTarget,
                pixelShaderBindPatch);
            const auto renderTargetOwned = active && detourPatchOwned(
                renderTargetBindTarget,
                renderTargetBindPatch);
            const auto renderTargetAndUnorderedAccessOwned = active &&
                detourPatchOwned(
                    renderTargetAndUnorderedAccessBindTarget,
                    renderTargetAndUnorderedAccessBindPatch);
            createVertexShaderDetourEnabled.store(
                createVertexOwned,
                std::memory_order_release);
            createPixelShaderDetourEnabled.store(
                createOwned,
                std::memory_order_release);
            createComputeShaderDetourEnabled.store(
                createComputeOwned,
                std::memory_order_release);
            vertexShaderBindDetourEnabled.store(
                vertexBindOwned,
                std::memory_order_release);
            pixelShaderBindDetourEnabled.store(
                bindOwned,
                std::memory_order_release);
            renderTargetBindDetourEnabled.store(
                renderTargetOwned,
                std::memory_order_release);
            renderTargetAndUnorderedAccessBindDetourEnabled.store(
                renderTargetAndUnorderedAccessOwned,
                std::memory_order_release);
            const auto drawInstalled =
                qualificationDrawDetoursInstalled.load(
                    std::memory_order_acquire);
            const auto drawOwned = drawInstalled &&
                detourPatchOwned(drawIndexedTarget, drawIndexedPatch) &&
                detourPatchOwned(drawTarget, drawPatch) &&
                detourPatchOwned(
                    drawIndexedInstancedTarget,
                    drawIndexedInstancedPatch) &&
                detourPatchOwned(drawInstancedTarget, drawInstancedPatch);
            qualificationDrawDetoursOwned.store(
                drawOwned,
                std::memory_order_release);
            if (drawInstalled && !drawOwned) {
                const auto drawFailures =
                    qualificationDrawHookValidationFailures.fetch_add(
                        1,
                        std::memory_order_relaxed) +
                    1;
                if (drawFailures == 1 ||
                    (drawFailures & (drawFailures - 1)) == 0) {
                    logging::error(
                        "D3D11 qualification draw-detour ownership validation failed (trigger={}, failures={}); shader replacement remains active, but draw proof is unavailable.",
                        trigger ? trigger : "unknown",
                        drawFailures);
                }
            }
            if (createVertexOwned && createOwned && createComputeOwned &&
                vertexBindOwned &&
                bindOwned && renderTargetOwned &&
                renderTargetAndUnorderedAccessOwned) {
                return true;
            }

            const auto failures = shaderHookValidationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if (failures == 1 || (failures & (failures - 1)) == 0) {
                logging::error(
                    "D3D11 shader detour ownership validation failed (trigger={}, active={}, createVertexOwned={}, createOwned={}, createComputeOwned={}, vertexBindOwned={}, bindOwned={}, renderTargetOwned={}, renderTargetAndUnorderedAccessOwned={}, failures={}); no hook repair was attempted.",
                    trigger ? trigger : "unknown",
                    active,
                    createVertexOwned,
                    createOwned,
                    createComputeOwned,
                    vertexBindOwned,
                    bindOwned,
                    renderTargetOwned,
                    renderTargetAndUnorderedAccessOwned,
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
            .createVertexShaderDetourEnabled =
                createVertexShaderDetourEnabled.load(
                    std::memory_order_acquire),
            .createPixelShaderDetourEnabled =
                createPixelShaderDetourEnabled.load(std::memory_order_acquire),
            .createComputeShaderDetourEnabled =
                createComputeShaderDetourEnabled.load(
                    std::memory_order_acquire),
            .vertexShaderBindDetourEnabled =
                vertexShaderBindDetourEnabled.load(
                    std::memory_order_acquire),
            .pixelShaderBindDetourEnabled =
                pixelShaderBindDetourEnabled.load(std::memory_order_acquire),
            .renderTargetBindDetourEnabled =
                renderTargetBindDetourEnabled.load(
                    std::memory_order_acquire),
            .renderTargetAndUnorderedAccessBindDetourEnabled =
                renderTargetAndUnorderedAccessBindDetourEnabled.load(
                    std::memory_order_acquire),
            .qualificationDrawDetoursInstalled =
                qualificationDrawDetoursInstalled.load(
                    std::memory_order_acquire),
            .qualificationDrawDetoursOwned =
                qualificationDrawDetoursOwned.load(std::memory_order_acquire),
            .shaderHookInstallFailures =
                shaderHookInstallFailures.load(std::memory_order_relaxed),
            .shaderHookValidationFailures =
                shaderHookValidationFailures.load(std::memory_order_relaxed),
            .qualificationDrawHookInstallFailures =
                qualificationDrawHookInstallFailures.load(
                    std::memory_order_relaxed),
            .qualificationDrawHookValidationFailures =
                qualificationDrawHookValidationFailures.load(
                    std::memory_order_relaxed),
            .pixelShaderBindRecursions =
                pixelShaderBindRecursions.load(std::memory_order_relaxed),
            .deviceCreationCalls =
                deviceCreationCalls.load(std::memory_order_relaxed),
            .vertexShaderCreationCalls =
                vertexShaderCreationCalls.load(std::memory_order_relaxed),
            .pixelShaderCreationCalls =
                pixelShaderCreationCalls.load(std::memory_order_relaxed),
            .computeShaderCreationCalls =
                computeShaderCreationCalls.load(std::memory_order_relaxed),
            .vertexShaderBindCalls =
                vertexShaderBindCalls.load(std::memory_order_relaxed),
            .pixelShaderBindCalls =
                pixelShaderBindCalls.load(std::memory_order_relaxed),
            .renderTargetBindCalls =
                renderTargetBindCalls.load(std::memory_order_relaxed),
            .renderTargetAndUnorderedAccessBindCalls =
                renderTargetAndUnorderedAccessBindCalls.load(
                    std::memory_order_relaxed),
        };
    }

    std::uint64_t beginD3D11QualificationSession(
        std::uint64_t geometryUpdateBaseline) noexcept
    {
        qualificationSessionActive.store(false, std::memory_order_release);
        qualificationGeometryUpdateBaseline.store(
            geometryUpdateBaseline,
            std::memory_order_release);
        auto sessionId = qualificationSessionId.fetch_add(
                             1,
                             std::memory_order_acq_rel) +
            1;
        if (sessionId == 0) {
            sessionId = qualificationSessionId.fetch_add(
                            1,
                            std::memory_order_acq_rel) +
                1;
        }
        return sessionId;
    }

    void endD3D11QualificationSession(std::uint64_t sessionId) noexcept
    {
        if (sessionId != 0 &&
            qualificationSessionId.load(std::memory_order_acquire) ==
                sessionId) {
            qualificationSessionActive.store(false, std::memory_order_release);
        }
    }

    QualificationSnapshot d3d11QualificationSnapshot() noexcept
    {
        return {
            .sessionActive =
                qualificationSessionActive.load(std::memory_order_acquire),
            .sessionId =
                qualificationActivatedSessionId.load(
                    std::memory_order_acquire),
            .geometryUpdateBaseline =
                qualificationGeometryUpdateBaseline.load(
                    std::memory_order_acquire),
            .replacementShaderBinds =
                qualificationReplacementShaderBinds.load(
                    std::memory_order_relaxed),
            .drawIndexedCalls =
                qualificationDrawIndexedCalls.load(std::memory_order_relaxed),
            .drawCalls =
                qualificationDrawCalls.load(std::memory_order_relaxed),
            .drawIndexedInstancedCalls =
                qualificationDrawIndexedInstancedCalls.load(
                    std::memory_order_relaxed),
            .drawInstancedCalls =
                qualificationDrawInstancedCalls.load(
                    std::memory_order_relaxed),
            .replacementDrawCalls =
                qualificationReplacementDrawCalls.load(
                    std::memory_order_relaxed),
            .bindingStateChecks =
                qualificationBindingStateChecks.load(std::memory_order_relaxed),
            .bindingStateFailures =
                qualificationBindingStateFailures.load(
                    std::memory_order_relaxed),
            .drawStateChecks =
                qualificationDrawStateChecks.load(std::memory_order_relaxed),
            .drawStateFailures =
                qualificationDrawStateFailures.load(std::memory_order_relaxed),
            .bindingsWithoutFreshGeometry =
                qualificationBindingsWithoutFreshGeometry.load(
                    std::memory_order_relaxed),
            .drawsWithoutFreshGeometry =
                qualificationDrawsWithoutFreshGeometry.load(
                    std::memory_order_relaxed),
            .replacementContractMask =
                qualificationReplacementContractMask.load(
                    std::memory_order_relaxed),
            .bindingVerifiedContractMask =
                qualificationBindingVerifiedContractMask.load(
                    std::memory_order_acquire),
            .drawVerifiedContractMask =
                qualificationDrawVerifiedContractMask.load(
                    std::memory_order_acquire),
            .lastBindingState =
                qualificationLastBindingState.load(std::memory_order_relaxed),
            .lastDrawState =
                qualificationLastDrawState.load(std::memory_order_relaxed),
        };
    }
}
