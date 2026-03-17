#include "Hooks.h"
#include "Globals.h"

namespace {

    // ---- Hook thunks (pass-through for now) ----

    static bool __fastcall Hook_BeginTechnique(void* shader, uint32_t vsTechID,
        uint32_t hsTechID, uint32_t dsTechID, uint32_t psTechID, void* renderPass)
    {
        return Hooks::OriginalBeginTechnique(shader, vsTechID, hsTechID, dsTechID, psTechID, renderPass);
    }

    static void __fastcall Hook_LightingSetupGeometry(void* shader, void* renderPass)
    {
        Hooks::OriginalLightingSetupGeometry(shader, renderPass);
        // Feature callbacks will be added in integration task
    }

    static void __fastcall Hook_LightingSetupMaterial(void* shader, void* material)
    {
        Hooks::OriginalLightingSetupMaterial(shader, material);
        // Feature callbacks will be added in integration task
    }

    static void __fastcall Hook_LoadShaders(void* shader)
    {
        spdlog::info("BSShader::LoadShaders called for shader at {:X}", reinterpret_cast<uintptr_t>(shader));
        Hooks::OriginalLoadShaders(shader);
    }

    static HRESULT __stdcall Hook_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        // Menu draw will be added in integration task
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
        spdlog::info("Hooks::InstallShaderHooks — installing Detour hooks...");

        auto base = Globals::GetBase();

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
        spdlog::info("Hooks::InstallRenderHooks — installing vtable hooks...");

        auto vtable = reinterpret_cast<uintptr_t*>(Globals::GetBSLightingShaderVtable());
        if (!vtable) {
            spdlog::error("  BSLightingShader vtable is null — skipping render hooks");
            return;
        }

        // --- SetupGeometry at vtable[7] (byte offset +0x38) ---
        void* origGeom = nullptr;
        if (PatchVtableEntry(vtable, 7, reinterpret_cast<void*>(Hook_LightingSetupGeometry), &origGeom)) {
            OriginalLightingSetupGeometry = reinterpret_cast<SetupGeometry_t>(origGeom);
            spdlog::info("  Hooked BSLightingShader::SetupGeometry (vtable[7]) — original {:X}",
                reinterpret_cast<uintptr_t>(origGeom));
        } else {
            spdlog::error("  Failed to hook BSLightingShader::SetupGeometry");
        }

        // --- SetupMaterial at vtable[4] (byte offset +0x20) ---
        void* origMat = nullptr;
        if (PatchVtableEntry(vtable, 4, reinterpret_cast<void*>(Hook_LightingSetupMaterial), &origMat)) {
            OriginalLightingSetupMaterial = reinterpret_cast<SetupMaterial_t>(origMat);
            spdlog::info("  Hooked BSLightingShader::SetupMaterial (vtable[4]) — original {:X}",
                reinterpret_cast<uintptr_t>(origMat));
        } else {
            spdlog::error("  Failed to hook BSLightingShader::SetupMaterial");
        }
    }

    void InstallD3DHooks()
    {
        spdlog::info("Hooks::InstallD3DHooks — installing D3D vtable hooks...");

        // Get swap chain: renderer singleton at base+0x609BF80, dereference, then offset +0x70
        auto renderer = Globals::GetRenderer();
        if (!renderer) {
            spdlog::error("  Renderer singleton is null — skipping D3D hooks");
            return;
        }

        auto swapChainPtr = reinterpret_cast<IDXGISwapChain**>(renderer + 0x70);
        IDXGISwapChain* swapChain = *swapChainPtr;
        if (!swapChain) {
            spdlog::error("  IDXGISwapChain is null — skipping D3D hooks");
            return;
        }

        spdlog::info("  SwapChain at {:X}", reinterpret_cast<uintptr_t>(swapChain));

        // IDXGISwapChain vtable: Present is entry [8] (byte offset +64)
        auto swapChainVtable = *reinterpret_cast<uintptr_t**>(swapChain);

        void* origPresent = nullptr;
        if (PatchVtableEntry(swapChainVtable, 8, reinterpret_cast<void*>(Hook_Present), &origPresent)) {
            OriginalPresent = reinterpret_cast<Present_t>(origPresent);
            spdlog::info("  Hooked IDXGISwapChain::Present (vtable[8]) — original {:X}",
                reinterpret_cast<uintptr_t>(origPresent));
        } else {
            spdlog::error("  Failed to hook IDXGISwapChain::Present");
        }
    }

    void InstallAll()
    {
        spdlog::info("Hooks::InstallAll — beginning hook installation...");

        InstallShaderHooks();
        InstallRenderHooks();
        InstallD3DHooks();

        spdlog::info("Hooks::InstallAll — hook installation complete");
    }

}  // namespace Hooks
