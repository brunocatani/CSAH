#pragma once
#include "PCH.h"

namespace Hooks {
    void InstallAll();
    void InstallShaderHooks();
    void InstallRenderHooks();
    void InstallD3DHooks();

    // --- Hook function types ---

    // BSShader::BeginTechnique at RVA 0x2814BE0
    // 6 params: this, vsTechID, hsTechID, dsTechID, psTechID, BSRenderPass*
    using BeginTechnique_t = bool(__fastcall*)(void* shader, uint32_t vsTechID,
        uint32_t hsTechID, uint32_t dsTechID, uint32_t psTechID, void* renderPass);
    inline BeginTechnique_t OriginalBeginTechnique = nullptr;

    // BSLightingShader::SetupGeometry at vtable[7]
    using SetupGeometry_t = void(__fastcall*)(void* shader, void* renderPass);
    inline SetupGeometry_t OriginalLightingSetupGeometry = nullptr;

    // BSLightingShader::SetupMaterial at vtable[4]
    using SetupMaterial_t = void(__fastcall*)(void* shader, void* material);
    inline SetupMaterial_t OriginalLightingSetupMaterial = nullptr;

    // BSShader::LoadShaders at RVA 0x27F4800
    using LoadShaders_t = void(__fastcall*)(void* shader);
    inline LoadShaders_t OriginalLoadShaders = nullptr;

    // IDXGISwapChain::Present
    using Present_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    inline Present_t OriginalPresent = nullptr;
}
