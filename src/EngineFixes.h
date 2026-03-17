#pragma once

namespace EngineFixes
{
    // Phase 0: Apply before game shader init (called from onModLoaded / early init)
    bool ApplyAll();

    // Individual patch groups
    bool EnableTiledDeferredLighting();    // 5 NOP patches from F4VR-Tiled-Lighting
    bool ExpandShadowCascades();           // ~10 patches from VR-Shadow-Boost preloader
    bool FixVRStereoShadowDispatch();      // JZ->JMP at 0x281be1c
    bool ApplyCrashPrevention();           // Null safety, zero-init, node clear, ptr validation

    // Phase 1: Apply after game load (INI overrides, runtime fixes)
    bool ApplyPostLoadFixes();
    bool ForceINISettings();               // bComputeShaderDeferredTiledLighting = true
}
