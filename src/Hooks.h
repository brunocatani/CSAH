#pragma once
#include "PCH.h"

namespace Hooks {
    void InstallAll();
    void InstallShaderHooks();
    void InstallRenderHooks();
    void InstallD3DHooks();
    void InstallEarlyD3DHook();  // Call from F4SEPlugin_Load (before FXP loading)
    void MarkDeferredInitDone();

    // --- Hook function types ---

    // VR Extended BSLightingShader::BeginTechnique at RVA 0x291DA20
    // Signature: char(this, combinedTechID, renderPass) — 3 params
    // This is the RENDERER object, NOT the "Lighting" accumulation object (0x28B5C10)
    using LightingBeginTechnique_t = char(__fastcall*)(void* shader, uint32_t techID, void* renderPass);
    inline LightingBeginTechnique_t OriginalLightingBeginTechnique = nullptr;

    // VR Extended BSLightingShader::SetupGeometry at vtable[9] (offset +0x48)
    using SetupGeometry_t = void(__fastcall*)(void* shader, void* renderPass);
    inline SetupGeometry_t OriginalLightingSetupGeometry = nullptr;

    // BSLightingShader::SetupMaterial at vtable[6] — UNUSED, kept for reference
    using SetupMaterial_t = void(__fastcall*)(void* shader, void* material);
    inline SetupMaterial_t OriginalLightingSetupMaterial = nullptr;

    // ID3D11DeviceContext::PSSetShader — vtable[9]
    using PSSetShader_t = void(__fastcall*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
    inline PSSetShader_t OriginalPSSetShader = nullptr;

    // ID3D11Device::CreatePixelShader — vtable[15]
    using CreatePixelShader_t = HRESULT(__fastcall*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
    inline CreatePixelShader_t OriginalCreatePixelShader = nullptr;

    // IDXGISwapChain::Present
    using Present_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    inline Present_t OriginalPresent = nullptr;

    // Original WndProc (captured via SetWindowLongPtrA)
    inline WNDPROC OriginalWndProc = nullptr;
}
