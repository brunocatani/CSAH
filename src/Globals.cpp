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

    // Helper: check if a pointer looks like a valid heap/COM object (not garbage)
    static bool IsValidPointer(uintptr_t ptr)
    {
        // Must be above 64KB (below is reserved), must be in user-mode range,
        // and must be 8-byte aligned (COM objects are always aligned)
        return ptr > 0x10000 && ptr < 0x00007FFFFFFFFFFF && (ptr & 0x7) == 0;
    }

    ID3D11Device* GetDevice()
    {
        // Read from global pointer location
        // From Ghidra: DAT_1460f3cf0 = *(renderer+0x48) = Device
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x60F3CF0);
        if (!ptr) return nullptr;
        auto device = reinterpret_cast<ID3D11Device*>(*ptr);
        if (!device || !IsValidPointer(reinterpret_cast<uintptr_t>(device))) return nullptr;
        auto vtable = *reinterpret_cast<uintptr_t*>(device);
        if (!IsValidPointer(vtable)) return nullptr;
        return device;
    }

    ID3D11DeviceContext* GetContext()
    {
        // From Ghidra: DAT_1460f3d08 = *(renderer+0x50) = Context
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x60F3D08);
        if (!ptr) return nullptr;
        auto ctx = reinterpret_cast<ID3D11DeviceContext*>(*ptr);
        if (!ctx || !IsValidPointer(reinterpret_cast<uintptr_t>(ctx))) return nullptr;
        auto vtable = *reinterpret_cast<uintptr_t*>(ctx);
        if (!IsValidPointer(vtable)) return nullptr;
        return ctx;
    }

    // ---- Renderer ----

    uintptr_t GetRenderer()
    {
        // From Ghidra: DAT_1460f3ce8 = Renderer singleton
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x60F3CE8);
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
        return s_base + 0x30bbdb8;  // VR vtable (from Ghidra constructor)
    }

    uintptr_t GetBSLightingShader()
    {
        // VR singleton at base+0x68794b0 (from global LoadShaders FUN_1427f4800)
        auto ptr = reinterpret_cast<uintptr_t*>(s_base + 0x68794b0);
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
