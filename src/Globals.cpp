#include "Globals.h"

namespace Globals {

    static uintptr_t s_base = 0;
    static bool s_initialized = false;

    void Initialize()
    {
        if (s_initialized) {
            return;
        }

        s_base = REL::Module::get().base();
        s_initialized = true;

        spdlog::info("Globals::Initialize — module base: {:#x}", s_base);

        // Log D3D pointers
        spdlog::info("  Renderer:              {:#x}", GetRenderer());
        spdlog::info("  Device:                {}", fmt::ptr(GetDevice()));
        spdlog::info("  Context:               {}", fmt::ptr(GetContext()));
        spdlog::info("  RenderManager:         {:#x}", GetRenderManager());
        spdlog::info("  GraphicsState:         {:#x}", GetGraphicsState());

        // Log shader singletons
        spdlog::info("  BSLightingShaderVtbl:  {:#x}", GetBSLightingShaderVtable());
        spdlog::info("  BSLightingShader:      {:#x}", GetBSLightingShader());
        spdlog::info("  BSGrassShaderVtbl:     {:#x}", GetBSGrassShaderVtable());
        spdlog::info("  BSGrassShader:         {:#x}", GetBSGrassShader());
        spdlog::info("  ImageSpaceManager:     {:#x}", GetImageSpaceManager());

        // Log VR state
        spdlog::info("  IsVR:                  {}", IsVR());
        spdlog::info("  IVRSystem:             {:#x}", GetIVRSystem());
        spdlog::info("  VREyeIndex addr:       {}", fmt::ptr(GetVREyeIndex()));
        spdlog::info("  VRStereoFlag addr:     {}", fmt::ptr(GetVRStereoFlag()));

        // Log sky/weather
        spdlog::info("  Sky:                   {:#x}", GetSky());
        spdlog::info("  ShadowStateRing:       {:#x}", GetShadowStateRing());
        spdlog::info("  ShadowDistCache addr:  {}", fmt::ptr(GetShadowDistanceCache()));
        spdlog::info("  CascadeCount addr:     {}", fmt::ptr(GetCascadeCountGlobal()));

        // Log render targets & fog
        spdlog::info("  RenderTargetManager:   {:#x}", GetRenderTargetManager());
        spdlog::info("  FogGlobals:            {:#x}", GetFogGlobals());
    }

    bool IsInitialized()
    {
        return s_initialized;
    }

    // ---- Module base ----

    uintptr_t GetBase()
    {
        return s_base;
    }

    // ---- D3D11 (pointer dereference from Renderer singleton) ----

    ID3D11Device* GetDevice()
    {
        auto ptr = reinterpret_cast<ID3D11Device**>(s_base + 0x609BF88);
        return ptr ? *ptr : nullptr;
    }

    ID3D11DeviceContext* GetContext()
    {
        auto ptr = reinterpret_cast<ID3D11DeviceContext**>(s_base + 0x609BF98);
        return ptr ? *ptr : nullptr;
    }

    // ---- Renderer ----

    uintptr_t GetRenderer()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x609BF80);
        return ptr ? *ptr : 0;
    }

    uintptr_t GetRenderManager()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6239340);
        return ptr ? *ptr : 0;
    }

    uintptr_t GetGraphicsState()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6235AC0);
        return ptr ? *ptr : 0;
    }

    // ---- Shader singletons ----

    uintptr_t GetBSLightingShaderVtable()
    {
        return s_base + 0x309AAB8;
    }

    uintptr_t GetBSLightingShader()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6733100);
        return ptr ? *ptr : 0;
    }

    uintptr_t GetBSGrassShaderVtable()
    {
        return s_base + 0x3098DA8;
    }

    uintptr_t GetBSGrassShader()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6732948);
        return ptr ? *ptr : 0;
    }

    uintptr_t GetImageSpaceManager()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6732C38);
        return ptr ? *ptr : 0;
    }

    // ---- VR state ----

    bool IsVR()
    {
        return REL::Module::IsVR();
    }

    uintptr_t GetIVRSystem()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x5BBEAB0);
        return ptr ? *ptr : 0;
    }

    uint32_t* GetVREyeIndex()
    {
        return reinterpret_cast<uint32_t*>(s_base + 0x391E750);
    }

    uint32_t* GetVRStereoFlag()
    {
        return reinterpret_cast<uint32_t*>(s_base + 0x391E738);
    }

    // ---- Sky/Weather ----

    uintptr_t GetSky()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6235AC8);
        return ptr ? *ptr : 0;
    }

    uintptr_t GetShadowStateRing()
    {
        return s_base + 0x68780D0;
    }

    float* GetShadowDistanceCache()
    {
        return reinterpret_cast<float*>(s_base + 0x68788F0);
    }

    uint32_t* GetCascadeCountGlobal()
    {
        return reinterpret_cast<uint32_t*>(s_base + 0x3924818);
    }

    // ---- Shadow scene nodes ----

    uintptr_t GetShadowSceneNode()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6879520);
        return s_base ? *ptr : 0;
    }

    uintptr_t GetShadowSceneNode2()
    {
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x6885d40);
        return s_base ? *ptr : 0;
    }

    // ---- VR cascade array ----

    uintptr_t GetVRCascadeArrayPtr()
    {
        return s_base ? s_base + 0x6878b18 : 0;
    }

    uint32_t* GetVRCascadeArrayCount()
    {
        return s_base ? reinterpret_cast<uint32_t*>(s_base + 0x6878b28) : nullptr;
    }

    // ---- Cascade mask ----

    uint32_t* GetCascadeMaskGlobal()
    {
        return s_base ? reinterpret_cast<uint32_t*>(s_base + 0x6885cc4) : nullptr;
    }

    // ---- Render targets ----

    uintptr_t GetRenderTargetManager()
    {
        return s_base + 0x38AC010;
    }

    // ---- Fog globals ----

    uintptr_t GetFogGlobals()
    {
        return s_base + 0x65A2AC4;
    }

}
