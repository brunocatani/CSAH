# Findings — FO4VR Shader Hooking Research

## Finding 1: FO4VR rendering pipeline is fundamentally different from Skyrim
**Date:** 2026-03-21
**Impact:** Critical — entire hooking approach must change

Skyrim CS hooks BSShader::BeginTechnique (called via virtual dispatch) and patches CALL sites inside it. FO4VR never calls BSLightingShader::BeginTechnique at runtime — confirmed with:
- Detour patch verified (E9 JMP at prologue)
- Zero calls across full game session
- All xrefs are .pdata exception entries, not code calls

FO4 uses function pointer dispatch tables and possibly pre-resolved shader states for the deferred rendering pipeline.

## Finding 2: Skyrim CS approach (from source analysis)
**Date:** 2026-03-21

Skyrim CS uses 3 hook types:
1. `stl::detour_thunk` — full function replacement
2. `stl::write_thunk_call` — patches specific CALL instruction at offset inside parent function
3. `stl::write_vfunc` — vtable slot replacement

For BeginTechnique: patches CALL sites at +0xC3 and +0xD7 INSIDE BeginTechnique to intercept SetVertexShader/SetPixelShader. At draw time, swaps shader via context->PSSetShader().

Key: they also call context->PSSetShader() directly for CS-only permutations.

## Finding 3: BSLightingShader class layout (VR)
**Date:** 2026-03-21

- vtable[0]: FUN_1428bacd0 (destructor)
- vtable[1]: FUN_141c13d80
- vtable[2]: 0x1427d81e0
- vtable[3]: FUN_142813560
- vtable[4]: FUN_1428b5c10 (BeginTechnique, 3-param) — NEVER CALLED
- vtable[5]: FUN_1428b5bb0 (simple setup)
- vtable[6]: FUN_1428b6240 (SetupMaterial — switch on material type)
- vtable[7]: 0x1428135d0 (SetupGeometry)
- vtable[8]: 0x1428135e0

## Finding 4: Render pass creation
**Date:** 2026-03-21

FUN_1427a3f90 (geometry submission) creates BSRenderPass objects:
- Passes BSLightingShader singleton (DAT_14689b410) as shader
- Builds technique ID from material flags (bits 8-13 = material type)
- Stores at pass+0x08 (shader), pass+0x48 (technique), pass+0x4c (pass type)
- Uses FUN_14278e610 to create pass objects

## Finding 5: ID3D11DeviceContext::PSSetShader vtable hook doesn't fire
**Date:** 2026-03-21
**Impact:** Critical

Hooked vtable[9] on context from Globals::GetContext() (base+0x60F3D08). Hook installed successfully but never fires. Possible causes:
- Game uses a DIFFERENT D3D context for rendering (deferred context, VR wrapper)
- SteamVR interposes its own context
- The cached context pointer is a BSGraphics wrapper, not the raw ID3D11DeviceContext

**Latest build (untested):** Compares cached context vs device->GetImmediateContext() to identify mismatch.

## Finding 6: PS technique ID extraction (from Ghidra)
**Date:** 2026-03-21

```cpp
// FUN_14293a520 — extract PS tech ID from combined
uint32_t ExtractPSTechID(uint32_t combined) {
    uint32_t id = combined;
    if ((id & 4) == 0) id &= ~2u;
    return id | 1;
}

// FUN_14293a4f0 — extract VS tech ID
uint32_t ExtractVSTechID(uint32_t combined) {
    return combined & 0x3F0F;
}
```

## Finding 8: PS input signatures vary wildly across permutations (2026-03-21 late)
VR FXP PS permutations have DIFFERENT ISGN layouts:
- Simple: `SV_POSITION, COLOR, TEXCOORD`
- Medium: `SV_POSITION, TEXCOORD, TEXCOORD`
- Complex: `SV_POSITION, TEXCOORD*2, COLOR, POSITION*3, SV_CullDistance, SV_ClipDistance`
- VR: adds `EYEINDEX`

Cannot use ONE PS for ALL permutations. Our diagnostic confirmed:
- TEXCOORD0 has tangent data for some surfaces (distant) but zero for nearby ground
- Diffuse texture (t0) NOT bound for most surfaces
- ALL texture slots (t0-t7) and vertex color return black for ground geometry

This means replacing ALL BSLightingShader PS with one generic PS is NOT viable.
Need per-permutation or per-layout-group replacement.

## Finding 9: CreatePixelShader hook timing remains unsolved
- Game device NOT available at PostPostLoad (base+0x60F3CF0 = null)
- D3D11CreateDevice fails at PostPostLoad (HARDWARE, WARP, REFERENCE, NULL all fail)
- d3d11.dll might not be loaded yet at PostPostLoad time
- All PS created before GameDataReady when we CAN hook

Next approach: Hook in DllMain(DLL_PROCESS_ATTACH) or use IAT patching on game exe.

## Finding 7: Scatter tables empty because FXP never loaded into them
**Date:** 2026-03-21

Both pointer sources (raw singleton 0x689b410, refcounted 0x68794b0) point to SAME object. Type=8 confirmed. Sentinel at +0xC8 is set (constructor ran) but count=0 and buckets=null. The FXP loader (FUN_142814260) writes to these offsets but data never appears — possibly because our LoadShaders Detour broke it, or VR uses different offsets.

After removing LoadShaders Detour: STILL empty. FXP data may go elsewhere in VR.

## Finding 10: FO4VR PS Input Signature Layout (ISGN) — CRITICAL
**Date:** 2026-03-21
**Impact:** Resolves the black output blocker

FO4VR BSLightingShader PS have 105 unique ISGN layouts (from 2283 PS in Shaders012_VR.fxp).
Key layouts for POM:

**Layout #1 (420 PS, 238 POM) — DEPTH PRE-PASS:**
`SV_POSITION, EYEINDEX, SV_CullDistance, SV_ClipDistance`
No texture coordinates, no color — DO NOT replace with GBuffer PS.

**Layout #3 (144 PS, 134 POM) — FULL GBUFFER:**
```
v0 SV_POSITION   (xyzw)  — screen position
v1 TEXCOORD0     (xyz)   — tangent vector
v2 TEXCOORD1     (xyz)   — bitangent vector
v3 TEXCOORD2     (xyz)   — normal vector
v4 TEXCOORD3     (xyzw)  — .w = UV.u (!!)
v5 TEXCOORD4     (xyzw)  — .w = UV.v (!!)
v6 COLOR0        (xyzw)  — vertex color + alpha
v7 EYEINDEX      (x)     — VR eye (0=left, 1=right)
v8 SV_IsFrontFace(x)     — face culling
```

**Layout #4 (123 PS, 117 POM) — GBUFFER NO VERTEX COLOR:**
Same as #3 but without COLOR0. Handle by falling back to white (1,1,1,1).

**KEY DIFFERENCE FROM SKYRIM:**
- FO4VR: UVs packed in TEXCOORD3.w / TEXCOORD4.w
- Skyrim: UVs in TEXCOORD0.xy
- FO4VR: TBN at TEXCOORD0-2
- Skyrim: TBN at TEXCOORD1-3
- FO4VR: Always has EYEINDEX (VR-only)
- Skyrim: Conditionally adds VR semantics

**FILTER STRATEGY:** Check render target count before replacing. GBuffer pass binds 5 RTs; depth pre-pass binds 0-1.

## Finding 11: Forward vs Deferred Pass Separation (from DXBC agent analysis)
**Date:** 2026-03-22

Layout #2 (144 POM) and Layout #5 (122 POM) are **FORWARD PASS** shaders:
- UVs in TEXCOORD0.xy (straightforward)
- Single SV_Target0 output (NOT 5 GBuffer targets)
- Include fog blending, depth fade, sRGB vertex color conversion
- Layout #5 also has 4-point-light forward computation using TEXCOORD6 as world position

Layout #3 (134 POM) and Layout #4 (117 POM) are **GBUFFER DEFERRED** shaders:
- UVs packed in TEXCOORD3.w / TEXCOORD4.w
- 5 SV_Target outputs (albedo, normals, material, specular, emissive)
- Full TBN normal mapping, alpha test, specular encoding

RT count filter (>=4 RTs) correctly separates these:
- Forward: 1 RT → skipped
- GBuffer: 5 RTs → replaced

## Finding 12: Skyrim CS Architecture — Per-Permutation PS Compilation
**Date:** 2026-03-22

Skyrim CS compiles ONE PS PER PERMUTATION DESCRIPTOR, not one PS for all.
- `ShaderCache::GetPixelShader(shader, descriptor)` keyed by descriptor
- Each descriptor gets unique #define set (PARALLAX, LANDSCAPE, EYE, etc.)
- VS_OUTPUT struct changes per-permutation via #ifdef blocks
- `BSShader::BeginTechnique` hook provides the descriptor at draw time

FO4VR can't use this approach because BeginTechnique is never called.
Alternative: identify Layout #3/#4 surfaces via RT count + SRV binding checks.
