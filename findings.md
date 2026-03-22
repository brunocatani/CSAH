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

## Finding 13: FO4 Deferred Rendering — T2/T3 Control Lighting
**Date:** 2026-03-22

FO4 uses TILED DEFERRED RENDERING. BSLightingShader writes RAW material data.
Composite formula: `final = albedo * (ambient + diffuse_lights) + specular + emissive`

T2/T3 zeros -> composite produces near-zero lighting -> dark output.
Correct T2 defaults: (0, 0.118, 0.1, 1.0) — smoothness/roughness/saturate
Correct T3 defaults: (0.02, 0.02, 0.3, 1.0) — specular/glossiness/pow(alpha,0.1)
T2.w and T3.w MUST be non-zero for proper lighting.

## Finding 14: View-Space Normals + Y-Up
**Date:** 2026-03-22

Normals in TEXCOORD0-2 are VIEW SPACE (change with camera).
FO4 uses Y-up. min(Nz,0) ensures normals face camera.
Lambert azimuthal: `encoded.xy = N.xy / sqrt(8-8*Nz) + 0.5, encoded.z = -Nz`

## Finding 15: BSLightingShader Dual Render Paths (from Ghidra agents)
**Date:** 2026-03-22

BSLightingShader has TWO render paths to PSSetShader:

**Path A (Individual passes):** Goes through BeginTechnique (vtable+0x20)
→ BSShader::BeginTechnique → Renderer::SetShaders → PSSetShader

**Path B (Grouped batch):** BYPASSES BeginTechnique entirely
→ BSLightingShader::RenderBatch → batch dispatcher (base+0x28AA8D0)
→ copies pre-resolved PS from group+0x68 → Renderer::SetShaders → PSSetShader

Path B is why 120+ PS are "unknown" in differential tracking. BeginTechnique IS called
for some individual passes (Path A), but the MAJORITY go through batch mode (Path B).

**Renderer::SetShaders (base+0x1D92C00)** is the UNIVERSAL convergence point for ALL paths.
Could hook this for even more reliable interception than PSSetShader vtable.

The BSGraphics renderer at DAT_146235ab0 is a WRAPPER around the real D3D11 context,
with PSSetShader at wrapper vtable offset 0x1E0 (not standard D3D11 vtable[9]).
Our raw D3D11 PSSetShader hook still works because the wrapper delegates to real context.

## Finding 16: GFXBooster Validates Our Approach
**Date:** 2026-03-22

GFXBooster (github.com/disi/GFXBooster) is an F4SE mod that successfully hooks
PSSetShader/VSSetShader on FO4 using the same D3D11 context vtable approach.
It identifies shaders via DXBC hash fingerprinting. Proves our architecture is correct.

## Finding 17: DFTiledLighting Compute Shader Pipeline
**Date:** 2026-03-22

The deferred composite at FUN_142848e70 runs 3 compute dispatches:
1. Light culling — binds GBuffer as SRVs (slots 0-5), builds per-tile light lists
2. Light assignment — assigns lights to tiles
3. Lighting composite — final lighting with shadows (technique 1 or 2)

GBuffer RT-to-SRV transition via FUN_141db9dd0 (6 calls, slots 0-5).
Thread groups: (width+7)/8 × (height+7)/8 (8×8 tiles).

## Finding 18: TWO BSLightingShader Objects — ROOT CAUSE
**Date:** 2026-03-22

FO4VR has TWO BSLightingShader objects:
1. "Lighting" (DAT_14689b410, vtable 0x1430bbdb8, BeginTechnique 0x28B5C10) — accumulation only, NOT rendering
2. "VR Extended" (DAT_14689b8a0, vtable 0x1430bf3c8, BeginTechnique 0x291DA20) — THE REAL RENDERER

We Detoured the WRONG object. Hook 0x291DA20 for per-permutation support.
SetupGeometry: vtable[9] (+0x48), not vtable[7] (+0x38).

## Finding 20: GBuffer RT Formats — MRT1 is R16G16, MRT0 is SRGB
**Date:** 2026-03-22

GBuffer render target formats from Ghidra analysis:
- MRT0 (Albedo): R8G8B8A8_UNORM_SRGB — auto gamma encode on write!
- MRT1 (Normals): R16G16_UNORM — ONLY 2 channels! T1.z/.w DISCARDED
- MRT2 (Material): R8G8B8A8_UNORM
- MRT3 (Emissive): R8G8B8A8_UNORM
- MRT4 (Additional): R8G8B8A8_UNORM_SRGB
- MRT5 (Motion Vectors): R16G16_FLOAT

MRT0 SRGB means hardware gamma-encodes our output. If diffuse texture is
sampled as SRGB SRV (returns LINEAR), our multiply + SRGB RT write is correct.
If sampled as plain UNORM (returns gamma-encoded), double-encoding makes it dark.

MRT1 R16G16 means deferred composite reconstructs Nz from just Nx,Ny:
  Nz = 8 * dot(enc, enc) - 1   where enc = texValue.xy - 0.5

VR composite uses DIFFERENT RT indices: 99, 100, 102, 103 (vs flat 28, 29, 32, 33).
