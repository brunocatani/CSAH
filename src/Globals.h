#pragma once
#include "PCH.h"

namespace Globals {
    void Initialize();
    bool IsInitialized();

    // D3D11 — from Ghidra RE of FO4VR D3D11CreateDeviceAndSwapChain call
    ID3D11Device* GetDevice();           // base+0x60F3CF0 (DAT_1460f3cf0)
    ID3D11DeviceContext* GetContext();    // base+0x60F3D08 (DAT_1460f3d08)

    // Renderer
    uintptr_t GetRenderer();             // base+0x60F3CE8 (DAT_1460f3ce8)
    uintptr_t GetRenderManager();        // base+0x6239340
    uintptr_t GetGraphicsState();        // base+0x6235AC0

    // Shader singletons
    uintptr_t GetBSLightingShaderVtable(); // base+0x30BF3C8 (VR Extended)
    uintptr_t GetBSLightingShader();       // base+0x689b8a0 (VR Extended renderer)
    uintptr_t GetBSLightingShaderAccum();  // base+0x689b410 (Lighting accumulation object, scatter tables)
    uintptr_t GetBSGrassShaderVtable();    // base+0x3098DA8
    uintptr_t GetBSGrassShader();          // base+0x6732948
    uintptr_t GetImageSpaceManager();      // base+0x6732C38

    // VR state
    bool IsVR();
    uintptr_t GetIVRSystem();              // base+0x5bbeab0
    uint32_t* GetVREyeIndex();             // base+0x391e750
    uint32_t* GetVRStereoFlag();           // base+0x391e738

    // Sky/Weather
    uintptr_t GetSky();                    // base+0x6235AC8 (VR preferred)
    uintptr_t GetShadowStateRing();        // base+0x68780D0
    float* GetShadowDistanceCache();       // base+0x68788f0
    uint32_t* GetCascadeCountGlobal();     // base+0x3924818

    // Shadow scene nodes (for cascade runtime)
    uintptr_t GetShadowSceneNode();        // base+0x6879520 (render)
    uintptr_t GetShadowSceneNode2();       // base+0x6885d40 (setup, VR-only)

    // VR cascade array
    uintptr_t GetVRCascadeArrayPtr();      // base+0x6878b18
    uint32_t* GetVRCascadeArrayCount();    // base+0x6878b28

    // Cascade mask global
    uint32_t* GetCascadeMaskGlobal();      // base+0x6885cc4

    // Render targets
    uintptr_t GetRenderTargetManager();    // base+0x38ac010

    // Fog globals region
    uintptr_t GetFogGlobals();             // base+0x65A2AC4

    // Module base (cached)
    uintptr_t GetBase();
}
