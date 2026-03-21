#include "Hooks.h"
#include "Globals.h"
#include "State.h"
#include "Feature.h"
#include "Menu.h"
#include "EngineFixes.h"
#include "ShaderCache.h"
#include <imgui.h>
#include <atomic>
#include "ShaderReplacer.h"

namespace {

    // ---- Forward declarations for deferred D3D init ----
    static bool s_deferredD3DInitDone = false;
    static std::atomic<bool> s_shaderReplacementDone{false};
    static void* s_capturedBSLightingShader = nullptr;
    static void TryDeferredD3DInit();

    // ---- Hook thunks ----

    static bool __fastcall Hook_BeginTechnique(void* shader, uint32_t vsTechID,
        uint32_t hsTechID, uint32_t dsTechID, uint32_t psTechID, void* renderPass)
    {
        // Fallback deferred D3D init — BeginTechnique fires during rendering when D3D is definitely ready
        if (!s_deferredD3DInitDone) {
            TryDeferredD3DInit();
        }

        // Get BSLightingShader directly from its singleton (found via Ghidra constructor)
        // Singleton at base+0x689b410 (DAT_14689b410 in Ghidra decompilation)
        if (!s_shaderReplacementDone && Globals::GetDevice()) {
            auto base = REL::Module::get().base();
            auto singletonPtr = reinterpret_cast<void**>(base + 0x689b410);
            void* bsLighting = singletonPtr ? *singletonPtr : nullptr;
            if (bsLighting) {
                auto bsAddr = reinterpret_cast<uintptr_t>(bsLighting);

                // Check if scatter tables are populated (FXP loaded)
                uint32_t psCount = *reinterpret_cast<uint32_t*>(bsAddr + 0xB8 + 0x04);
                auto* psBuckets = *reinterpret_cast<void**>(bsAddr + 0xB8 + 0x20);

                if (psCount > 0 && psBuckets) {
                    bool expected = false;
                    if (!s_shaderReplacementDone.compare_exchange_strong(expected, true))
                        return Hooks::OriginalBeginTechnique(shader, vsTechID, hsTechID, dsTechID, psTechID, renderPass);
                    s_capturedBSLightingShader = bsLighting;
                    spdlog::info("BeginTechnique: BSLightingShader at {} — PS scatter populated (count={}, buckets={})",
                                 fmt::ptr(bsLighting), psCount, fmt::ptr(psBuckets));
                    spdlog::info("  Triggering filtered replacement for parallax permutations");
                    ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(bsLighting, 8,
                        [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
                }
                // else: scatter tables not populated yet, will retry next frame
            }
        }

        auto& state = State::GetSingleton();
        state.currentShader = shader;
        state.currentTechniqueID = psTechID;
        return Hooks::OriginalBeginTechnique(shader, vsTechID, hsTechID, dsTechID, psTechID, renderPass);
    }

    static void __fastcall Hook_LightingSetupGeometry(void* shader, void* renderPass)
    {
        Hooks::OriginalLightingSetupGeometry(shader, renderPass);

        // First call: capture BSLightingShader and trigger shader replacement
        if (!s_shaderReplacementDone && shader && Globals::GetDevice()) {
            s_shaderReplacementDone = true;
            s_capturedBSLightingShader = shader;
            spdlog::info("SetupGeometry: Captured BSLightingShader at {}, triggering shader replacement",
                         fmt::ptr(shader));
            ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(shader, 8,
                [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
        }

        // Bind shared data CB
        State::GetSingleton().BindSharedData();

        // Feature callbacks
        for (auto* f : Feature::GetFeatureList()) {
            if (f->loaded && f->enabled) {
                f->OnSetupGeometry(renderPass);
            }
        }
    }

    static void __fastcall Hook_LightingSetupMaterial(void* shader, void* material)
    {
        Hooks::OriginalLightingSetupMaterial(shader, material);

        // Feature callbacks
        for (auto* f : Feature::GetFeatureList()) {
            if (f->loaded && f->enabled) {
                f->OnSetupMaterial(material);
            }
        }
    }

    static void TryDeferredD3DInit()
    {
        if (s_deferredD3DInitDone) return;

        // Re-probe globals — device may have become available since kGameDataReady
        if (!Globals::GetDevice()) {
            Globals::Initialize();  // Re-probe
            if (!Globals::GetDevice()) return;  // Still not ready
        }

        auto* device = Globals::GetDevice();
        auto* context = Globals::GetContext();
        spdlog::info("=== Deferred D3D init (device now available) ===");
        spdlog::info("  Device: {}, Context: {}", fmt::ptr(device), fmt::ptr(context));
        spdlog::info("  Renderer: {:#x}", Globals::GetRenderer());
        spdlog::info("  BSLightingShader: {:#x}", Globals::GetBSLightingShader());
        s_deferredD3DInitDone = true;

        State::GetSingleton().Initialize();
        Feature::InitializeAll();
        Hooks::InstallRenderHooks();
        Hooks::InstallD3DHooks();
        EngineFixes::ApplyPostLoadFixes();
        EngineFixes::StartCascadeRuntime();

        // Trigger shader replacement using captured pointer from Hook_LoadShaders
        if (s_capturedBSLightingShader) {
            spdlog::info("Triggering deferred shader replacement for BSLightingShader at {}",
                         fmt::ptr(s_capturedBSLightingShader));
            ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(
                s_capturedBSLightingShader, 8,
                [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
        } else {
            spdlog::warn("BSLightingShader not yet captured from Hook_LoadShaders");
        }

        Feature::SaveAllSettings("Data/CommunityShaders/Settings/CommunityShaders.json");
        spdlog::info("=== Deferred D3D init complete ===");
    }

    static void __fastcall Hook_LoadShaders(void* shader)
    {
        Hooks::OriginalLoadShaders(shader);

        // After the game loads this shader's FXP, attempt replacement
        // BSShader+0x00 is the vtable pointer — compare to known vtable addresses
        if (!shader) return;

        // Try deferred D3D init on each LoadShaders call — device may become available
        // between kGameDataReady and the end of shader loading
        if (!s_deferredD3DInitDone) {
            TryDeferredD3DInit();
            if (!s_deferredD3DInitDone) return;  // Still not ready
        }

        auto vtablePtr = *reinterpret_cast<uintptr_t*>(shader);
        auto base = REL::Module::get().base();
        if (!base) return;

        if (vtablePtr == base + 0x30bbdb8) {  // BSLightingShader VR vtable (from Ghidra constructor)
            // BSLightingShader (type 8) — always capture the pointer
            s_capturedBSLightingShader = shader;
            spdlog::info("BSShader::LoadShaders — BSLightingShader captured at {}", fmt::ptr(shader));

            // Only do shader replacement if D3D init is done
            if (s_deferredD3DInitDone && Globals::GetDevice()) {
                spdlog::info("  Triggering filtered replacement (POM permutations only)");
                ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(shader, 8,
                    [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
            } else {
                spdlog::info("  Deferring replacement (D3D not ready or init pending)");
            }
        } else if (vtablePtr == base + 0x3098DA8) {
            // BSGrassShader (type 6) — future use
            spdlog::info("BSShader::LoadShaders — BSGrassShader loaded at {}", fmt::ptr(shader));
            // Future: ShaderReplacer::GetSingleton().ReplaceAllPermutations(shader, 6);
        } else {
            spdlog::debug("BSShader::LoadShaders — shader at {} vtable {:X}",
                          fmt::ptr(shader), vtablePtr);
        }
    }

    static LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto& menu = Menu::GetSingleton();
        if (menu.initialized && menu.isVisible) {
            menu.ProcessWndProc(hwnd, msg, wParam, lParam);

            ImGuiIO& io = ImGui::GetIO();
            if ((io.WantCaptureKeyboard && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) ||
                (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST))) {
                return 1;
            }
        }

        return CallWindowProcA(Hooks::OriginalWndProc, hwnd, msg, wParam, lParam);
    }

    static HRESULT __stdcall Hook_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        static bool menuInitialized = false;

        if (!menuInitialized) {
            // Initialize Menu on first Present call (device/context now guaranteed valid)
            DXGI_SWAP_CHAIN_DESC desc{};
            swapChain->GetDesc(&desc);
            Menu::GetSingleton().Initialize(desc.OutputWindow, Globals::GetDevice(), Globals::GetContext());

            // Subclass the game window to forward input to ImGui
            Hooks::OriginalWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(desc.OutputWindow, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(Hook_WndProc)));
            if (Hooks::OriginalWndProc) {
                spdlog::info("  Subclassed WndProc for ImGui input");
            } else {
                spdlog::error("  Failed to subclass WndProc — menu input will not work");
            }

            menuInitialized = true;
        }

        // Retry shader replacement — check both base (+0xB8) and custom (+0x120) scatter tables
        if (!s_shaderReplacementDone.load()) {
            static uint32_t s_frameCounter = 0;
            ++s_frameCounter;
            auto base = REL::Module::get().base();
            auto singletonPtr = reinterpret_cast<void**>(base + 0x689b410);
            void* bsLighting = singletonPtr ? *singletonPtr : nullptr;
            if (bsLighting && Globals::GetDevice()) {
                auto bsAddr = reinterpret_cast<uintptr_t>(bsLighting);

                // Check BOTH possible scatter table locations
                uint32_t psCountBase = *reinterpret_cast<uint32_t*>(bsAddr + 0xB8 + 0x04);
                auto* psBucketsBase = *reinterpret_cast<void**>(bsAddr + 0xB8 + 0x20);

                // Custom table from BSLightingShader constructor:
                // capacity at +0x124, buckets at +0x140
                uint32_t psCountCustom = *reinterpret_cast<uint32_t*>(bsAddr + 0x124);
                auto* psBucketsCustom = *reinterpret_cast<void**>(bsAddr + 0x140);

                // Log every 300 frames (~5 sec) to track state
                if (s_frameCounter % 300 == 1) {
                    spdlog::info("Present[{}]: BSLighting scatter check — base(count={},buckets={}) custom(count={},buckets={})",
                                 s_frameCounter, psCountBase, fmt::ptr(psBucketsBase),
                                 psCountCustom, fmt::ptr(psBucketsCustom));
                }

                // Try base table first, then custom
                void* targetShader = nullptr;
                if (psCountBase > 0 && psBucketsBase) {
                    targetShader = bsLighting;
                    spdlog::info("Present: BASE scatter table populated! count={}", psCountBase);
                } else if (psCountCustom > 0 && psBucketsCustom) {
                    // Custom table found — need to update kPSTableOffset
                    targetShader = bsLighting;
                    spdlog::info("Present: CUSTOM scatter table populated! count={} at +0x120", psCountCustom);
                }

                if (targetShader) {
                    bool expected = false;
                    if (s_shaderReplacementDone.compare_exchange_strong(expected, true)) {
                        s_capturedBSLightingShader = targetShader;
                        ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(targetShader, 8,
                            [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
                    }
                }
            }
        }

        // Per-frame updates (UpdatePerFrame increments frameCount internally)
        auto& state = State::GetSingleton();
        state.UpdatePerFrame();

        // Feature lifecycle
        Feature::ResetAll();
        Feature::PrepassAll();

        // Toggle menu with F10
        if (GetAsyncKeyState(VK_F10) & 1) {
            Menu::GetSingleton().Toggle();
        }

        // Draw menu overlay
        Menu::GetSingleton().Draw();

        return Hooks::OriginalPresent(swapChain, syncInterval, flags);
    }

    // ---- Helpers ----

    bool PatchVtableEntry(uintptr_t* vtable, size_t index, void* newFunc, void** originalOut)
    {
        *originalOut = reinterpret_cast<void*>(vtable[index]);

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            spdlog::error("VirtualProtect failed for vtable[{}] at {:X}", index,
                reinterpret_cast<uintptr_t>(&vtable[index]));
            return false;
        }

        vtable[index] = reinterpret_cast<uintptr_t>(newFunc);

        VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }

}  // anonymous namespace

namespace Hooks {

    void InstallShaderHooks()
    {
        spdlog::info("Hooks::InstallShaderHooks - installing Detour hooks...");

        // Get base directly — Globals::Initialize() hasn't run yet at kPostPostLoad time
        auto base = REL::Module::get().base();
        spdlog::info("  Module base for hooks: {:#x}", base);
        if (!base) {
            spdlog::error("  Module base is null — cannot install hooks");
            return;
        }

        // --- BeginTechnique at base+0x2814BE0 ---
        OriginalBeginTechnique = reinterpret_cast<BeginTechnique_t>(base + 0x2814BE0);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&OriginalBeginTechnique), Hook_BeginTechnique);
        LONG result = DetourTransactionCommit();

        if (result == NO_ERROR) {
            spdlog::info("  Hooked BeginTechnique at {:X}", base + 0x2814BE0);
        } else {
            spdlog::error("  Failed to hook BeginTechnique: error {}", result);
        }

        // --- LoadShaders at base+0x27F4800 ---
        OriginalLoadShaders = reinterpret_cast<LoadShaders_t>(base + 0x27F4800);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&OriginalLoadShaders), Hook_LoadShaders);
        result = DetourTransactionCommit();

        if (result == NO_ERROR) {
            spdlog::info("  Hooked LoadShaders at {:X}", base + 0x27F4800);
        } else {
            spdlog::error("  Failed to hook LoadShaders: error {}", result);
        }
    }

    void InstallRenderHooks()
    {
        spdlog::info("Hooks::InstallRenderHooks - installing vtable hooks...");

        auto vtable = reinterpret_cast<uintptr_t*>(Globals::GetBSLightingShaderVtable());
        if (!vtable) {
            spdlog::error("  BSLightingShader vtable is null - skipping render hooks");
            return;
        }

        // --- SetupGeometry at vtable[7] (byte offset +0x38) ---
        void* origGeom = nullptr;
        if (PatchVtableEntry(vtable, 7, reinterpret_cast<void*>(Hook_LightingSetupGeometry), &origGeom)) {
            OriginalLightingSetupGeometry = reinterpret_cast<SetupGeometry_t>(origGeom);
            spdlog::info("  Hooked BSLightingShader::SetupGeometry (vtable[7]) - original {:X}",
                reinterpret_cast<uintptr_t>(origGeom));
        } else {
            spdlog::error("  Failed to hook BSLightingShader::SetupGeometry");
        }

        // --- SetupMaterial — DISABLED until vtable index verified via Ghidra ---
        // Testing showed vtable[4] contains non-code data (0x73657A6973 = ASCII "sizes").
        // No features currently use OnSetupMaterial(), safe to skip.
        // TODO: Verify correct vtable index for BSLightingShader::SetupMaterial (RVA 0x289D510)
        spdlog::info("  SetupMaterial hook SKIPPED (vtable index needs Ghidra verification)");
    }

    void InstallD3DHooks()
    {
        spdlog::info("Hooks::InstallD3DHooks - installing D3D vtable hooks...");

        // Get swap chain: renderer at base+0x60F3CE8, dereference, SwapChain at renderer+0x70
        auto renderer = Globals::GetRenderer();
        if (!renderer) {
            spdlog::error("  Renderer singleton is null - skipping D3D hooks");
            return;
        }

        auto swapChainPtr = reinterpret_cast<IDXGISwapChain**>(renderer + 0x70);
        IDXGISwapChain* swapChain = *swapChainPtr;
        if (!swapChain) {
            spdlog::error("  IDXGISwapChain is null - skipping D3D hooks");
            return;
        }

        spdlog::info("  SwapChain at {:X}", reinterpret_cast<uintptr_t>(swapChain));

        // IDXGISwapChain vtable: Present is entry [8] (byte offset +64)
        auto swapChainVtable = *reinterpret_cast<uintptr_t**>(swapChain);
        if (!swapChainVtable) {
            spdlog::error("  SwapChain vtable pointer is null (D3D not fully initialized?) - skipping D3D hooks");
            return;
        }

        void* origPresent = nullptr;
        if (PatchVtableEntry(swapChainVtable, 8, reinterpret_cast<void*>(Hook_Present), &origPresent)) {
            OriginalPresent = reinterpret_cast<Present_t>(origPresent);
            spdlog::info("  Hooked IDXGISwapChain::Present (vtable[8]) - original {:X}",
                reinterpret_cast<uintptr_t>(origPresent));
        } else {
            spdlog::error("  Failed to hook IDXGISwapChain::Present");
        }
    }

    void MarkDeferredInitDone()
    {
        s_deferredD3DInitDone = true;
    }

    void InstallAll()
    {
        spdlog::info("Hooks::InstallAll - beginning hook installation...");

        InstallShaderHooks();
        InstallRenderHooks();
        InstallD3DHooks();

        spdlog::info("Hooks::InstallAll - hook installation complete");
    }

}  // namespace Hooks
