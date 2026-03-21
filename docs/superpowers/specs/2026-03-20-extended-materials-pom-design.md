# Extended Materials: Parallax Occlusion Mapping for FO4VR Community Shaders

**Date:** 2026-03-20
**Status:** Draft (post-review v2)
**Sub-project:** 1 of 3 (Foundation + POM)

## Overview

Port Skyrim Community Shaders' Extended Materials system to FO4VR, starting with Parallax Occlusion Mapping (POM) on object meshes and landscapes. This replaces FO4's single-step parallax with full ray-marched POM using height data from the `_s.dds` specular texture alpha channel — compatible with existing ENB Complex Parallax texture packs.

## Goals

- POM ray-marching on parallax-flagged meshes, replacing vanilla single-step parallax
- Compatible with existing FO4 ENB Complex Parallax texture packs (Vivid Fallout, FO4 HD Overhaul Parallax, etc.)
- Height data from `_s.dds` alpha channel (already bound by vanilla BSLightingShader)
- Distance-based fade for performance
- Optional parallax self-shadows
- VR stereo support (extra mip bias, conservative step counts)
- Togglable via ImGui menu

## Non-Goals (deferred to sub-projects 2 & 3)

- Terrain height blending (6-tile system)
- Enhanced BRDF (GGX, Burley, etc.)
- PBR flag system (subsurface, coat, fuzz, glint)
- SetupMaterial hook / .bgsm parsing
- Custom texture loading

## Texture Format

Confirmed via inspection of target texture packs:

- **Vivid Fallout - Landscapes - Complex Parallax Occlusion**: 35 `_s.dds` files (landscape textures). Height in alpha channel.
- **FO4 HD Overhaul Parallax**: BA2 archive + ESP (object textures). Same `_s.dds` alpha convention.

No `_p.dds` (dedicated parallax) files. All height data is in the alpha channel of the specular/smoothness map, which is the standard ENB Complex Parallax convention for FO4.

**Alpha channel convention:** 0.0 = deepest (fully recessed), 1.0 = surface level (no displacement). This matches ENB and Skyrim CS convention. The displacement math subtracts 0.5 and scales, so mid-gray (0.5) = no displacement.

The `_s.dds` texture is already bound by vanilla `BSLightingShader` — no C++ texture loading needed.

## Implementation Phases

This spec is implemented in 3 sequential phases, each producing a testable deliverable:

### Phase A: FXP Extraction & Pipeline Mapping (Blocking Prerequisite)

**Deliverable:** A reference document with confirmed CB layouts, texture slot map, PS_INPUT signature, and vanilla parallax math for parallax permutations.

This phase must complete before any shader reconstruction begins. It produces the ground truth that Phases B and C depend on.

### Phase B: Vanilla-Matching PS Reconstruction

**Deliverable:** A `Community/Lighting.hlsl` that compiles for parallax permutations and produces output visually identical to vanilla FO4 (no POM yet).

Success criteria: In-game, parallax surfaces look identical whether using our reconstructed PS or vanilla. This validates the reconstruction before we modify the parallax math.

### Phase C: POM Integration

**Deliverable:** POM ray-marching replaces vanilla single-step parallax in the reconstructed shader. ExtendedMaterials Feature class with ImGui controls.

## Architecture

### Layer 1: FXP Disassembly & Texture Slot Mapping (Phase A)

**Input:** `Shaders011.fxp` (compiled shader archive in project root)

**Process:**
1. Write a Python script to parse the FXP container format:
   - Scan for `DXBC` magic bytes (observed at offsets 0x44 and 0x340 in hex dump)
   - Extract each DXBC blob with associated technique flags/metadata from the FXP header structure
   - Disassemble via `fxc /dumpbin` or Python DXBC parser
   - Alternative: If FXP parsing proves too complex, use RenderDoc to capture running shaders directly (bypasses FXP format entirely)
2. Identify parallax permutations by technique flag bits:
   - Filter: `(techniqueID & 0x0800) != 0` — the `PARALLAX_OCCLUSION_MAPPING` flag
   - Note: `MATERIAL_PARALLAX` (material type 0x04, bits 8-12) is the material type, while 0x0800 is the rendering flag. A permutation may have either or both. We filter on 0x0800 because that's the flag that activates parallax math in the shader. Permutations with `MATERIAL_PARALLAX` material type but without 0x0800 use simple bump mapping, not parallax — those stay vanilla.
   - Estimated count: 10-30 parallax permutations out of ~200+ total BSLightingShader permutations
3. From disassembly, extract and document:
   - **Texture slot assignments**: Which `t` register = diffuse, normal, specular, etc.
   - **PerMaterial CB (b1) field layout**: Confirm `ParallaxOccData` byte offset within the 8-float4 struct. Currently assumed to be the 4th float4 (byte offset 48) based on the declaration in the non-Community Lighting.hlsl, but this MUST be verified against DXBC register access patterns (e.g., `cb1[3]` = offset 48).
   - **All CB layouts**: Document PerGeometry (b0), PerMaterial (b1), PerFrame (b2) field layouts for parallax permutations
   - **PS_INPUT signature**: Full list of VS outputs consumed by the PS — specifically confirm that tangent and bitangent are present for parallax permutations (required for TBN matrix construction)
   - **The vanilla parallax math**: Identify the exact instructions performing single-step UV offset — this is what POM replaces
   - **CB register usage**: Document which `b` registers vanilla uses (b0-b?). This determines which register is safe for our ExtendedMaterials CB. Currently planned for b5 but must be verified as unused.

**Validation:** Cross-reference with RenderDoc frame captures on FO4 flat and/or Ghidra analysis of FO4 flat binary.

**Output document:** `docs/reference/fxp-parallax-analysis.md` with all confirmed layouts.

### Layer 2: Targeted PS Replacement (Phase B)

Only replace PS for technique IDs with parallax bits set. VS stays vanilla.

**ShaderReplacer changes:**
```cpp
// New method — supplements (does not replace) ReplaceAllPermutations
void ReplaceFilteredPermutations(void* bsShader, uint32_t shaderType,
    std::function<bool(uint32_t techniqueID)> filter);
```

Implementation: Identical to `ReplaceAllPermutations` but skips compilation for technique IDs where `filter()` returns false. The existing `ReplaceAllPermutations` remains available for future use (e.g., when all permutations are reconstructed).

Called from `Hook_LoadShaders` for BSLightingShader (type 8) — replaces the current `ReplaceAllPermutations` call:
```cpp
// Before: replacer.ReplaceAllPermutations(shader, 8);
// After:
replacer.ReplaceFilteredPermutations(shader, 8,
    [](uint32_t id) { return (id & 0x0800) != 0; });
```

Non-parallax permutations are never compiled, never touched — vanilla shaders remain.

**Reconstructed HLSL (`Community/Lighting.hlsl`):**

This is the largest engineering task in the spec. The reconstruction must include:
- **Full CB declarations**: PerGeometry (b0), PerMaterial (b1), PerFrame (b2), SharedData (b3) — all fields with correct byte offsets from Phase A
- **Texture slot declarations**: All `Texture2D` + `SamplerState` at correct `t` and `s` registers
- **PS_INPUT struct**: Matching vanilla VS output signature exactly (positions, texcoords, normals, tangents, colors, etc.)
- **Complete lighting model**: Ambient (directional SH from SharedData), directional light, point lights, fog, emissive — all reconstructed from DXBC disassembly
- **The parallax UV offset**: Initially reconstructed as vanilla single-step (Phase B), then replaced with POM (Phase C)

Technique IDs without parallax bits will fail to compile (intentional) — ShaderReplacer keeps vanilla.

**Note on compilation defines:** The `PARALLAX_OCCLUSION_MAPPING` define is already generated by `ShaderCache::BuildDefines()` for technique IDs with bit 0x0800 set (see `ShaderCache.cpp:162`). No changes to the define-generation logic are needed — the HLSL `#ifdef PARALLAX_OCCLUSION_MAPPING` will work automatically.

**Rollback mechanism:** If reconstruction causes visual artifacts, the ImGui menu includes a "Restore Vanilla Shaders" button that writes back all `vanillaPS` pointers from `ShaderReplacer`'s saved map. This provides in-game recovery without restart.

### Layer 3: POM Feature (Phase C)

**`ExtendedMaterials` Feature class:**

```
src/Features/ExtendedMaterials.h
src/Features/ExtendedMaterials.cpp
```

Follows existing `LinearLighting` pattern:

```cpp
struct ExtendedMaterials : Feature {
    std::string GetName() override { return "Extended Materials"; }
    std::string GetShortName() override { return "ExtendedMaterials"; }
    std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
    bool HasShaderDefine(uint32_t shaderType) override { return shaderType == 8; }
    bool IsCore() override { return true; }

    struct alignas(16) Settings {
        uint32_t EnablePOM = 1;
        uint32_t EnableShadows = 1;
        uint32_t MaxSteps = 0;        // 0 = distance-based automatic
        float HeightScaleMult = 1.0f;  // Multiplier on vanilla ParallaxOccData.x
    };

    std::unique_ptr<ConstantBuffer> settingsCB;  // bound to b5 (or verified-safe register)

    void SetupResources() override;          // creates settingsCB
    void OnSetupGeometry(void* renderPass) override;  // updates + binds settingsCB
    void DrawSettings() override;            // ImGui checkboxes + sliders
    void SaveSettings(nlohmann::json& j) override;
    void LoadSettings(const nlohmann::json& j) override;
};
```

**Feature registration:** In `XSEPlugin.cpp`, alongside existing feature globals:
```cpp
// At file scope (next to g_linearLighting):
static ExtendedMaterials g_extendedMaterials;

// In kPostPostLoad handler (next to LinearLighting registration):
Feature::RegisterFeature(&g_extendedMaterials);
```
This matches the pattern used by `LinearLighting` (see `XSEPlugin.cpp:43`).

**CB register allocation table** (to be included in Phase A output document):
| Register | Owner | Notes |
|----------|-------|-------|
| b0 | Vanilla PerGeometry | Engine-managed |
| b1 | Vanilla PerMaterial | Engine-managed, contains ParallaxOccData |
| b2 | Vanilla PerFrame | Engine-managed |
| b3 | SharedData (ours) | Community Shaders shared CB |
| b4 | LinearLighting (ours) | Feature CB |
| b5 | ExtendedMaterials (ours) | Feature CB (pending Phase A verification) |

## POM Algorithm

Adapted from Skyrim CS `ExtendedMaterials.hlsli` (4-step batched contact refinement):

### HLSL Include Structure

```
package/Shaders/Community/
    Lighting.hlsl                              — main PS (reconstructed parallax permutations)
    ExtendedMaterials/ExtendedMaterials.hlsli   — POM implementation
package/Shaders/Common/
    Color.hlsli        — existing (sRGB conversion)
    SharedData.hlsli   — existing (shared CB on b3)
    Math.hlsli         — new (PI, EPSILON, saturate helpers — ported from Skyrim CS)
```

### Algorithm Steps

1. **TBN matrix construction** from VS-output normal, tangent, bitangent. Transform view direction from world space to tangent space. (Tangent/bitangent presence in PS_INPUT confirmed during Phase A.)

2. **Flatten correction**: `viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + flattenAmount` — prevents extreme warping at grazing angles.

3. **Step count calculation**:
   ```hlsl
   float maxStepsBase = SharedData.IsInterior ? 8.0 : 16.0;
   #if defined(VR)
       maxStepsBase *= 0.5;  // Halved for VR performance
   #endif
   if (extMatSettings.MaxSteps > 0)
       maxStepsBase = (float)extMatSettings.MaxSteps;  // User override
   float nearBlendToFar = saturate(distance / 2048.0);
   uint numSteps = clamp(uint(maxStepsBase * (1.0 - nearBlendToFar) + 0.5), 1, (uint)maxStepsBase);
   ```

4. **Distance calculation**: `distance = length(input.WorldPos - PerFrame.CameraPosition.xyz)` where `CameraPosition` comes from the PerFrame CB (b2, exact field offset from Phase A). For VR, this uses the per-eye camera position.

5. **Ray-march loop** (batched 4 samples per iteration):
   - Calculate 4 UV offsets along view ray
   - Sample height from specular texture alpha: `specTex.SampleLevel(sampler, uv, mipLevel).a`
   - Adjust height: `(height - 0.5) * displacementScale + 0.5 + displacementOffset`
   - Compare against ray height at each step
   - On first intersection: enter contact refinement pass (subdivide and re-march at finer step size)

6. **Linear interpolation** between bounding samples for sub-step precision.

7. **Distance fade**: Beyond 2048 units, blend displaced UVs back to original. `nearBlendToFar = saturate(distance / 2048.0)`, squared for smooth transition.

8. **Output**: displaced UV coordinates used for all subsequent texture reads (diffuse, normal, specular RGB).

### Screen Noise for Stochastic Mip Selection

The Skyrim CS POM uses a noise value for stochastic mip level selection to reduce banding artifacts:
```hlsl
float screenNoise = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));
```
This is a cheap screen-space hash (Interleaved Gradient Noise) computed from `SV_POSITION.xy`. No texture lookup required.

### Mip Level Calculation

Computed once before the ray-march loop (not inside, which causes artifacts):
```hlsl
float2 textureDims;
specTex.GetDimensions(textureDims.x, textureDims.y);
float2 texCoordsPerSize = coords * textureDims;
float2 dxSize = ddx(texCoordsPerSize);
float2 dySize = ddy(texCoordsPerSize);
float mipLevel = max(0.5 * log2(min(dot(dxSize, dxSize), dot(dySize, dySize))), 0);
```
VR adjustment: `mipLevel += 1.0` to reduce stereo shimmer.

Stochastic selection: `mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);`

### Displacement Parameters Construction

```hlsl
DisplacementParams BuildDispParams() {
    DisplacementParams p;
    float scale = ParallaxOccData.x * extMatSettings.HeightScaleMult;
    p.DisplacementScale = scale;   // Controls normalized height adjustment amplitude
    p.DisplacementOffset = 0.0;    // No offset (future: per-material via .bgsm)
    p.HeightScale = scale;         // Controls parallax ray-march extent (view ray length)
    p.FlattenAmount = 0.0;         // No flatten (future: per-material)
    return p;
}
```
Note: `DisplacementScale` and `HeightScale` are currently identical. They diverge in Sub-project 2 (terrain) where per-tile scale differs from the overall parallax extent. Keeping them separate now avoids a refactor later.

### Optional Parallax Self-Shadows

Ported from Skyrim CS `GetParallaxSoftShadowMultiplier`:
- 4 samples along light direction through height field
- Produces shadow factor (0.0 = fully shadowed, 1.0 = fully lit)
- Applied to directional light contribution only (point light shadows deferred to future)
- Togglable via `EnableShadows` setting

### Integration Point in Lighting.hlsl

```hlsl
#ifdef EXTENDED_MATERIALS
#include "ExtendedMaterials/ExtendedMaterials.hlsli"
#endif

// In PSMain, before any texture sampling:
float2 uv = texCoord0;

#ifdef EXTENDED_MATERIALS
#ifdef PARALLAX_OCCLUSION_MAPPING
    float screenNoise = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));
    float distance = length(input.WorldPos - PerFrame.CameraPosition.xyz);
    DisplacementParams dispParams = BuildDispParams();
    float pixelOffset = 0;
    if (extMatSettings.EnablePOM) {
        uv = ExtendedMaterials::GetParallaxCoords(
            distance, texCoord0, mipLevel, viewDir, tbn,
            screenNoise, specTex, specSampler, 3,  // channel 3 = alpha
            dispParams, pixelOffset);
    }
#endif
#endif

// All subsequent texture reads use 'uv':
float4 diffuse = diffuseTex.Sample(diffuseSampler, uv);
float4 normal = normalTex.Sample(normalSampler, uv);
// etc.
```

### Settings Constant Buffer

```hlsl
cbuffer ExtendedMaterialsCB : register(b5) {  // Register verified as unused during Phase A
    uint EnablePOM;
    uint EnableShadows;
    uint MaxSteps;          // 0 = automatic distance-based
    float HeightScaleMult;  // Multiplier on vanilla ParallaxOccData.x
};
```

## Displacement Parameters

Read from vanilla `PerMaterial` CB (b1), already populated by the engine:

```hlsl
cbuffer PerMaterial : register(b1) {
    float4 LODTexParams;           // cb1[0]
    float4 TintColor;              // cb1[1]
    float4 EnvmapData;             // cb1[2]
    float4 ParallaxOccData;        // cb1[3] — x=heightScale, y=maxSteps
    float4 SpecularColor;          // cb1[4]
    float4 SparkleParams;          // cb1[5]
    float4 MultiLayerParallaxData; // cb1[6]
    float4 LightingEffectParams;   // cb1[7] — x=subSurfaceLightRolloff
};
```

**BLOCKING NOTE:** The field order above is based on the declaration in the non-Community `Lighting.hlsl` stub. The exact byte offsets MUST be confirmed against DXBC register access patterns during Phase A (e.g., if disassembly shows parallax height read from `cb1[3].x`, the layout is confirmed).

## File Changes

### New Files
| File | Purpose |
|------|---------|
| `src/Features/ExtendedMaterials.h` | Feature class declaration |
| `src/Features/ExtendedMaterials.cpp` | Feature implementation (settings, CB, ImGui) |
| `package/Shaders/Community/ExtendedMaterials/ExtendedMaterials.hlsli` | POM ray-march algorithm |
| `package/Shaders/Common/Math.hlsli` | Math constants (PI, EPSILON) |
| `tools/fxp_extract.py` | FXP parser/DXBC extractor script |
| `docs/reference/fxp-parallax-analysis.md` | Phase A output: confirmed layouts and slot maps |

### Modified Files
| File | Change |
|------|--------|
| `src/ShaderReplacer.h` | Add `ReplaceFilteredPermutations` method |
| `src/ShaderReplacer.cpp` | Implement filtered replacement |
| `src/Hooks.cpp` | Call filtered replacement for parallax permutations |
| `package/Shaders/Community/Lighting.hlsl` | Reconstruct parallax permutation PS, integrate POM |
| `src/XSEPlugin.cpp` | Register ExtendedMaterials feature (global + kPostPostLoad) |
| `src/Menu.cpp` | ExtendedMaterials settings rendered via existing Feature iteration |

## Testing Strategy

### Phase A Validation
- Compare disassembled DXBC output against known shader patterns
- Verify texture slot assignments with RenderDoc captures on FO4 flat
- Cross-reference CB layouts with Ghidra analysis of FO4 flat binary

### Phase B Validation (Vanilla-Match)
- Screenshot comparison: vanilla PS vs reconstructed PS for same scene
- A/B toggle via ImGui "Restore Vanilla Shaders" button
- Verify no visible difference in lighting, fog, specular on parallax-flagged objects
- If differences exist, iterate on reconstruction before proceeding to Phase C

### Phase C Validation (POM)
- Load with Vivid Fallout landscape parallax pack -> landscape surfaces should show depth
- Load with FO4 HD Overhaul Parallax -> object surfaces (walls, floors) should show depth
- Toggle POM off in ImGui -> should revert to flat appearance (reconstructed vanilla parallax)
- Walk toward/away from parallax surfaces -> distance fade should be smooth
- Look at parallax surfaces at grazing angles -> flatten correction should prevent warping

### VR-Specific
- Verify no stereo mismatch: Both eyes must see consistent displacement. Test by capturing both eye renders and overlaying (RenderDoc stereo capture or SteamVR frame timing overlay)
- Verify no shimmer at distance (mip bias adjustment)
- Confirm per-eye camera position is used for distance calculation

### Performance
- Frame time comparison with POM enabled vs disabled
- Target: <1ms overhead in typical outdoor scenes
- Profile with RenderDoc or GPU profiler to identify bottleneck (ALU vs texture fetch)

### Rollback
- ImGui "Restore Vanilla Shaders" button writes back all `vanillaPS` pointers from ShaderReplacer's saved map
- Provides in-game recovery without restart
- Also serves as the A/B comparison mechanism for Phase B validation

## Dependencies

- `Shaders011.fxp` — in project root (confirmed available)
- Skyrim CS source — at `E:\fo4dev\skirymvr_mods\source_codes\skyrim-community-shaders` (confirmed available)
- RenderDoc — user to install for frame capture validation
- FO4 flat binary — available in Ghidra for cross-reference
- Existing infrastructure: ShaderReplacer, ShaderCache, Feature system, Hooks (all working)

## Risk Mitigation

### PS Reconstruction Divergence
**Risk:** Reconstructed PS produces subtly different lighting than vanilla, creating visible seams between POM and non-POM surfaces.
**Mitigation:** Phase B exists specifically to catch this. A/B comparison must show no visible difference before POM is added. If perfect reconstruction proves impossible, we can still ship POM — small lighting differences are less noticeable than the parallax effect itself.

### FXP Parser Complexity
**Risk:** Bethesda's FXP format is poorly documented; parsing could become a rabbit hole.
**Mitigation:** Fallback path: use RenderDoc to capture running shaders directly (bypasses FXP format entirely). The FXP parser is preferred because it gives us ALL permutations at once, but RenderDoc captures of specific parallax draw calls provide the same information for those permutations.

### VR Stereo Mismatch
**Risk:** POM displacement inconsistent between eyes due to bugs in view direction or TBN calculation.
**Mitigation:** Test early with a high heightScale to make any mismatch obvious. The POM math is per-eye by design (uses per-eye view direction), so correct implementation should produce natural stereo depth.

### Disk Cache Coherence
**Note:** The existing `MakeCacheKey` hashes feature names of enabled features into the cache key. Toggling `ExtendedMaterials` on/off invalidates affected cache entries automatically. Per-feature settings (EnablePOM, MaxSteps, etc.) are runtime CB values, not compile-time defines, so they do not affect the cache key — this is correct behavior.

## Future Sub-Projects

**Sub-project 2: Terrain Height Blending**
- 6-tile landscape height blending
- `MATERIAL_MULTITEX_LANDSCAPE` permutation reconstruction
- Terrain-specific POM with per-tile displacement params
- `DisplacementScale` vs `HeightScale` distinction becomes relevant here

**Sub-project 3: Enhanced PBR & Materials**
- BRDF library port (GGX, Burley, Oren-Nayar, etc.)
- PBR flag system (subsurface, coat, fuzz, glint)
- SetupMaterial hook + .bgsm struct mapping
- Full material type reconstruction (Envmap, Hair, Eye, etc.)
