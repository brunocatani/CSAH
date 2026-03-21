# Extended Materials POM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Parallax Occlusion Mapping to FO4VR Community Shaders for parallax-flagged BSLightingShader permutations, compatible with ENB Complex Parallax texture packs.

**Architecture:** Three-phase approach: (A) extract and document vanilla shader pipeline from FXP bytecode, (B) reconstruct vanilla-matching PS for parallax permutations only, (C) integrate POM ray-marching from Skyrim CS. Only parallax permutations are replaced; everything else stays vanilla.

**Tech Stack:** C++20, HLSL SM5.0, D3D11, Python (FXP extraction tool), F4SE, Detours, ImGui

**Spec:** `docs/superpowers/specs/2026-03-20-extended-materials-pom-design.md`

**Skyrim CS reference:** `E:\fo4dev\skirymvr_mods\source_codes\skyrim-community-shaders`

---

## Dependency Notes

- **Tasks 3, 5, 7, 8 can run in parallel** — they have no cross-dependencies
- **GATE: Task 2 must complete before Task 6 begins** — Task 6 fills in placeholders from Phase A output
- **GATE: Task 6 must achieve visual parity before Task 4 is wired** — otherwise parallax surfaces regress in-game
- **Tasks 7+8 can start alongside Phase A** — they don't depend on FXP analysis
- **Fallback:** If FXP parsing (Task 1) proves too complex, use RenderDoc frame captures on FO4 flat to extract the same information (texture bindings, CB values, shader bytecode per draw call). RenderDoc gives per-draw-call data; FXP gives all permutations at once.
- **CMakeLists.txt:** New source files (`src/Features/ExtendedMaterials.h/cpp`) must be added to the CMake build configuration. Check existing patterns for how `LinearLighting` files are included.

## Phase A: FXP Extraction & Pipeline Mapping

### Task 1: FXP DXBC Extractor Script

**Files:**
- Create: `tools/fxp_extract.py`

This script parses `Shaders011.fxp`, extracts DXBC shader blobs, and dumps them as individual `.dxbc` files with technique ID metadata. The FXP format contains DXBC blobs preceded by technique flag metadata.

- [ ] **Step 1: Write the FXP scanner**

```python
#!/usr/bin/env python3
"""Extract DXBC shader blobs from Bethesda FXP shader archives."""

import struct
import sys
from pathlib import Path


def find_dxbc_blobs(data: bytes) -> list[tuple[int, int]]:
    """Scan for DXBC magic bytes and extract blob offsets + sizes."""
    blobs = []
    magic = b'DXBC'
    offset = 0
    while True:
        pos = data.find(magic, offset)
        if pos == -1:
            break
        # DXBC header: magic(4) + checksum(16) + one(4) + totalSize(4)
        if pos + 28 <= len(data):
            total_size = struct.unpack_from('<I', data, pos + 24)[0]
            if total_size > 0 and pos + total_size <= len(data):
                blobs.append((pos, total_size))
        offset = pos + 4
    return blobs


def extract_technique_flags(data: bytes, dxbc_offset: int) -> int | None:
    """Try to extract technique flags from the FXP metadata preceding a DXBC blob.

    The FXP header structure precedes each DXBC blob. The exact layout needs
    investigation, but technique flags are typically stored as a uint32 in the
    entry header before the DXBC data.

    Returns None if we can't confidently extract the flags.
    """
    # Look backwards from the DXBC blob for candidate technique flag patterns
    # This is a heuristic - will need refinement based on actual FXP structure
    if dxbc_offset >= 8:
        # Check the 4 bytes immediately before the DXBC blob and further back
        candidates = []
        for lookback in [4, 8, 12, 16, 20, 24, 28, 32]:
            if dxbc_offset >= lookback:
                val = struct.unpack_from('<I', data, dxbc_offset - lookback)[0]
                candidates.append((lookback, val))
        return candidates  # Return all candidates for manual inspection
    return None


def get_shader_type_from_isgn(dxbc_data: bytes) -> str:
    """Determine if this is a VS or PS from the ISGN/OSGN chunks."""
    if b'SV_POSITION' in dxbc_data:
        # Check OSGN (output signature) - if SV_POSITION is in output, it's a VS
        osgn_pos = dxbc_data.find(b'OSGN')
        if osgn_pos != -1:
            # Read OSGN chunk size
            chunk_size = struct.unpack_from('<I', dxbc_data, osgn_pos + 4)[0]
            osgn_chunk = dxbc_data[osgn_pos:osgn_pos + 8 + chunk_size]
            if b'SV_POSITION' in osgn_chunk:
                return 'VS'

    # Check for SV_Target in output signature - indicates PS
    if b'SV_Target' in dxbc_data:
        return 'PS'

    return 'UNKNOWN'


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path/to/Shaders011.fxp> [output_dir]")
        sys.exit(1)

    fxp_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('tools/extracted_shaders')
    output_dir.mkdir(parents=True, exist_ok=True)

    data = fxp_path.read_bytes()
    print(f"Read {len(data)} bytes from {fxp_path}")

    blobs = find_dxbc_blobs(data)
    print(f"Found {len(blobs)} DXBC blobs")

    for i, (offset, size) in enumerate(blobs):
        dxbc_data = data[offset:offset + size]
        shader_type = get_shader_type_from_isgn(dxbc_data)

        # Extract technique flag candidates
        flags_info = extract_technique_flags(data, offset)

        out_file = output_dir / f"shader_{i:04d}_{shader_type}_off{offset:#x}.dxbc"
        out_file.write_bytes(dxbc_data)

        # Write metadata sidecar
        meta_file = output_dir / f"shader_{i:04d}_{shader_type}_off{offset:#x}.meta.txt"
        with open(meta_file, 'w') as f:
            f.write(f"Index: {i}\n")
            f.write(f"FXP Offset: {offset:#x}\n")
            f.write(f"DXBC Size: {size} bytes\n")
            f.write(f"Shader Type: {shader_type}\n")
            if flags_info:
                f.write(f"\nTechnique flag candidates (lookback, value):\n")
                for lookback, val in flags_info:
                    # Decode technique bits for parallax identification
                    has_pom = (val & 0x0800) != 0
                    mat_type = (val >> 8) & 0x1F
                    f.write(f"  -{lookback} bytes: {val:#010x}"
                            f"  POM={has_pom} MatType={mat_type:#04x}\n")

        print(f"  [{i:4d}] {shader_type} at {offset:#08x} ({size:6d} bytes) -> {out_file.name}")

    print(f"\nExtracted {len(blobs)} shaders to {output_dir}")
    print(f"\nNext steps:")
    print(f"  1. Look for PS blobs where technique flag candidates have POM bit (0x0800) set")
    print(f"  2. Disassemble those with: fxc /dumpbin <file>.dxbc")
    print(f"  3. Or use: python -c \"import subprocess; ...\" to batch-disassemble")


if __name__ == '__main__':
    main()
```

- [ ] **Step 2: Run the extractor on Shaders011.fxp**

Run: `cd E:/fo4dev/fallout4vr_mods/special_projects/fo4vr-community-shaders && python tools/fxp_extract.py Shaders011.fxp tools/extracted_shaders`

Expected: Extracted `.dxbc` files in `tools/extracted_shaders/` with `.meta.txt` sidecars showing technique flag candidates.

- [ ] **Step 3: Identify parallax PS blobs**

Scan the meta files for blobs where a technique flag candidate has the POM bit set:

Run: `grep -l "POM=True" tools/extracted_shaders/*.meta.txt`

If the heuristic technique flag extraction is unreliable, fall back to checking DXBC disassembly for `cb1[3]` access (parallax height scale from PerMaterial CB).

- [ ] **Step 4: Disassemble parallax PS blobs**

For each identified parallax PS blob, disassemble with `fxc`:

Run: `fxc /dumpbin tools/extracted_shaders/shader_XXXX_PS_offYYYY.dxbc > tools/extracted_shaders/shader_XXXX_PS.asm`

If `fxc` is not on PATH, use the Windows SDK path (typically `C:\Program Files (x86)\Windows Kits\10\bin\<version>\x64\fxc.exe`).

Alternative: Use `D3DDisassemble` via a small C++ helper or Python ctypes call.

- [ ] **Step 5: Commit extraction tooling**

```bash
git add tools/fxp_extract.py
git commit -m "feat: add FXP DXBC extraction script for shader analysis"
```

---

### Task 2: Document Shader Pipeline from Disassembly

**Files:**
- Create: `docs/reference/fxp-parallax-analysis.md`

Analyze the disassembled parallax PS to extract all pipeline information needed for reconstruction.

- [ ] **Step 1: Map texture slot assignments**

From the DXBC disassembly, find all `dcl_resource` instructions to identify which `t` registers are used:

```
dcl_resource_texture2d (float,float,float,float) t0  // likely diffuse
dcl_resource_texture2d (float,float,float,float) t1  // likely normal
dcl_resource_texture2d (float,float,float,float) t2  // likely specular (_s.dds)
...
```

Also find `dcl_sampler` instructions to map sampler states.

Document in `fxp-parallax-analysis.md`:
```markdown
## Texture Slot Map
| Register | Texture | Notes |
|----------|---------|-------|
| t0 | Diffuse (_d.dds) | ... |
| t1 | Normal (_n.dds) | ... |
| t2 | Specular (_s.dds) | Height in .a channel |
```

- [ ] **Step 2: Map constant buffer layouts**

From `dcl_constantbuffer` and register access patterns (`cb0[N]`, `cb1[N]`, `cb2[N]`):

- **b0 (PerGeometry)**: Find all `cb0[N]` accesses, map to field names
- **b1 (PerMaterial)**: Specifically confirm `cb1[3].x` = ParallaxOccData.x (heightScale)
- **b2 (PerFrame)**: Find camera position field (needed for distance calculation)

Document each CB with byte offsets and field names.

- [ ] **Step 3: Map PS_INPUT signature**

From the ISGN (Input Signature) chunk of the parallax PS DXBC, extract:
- All semantics (TEXCOORD0, TEXCOORD1, COLOR0, SV_POSITION, etc.)
- Confirm presence of tangent and bitangent (required for TBN matrix)
- Note which semantics are present only for specific permutation flags

Document the full `PS_INPUT` struct with semantic mappings.

- [ ] **Step 4: Identify vanilla parallax math**

In the DXBC assembly, locate the parallax UV offset instructions. These will be a sequence that:
1. Reads height from the specular texture alpha (`t2.w` or similar)
2. Reads `ParallaxOccData.x` from `cb1[3].x`
3. Computes UV offset using view direction in tangent space
4. Applies offset to texture coordinates before subsequent reads

Mark these instructions — this is exactly what POM replaces.

- [ ] **Step 5: Verify CB register b5 is unused**

Confirm no `dcl_constantbuffer cb5` exists in any parallax permutation. If b5 is used by vanilla, pick the next available register.

- [ ] **Step 6: Cross-reference with Ghidra (optional)**

If any DXBC findings are ambiguous, ask the user to cross-reference using Ghidra MCP on the FO4 flat binary. Key functions to check:
- `BSLightingShader::SetupMaterial` — confirms which CB fields are written
- `BSLightingShader::SetupGeometry` — confirms which textures are bound

- [ ] **Step 7: Write the analysis document**

Create `docs/reference/fxp-parallax-analysis.md` with all findings:
- Texture slot map
- CB layouts (b0, b1, b2) with byte offsets
- PS_INPUT struct
- Vanilla parallax math annotated
- CB register allocation table (including b3=SharedData, b4=LinearLighting, b5=ExtendedMaterials)
- Any discrepancies or open questions

- [ ] **Step 8: Commit analysis document**

```bash
git add docs/reference/fxp-parallax-analysis.md
git commit -m "docs: add FXP parallax shader analysis with CB/texture/signature mappings"
```

---

## Phase B: Vanilla-Matching PS Reconstruction

### Task 3: Add ReplaceFilteredPermutations to ShaderReplacer

**Files:**
- Modify: `src/ShaderReplacer.h`
- Modify: `src/ShaderReplacer.cpp`

- [ ] **Step 1: Add method declaration to ShaderReplacer.h**

In `src/ShaderReplacer.h`, add after `ReplaceAllPermutations`:

```cpp
// Walk scatter table, compile and replace only permutations where filter returns true
void ReplaceFilteredPermutations(void* bsShader, uint32_t shaderType,
    std::function<bool(uint32_t techniqueID)> filter);
```

- [ ] **Step 2: Implement ReplaceFilteredPermutations in ShaderReplacer.cpp**

Add at the end of `src/ShaderReplacer.cpp`. This is identical to `ReplaceAllPermutations` but with a filter check added inside both the PS and VS walk callbacks:

```cpp
void ShaderReplacer::ReplaceFilteredPermutations(void* bsShader, uint32_t shaderType,
    std::function<bool(uint32_t techniqueID)> filter)
{
    if (!bsShader) {
        spdlog::error("ShaderReplacer::ReplaceFilteredPermutations - null BSShader pointer");
        return;
    }

    auto& cache = ShaderCache::GetSingleton();

    static const std::unordered_map<uint32_t, std::wstring> kShaderFiles = {
        {8, L"Data/Shaders/Community/Lighting.hlsl"},
        {6, L"Data/Shaders/Community/Grass.hlsl"},
    };
    auto fileIt = kShaderFiles.find(shaderType);
    if (fileIt == kShaderFiles.end()) {
        spdlog::warn("ShaderReplacer: No custom HLSL for shader type {}, skipping", shaderType);
        return;
    }
    const std::wstring& hlslPath = fileIt->second;

    // --- Pixel Shader pass ---
    uint32_t totalPS = 0, replacedPS = 0, failedPS = 0, cachedPS = 0, skippedPS = 0;

    spdlog::info("ShaderReplacer: Starting filtered PS replacement for shader type {}", shaderType);

    WalkScatterTable(bsShader, kPSTableOffset, [&](uint32_t techniqueID, void* shaderObj) {
        ++totalPS;

        if (!filter(techniqueID)) {
            ++skippedPS;
            return;
        }

        uint64_t mapKey = (static_cast<uint64_t>(shaderType) << 32) | techniqueID;
        auto* slotAddr = reinterpret_cast<ID3D11PixelShader**>(
            reinterpret_cast<uintptr_t>(shaderObj) + kD3DShaderPtrOffset);
        ID3D11PixelShader* vanilla = *slotAddr;

        if (vanillaPS.find(mapKey) == vanillaPS.end()) {
            vanillaPS[mapKey] = vanilla;
        }

        std::string cacheKey = cache.MakeCacheKey(shaderType, techniqueID, true);
        ShaderCache::CompiledShader compiled;

        if (cache.LoadFromDiskCache(cacheKey, compiled) && compiled.valid && compiled.ps) {
            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;
            ++cachedPS;
            return;
        }

        auto defines = cache.BuildDefines(shaderType, techniqueID, true);
        compiled = cache.CompileShader(hlslPath, "PSMain", "ps_5_0", defines);

        if (compiled.valid && compiled.ps) {
            cache.SaveToDiskCache(cacheKey, compiled);
            *slotAddr = compiled.ps.Get();
            keepAlivePS.push_back(std::move(compiled.ps));
            ++replacedPS;
        } else {
            ++failedPS;
        }
    });

    spdlog::info("ShaderReplacer: Filtered PS - total={}, replaced={} (cached={}), failed={}, skipped={}",
        totalPS, replacedPS, cachedPS, failedPS, skippedPS);

    // --- Vertex Shader pass (skipped for parallax - VS stays vanilla) ---
    // For future use: add VS pass with same filter pattern if needed.
    spdlog::info("ShaderReplacer: VS replacement skipped (VS stays vanilla for filtered replacement)");
}
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Release 2>&1 | head -50`

Expected: Clean compilation, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/ShaderReplacer.h src/ShaderReplacer.cpp
git commit -m "feat: add ReplaceFilteredPermutations for targeted shader replacement"
```

---

### Task 4: Wire Filtered Replacement into LoadShaders Hook

**Files:**
- Modify: `src/Hooks.cpp`

- [ ] **Step 1: Replace ReplaceAllPermutations call with filtered version**

In `src/Hooks.cpp`, in the `Hook_LoadShaders` function, change the BSLightingShader branch:

Replace:
```cpp
            spdlog::info("BSShader::LoadShaders — BSLightingShader loaded at {}, triggering replacement",
                         fmt::ptr(shader));
            ShaderReplacer::GetSingleton().ReplaceAllPermutations(shader, 8);
```

With:
```cpp
            spdlog::info("BSShader::LoadShaders — BSLightingShader loaded at {}, triggering filtered replacement (POM permutations only)",
                         fmt::ptr(shader));
            ShaderReplacer::GetSingleton().ReplaceFilteredPermutations(shader, 8,
                [](uint32_t techniqueID) { return (techniqueID & 0x0800) != 0; });
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build --config Release 2>&1 | head -50`

Expected: Clean compilation.

- [ ] **Step 3: Commit**

```bash
git add src/Hooks.cpp
git commit -m "feat: filter shader replacement to parallax permutations only (0x0800 bit)"
```

---

### Task 5: Create Math.hlsli Common Include

**Files:**
- Create: `package/Shaders/Community/Common/Math.hlsli`

Ported from Skyrim CS `package/Shaders/Common/Math.hlsli`. Located under `Community/Common/` to match existing include path pattern (Lighting.hlsl uses `#include "Common/Color.hlsli"` which resolves relative to the HLSL source file in `Community/`).

- [ ] **Step 1: Create Math.hlsli**

```hlsl
#ifndef MATH_HLSLI
#define MATH_HLSLI

#define EPSILON_DOT_CLAMP 1e-5f
#define EPSILON_DIVISION 1e-6f

namespace Math
{
    static const float PI = 3.1415926535897932384626433832795f;
    static const float HALF_PI = PI * 0.5f;
    static const float TAU = PI * 2.0f;
}

#endif // MATH_HLSLI
```

- [ ] **Step 2: Commit**

```bash
git add package/Shaders/Community/Common/Math.hlsli
git commit -m "feat: add Math.hlsli common include (PI, EPSILON constants)"
```

---

### Task 6: Reconstruct Vanilla Parallax PS in Lighting.hlsl

**Files:**
- Modify: `package/Shaders/Community/Lighting.hlsl`

**BLOCKED: Do not begin this task until Task 2 Step 7 is complete and `docs/reference/fxp-parallax-analysis.md` is committed.**

The exact CB layouts, texture slots, and PS_INPUT signature come from `docs/reference/fxp-parallax-analysis.md`. The code below uses placeholder field names that MUST be replaced with the confirmed values from Phase A.

This task reconstructs the vanilla lighting math for `PARALLAX_OCCLUSION_MAPPING` permutations. The goal is visual parity with vanilla — no POM yet. This is the largest engineering task in the plan — expect multiple iterations.

**Sub-steps for reconstruction (after filling in Phase A values):**
1. Declare CBs, textures, samplers, PS_INPUT/PS_OUTPUT with Phase A-confirmed layouts
2. Normal map unpacking (reconstruct from DXBC: tangent-space normal -> world-space)
3. Ambient lighting (directional SH from SharedData or PerFrame ambient terms)
4. Directional light (N dot L diffuse + specular from PerGeometry light params)
5. Point lights (if present in parallax permutations — check Phase A)
6. Fog application (from PerFrame fog parameters)
7. Vertex color modulation + alpha
8. Validate each component individually (e.g., output only ambient to verify it matches)

- [ ] **Step 1: Create the shader scaffold with Phase A-confirmed declarations**

Replace the entire contents of `package/Shaders/Community/Lighting.hlsl` with the reconstructed shader. The structure will be:

```hlsl
// FO4VR Community Shaders — Reconstructed BSLightingShader PS
// Parallax Occlusion Mapping permutations only.
// Non-parallax technique IDs will fail to compile (intentional).
//
// CB layouts, texture slots, and PS_INPUT signature sourced from:
//   docs/reference/fxp-parallax-analysis.md

// Guard: only parallax permutations are supported by this shader.
// Non-parallax technique IDs will hit this #error, causing compilation to fail,
// and ShaderReplacer will keep the vanilla shader for those permutations.
#if !defined(PARALLAX_OCCLUSION_MAPPING)
#error "This shader only supports PARALLAX_OCCLUSION_MAPPING permutations"
#endif

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"

// ---- Constant Buffers ----
// PLACEHOLDER: Replace field names/offsets with Phase A confirmed values

cbuffer PerGeometry : register(b0) {
    // Fields from fxp-parallax-analysis.md cb0 layout
    // e.g.: float4x4 WorldViewProj;
    //        float4x4 World;
    //        float4   EyePosition;
    //        ... etc
    float4 PG_Placeholder[16]; // REPLACE with actual layout from Phase A
};

cbuffer PerMaterial : register(b1) {
    // UNCONFIRMED — field order assumed from non-Community Lighting.hlsl stub.
    // MUST verify against DXBC cb1[N] access patterns from Phase A.
    float4 LODTexParams;           // cb1[0] UNCONFIRMED
    float4 TintColor;              // cb1[1] UNCONFIRMED
    float4 EnvmapData;             // cb1[2] UNCONFIRMED
    float4 ParallaxOccData;        // cb1[3] UNCONFIRMED — x=heightScale, y=maxSteps
    float4 SpecularColor;          // cb1[4] UNCONFIRMED
    float4 SparkleParams;          // cb1[5] UNCONFIRMED
    float4 MultiLayerParallaxData; // cb1[6] UNCONFIRMED
    float4 LightingEffectParams;   // cb1[7] UNCONFIRMED — x=subSurfaceLightRolloff
};

cbuffer PerFrame : register(b2) {
    // Fields from fxp-parallax-analysis.md cb2 layout
    // Must include CameraPosition for distance calculation
    float4 PF_Placeholder[16]; // REPLACE with actual layout from Phase A
};

// ---- Feature CBs ----

#ifdef LINEAR_LIGHTING
cbuffer LinearLightingCB : register(b4) {
    float LL_Gamma;
    float LL_UseExact;
    float2 LL_Pad;
};

float3 ApplyLinearInput(float3 color) {
    if (LL_UseExact > 0.5f) return SRGBToLinear(color);
    return SRGBToLinearFast(color);
}

float3 ApplyLinearOutput(float3 color) {
    if (LL_UseExact > 0.5f) return LinearToSRGB(color);
    return LinearToSRGBFast(color);
}
#endif

#ifdef EXTENDED_MATERIALS
cbuffer ExtendedMaterialsCB : register(b5) {
    uint EM_EnablePOM;
    uint EM_EnableShadows;
    uint EM_MaxSteps;
    float EM_HeightScaleMult;
};
#include "ExtendedMaterials/ExtendedMaterials.hlsli"
#endif

// ---- Textures ----
// PLACEHOLDER: Replace slot numbers with Phase A confirmed values

Texture2D<float4> TexDiffuse : register(t0);   // VERIFY slot from Phase A
Texture2D<float4> TexNormal : register(t1);     // VERIFY slot from Phase A
Texture2D<float4> TexSpecular : register(t2);   // VERIFY slot — height in .a

SamplerState SampDiffuse : register(s0);        // VERIFY slot from Phase A
SamplerState SampNormal : register(s1);         // VERIFY slot from Phase A
SamplerState SampSpecular : register(s2);        // VERIFY slot from Phase A

// ---- Structures ----
// PLACEHOLDER: Replace with exact VS output signature from Phase A

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    float3 Normal : TEXCOORD1;      // VERIFY semantic index from Phase A
    float3 Tangent : TEXCOORD2;     // VERIFY presence for parallax permutations
    float3 Bitangent : TEXCOORD3;   // VERIFY presence for parallax permutations
    float3 WorldPos : TEXCOORD4;    // VERIFY semantic index from Phase A
    float4 Color : COLOR0;
    // Additional interpolators from Phase A...
};

struct PS_OUTPUT {
    float4 Color : SV_Target0;
    // Additional render targets from Phase A (GBuffer outputs)...
};

// ---- PS Main ----
// Reconstructed from DXBC disassembly. See fxp-parallax-analysis.md.
// Phase B: vanilla-matching output. Phase C: POM integration.

PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;

    float2 uv = input.TexCoord0;

    // === PHASE C INJECTION POINT (POM) ===
    // When EXTENDED_MATERIALS is defined and PARALLAX_OCCLUSION_MAPPING is set,
    // this is where POM displaces UVs before any texture sampling.
    // For Phase B, vanilla single-step parallax is used instead.

#if defined(PARALLAX_OCCLUSION_MAPPING)
    // Vanilla single-step parallax (Phase B — matches vanilla exactly)
    // PLACEHOLDER: Replace with exact vanilla math from Phase A disassembly
    {
        float height = TexSpecular.Sample(SampSpecular, uv).a;
        float2 viewDirTS = float2(0, 0); // REPLACE: compute from TBN + view dir
        uv += viewDirTS * (height * ParallaxOccData.x);
    }
#endif

    // Sample textures with (possibly displaced) UVs
    float4 diffuseColor = TexDiffuse.Sample(SampDiffuse, uv);
    float4 normalMap = TexNormal.Sample(SampNormal, uv);
    float4 specularMap = TexSpecular.Sample(SampSpecular, uv);

#ifdef LINEAR_LIGHTING
    diffuseColor.rgb = ApplyLinearInput(diffuseColor.rgb);
#endif

    // === LIGHTING MODEL ===
    // PLACEHOLDER: Replace with reconstructed lighting math from Phase A
    // This must match vanilla output exactly for Phase B validation.
    //
    // The reconstructed lighting will include:
    //   1. Normal unpacking from normal map
    //   2. Ambient lighting (from SharedData directional SH or PerFrame ambient)
    //   3. Directional light (N dot L, specular)
    //   4. Point lights (if applicable for this permutation)
    //   5. Fog application
    //   6. Vertex color modulation
    //
    // For now, output diffuse * vertex color as a compilable placeholder:
    float3 finalColor = diffuseColor.rgb * input.Color.rgb;

#ifdef LINEAR_LIGHTING
    finalColor = ApplyLinearOutput(finalColor);
#endif

    output.Color = float4(finalColor, diffuseColor.a);
    return output;
}
```

**NOTE TO IMPLEMENTER:** The placeholders marked `REPLACE` and `VERIFY` MUST be filled in with data from `docs/reference/fxp-parallax-analysis.md` (Phase A output). The lighting model section is the core reconstruction work — translate the DXBC assembly into readable HLSL instruction by instruction. Cross-reference with the Skyrim CS `Lighting.hlsl` for naming conventions, but the math must match FO4's vanilla output.

- [ ] **Step 2: Verify the shader compiles for a parallax technique ID**

The shader must compile with parallax defines active. Test by temporarily adding a compile check:

Run: `fxc /T ps_5_0 /E PSMain /D PSHADER=1 /D PARALLAX_OCCLUSION_MAPPING=1 /D VC=1 package/Shaders/Community/Lighting.hlsl`

Expected: Successful compilation (warnings are OK, errors are not).

- [ ] **Step 3: Verify the shader FAILS for non-parallax technique IDs**

Non-parallax IDs should fail compilation so ShaderReplacer keeps vanilla:

Run: `fxc /T ps_5_0 /E PSMain /D PSHADER=1 /D MATERIAL_ENVMAP=1 package/Shaders/Community/Lighting.hlsl`

Expected: Compilation failure (this is intentional — envmap permutations stay vanilla).

- [ ] **Step 4: Test in-game — visual parity with vanilla**

1. Build the DLL: `cmake --build build --config Release`
2. Install to FO4VR
3. Load a save near parallax-flagged objects
4. Compare rendering with and without the mod (use ImGui "Restore Vanilla Shaders" if available)
5. Parallax surfaces should look identical to vanilla

If lighting differs: iterate on the reconstruction by comparing DXBC disassembly against the HLSL output.

- [ ] **Step 5: Commit reconstructed shader**

```bash
git add package/Shaders/Community/Lighting.hlsl
git commit -m "feat: reconstruct vanilla BSLightingShader PS for parallax permutations"
```

---

## Phase C: POM Integration

### Task 7: Create ExtendedMaterials Feature Class (C++)

**Files:**
- Create: `src/Features/ExtendedMaterials.h`
- Create: `src/Features/ExtendedMaterials.cpp`
- Modify: `src/XSEPlugin.cpp`

- [ ] **Step 1: Create ExtendedMaterials.h**

```cpp
#pragma once
#include "Feature.h"
#include "Buffer.h"

class ExtendedMaterials : public Feature {
public:
    std::string GetName() override { return "Extended Materials"; }
    std::string GetShortName() override { return "ExtendedMaterials"; }
    std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
    bool HasShaderDefine(uint32_t shaderType) override;
    bool IsCore() override { return true; }

    void SetupResources() override;
    void Prepass() override;                          // Update CB once per frame
    void OnSetupGeometry(void* renderPass) override;  // Bind CB per draw call
    void DrawSettings() override;
    void SaveSettings(nlohmann::json& j) override;
    void LoadSettings(const nlohmann::json& j) override;

    bool enablePOM = true;
    bool enableShadows = true;
    uint32_t maxSteps = 0;       // 0 = automatic distance-based
    float heightScaleMult = 1.0f;

private:
    struct alignas(16) ExtendedMaterialsCBData {
        uint32_t enablePOM;
        uint32_t enableShadows;
        uint32_t maxSteps;
        float heightScaleMult;
    };
    std::unique_ptr<ConstantBuffer> settingsCB;
};
```

- [ ] **Step 2: Create ExtendedMaterials.cpp**

```cpp
#include "Features/ExtendedMaterials.h"

#include <imgui.h>

static constexpr uint32_t kBSLightingShader = 8;

bool ExtendedMaterials::HasShaderDefine(uint32_t shaderType)
{
    return shaderType == kBSLightingShader;
}

void ExtendedMaterials::SetupResources()
{
    spdlog::info("ExtendedMaterials::SetupResources - creating constant buffer");
    settingsCB = std::make_unique<ConstantBuffer>(sizeof(ExtendedMaterialsCBData));

    if (!settingsCB || !settingsCB->IsValid()) {
        spdlog::error("ExtendedMaterials::SetupResources - failed to create constant buffer");
    }
}

void ExtendedMaterials::Prepass()
{
    if (!settingsCB || !settingsCB->IsValid()) {
        return;
    }

    // Update CB data once per frame (not per draw call)
    ExtendedMaterialsCBData data{};
    data.enablePOM = enablePOM ? 1 : 0;
    data.enableShadows = enableShadows ? 1 : 0;
    data.maxSteps = maxSteps;
    data.heightScaleMult = heightScaleMult;

    settingsCB->Update(&data, sizeof(data));
}

void ExtendedMaterials::OnSetupGeometry(void* /*renderPass*/)
{
    if (!settingsCB || !settingsCB->IsValid()) {
        return;
    }

    // Bind CB per draw call (data already updated in Prepass)
    ScopedD3DState guard;
    settingsCB->PSBind(5);  // b5 — verified unused during Phase A
}

void ExtendedMaterials::DrawSettings()
{
    ImGui::Checkbox("Enable POM", &enablePOM);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enables Parallax Occlusion Mapping on surfaces with height data "
            "in the specular texture alpha channel (_s.dds). "
            "Compatible with ENB Complex Parallax texture packs.");
    }

    ImGui::Checkbox("Enable Parallax Shadows", &enableShadows);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adds cheap self-shadowing to parallax surfaces based on height data.");
    }

    int steps = static_cast<int>(maxSteps);
    if (ImGui::SliderInt("Max Steps", &steps, 0, 32, steps == 0 ? "Auto" : "%d")) {
        maxSteps = static_cast<uint32_t>(steps);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximum POM ray-march steps. 0 = automatic (8 interior, 16 exterior). "
                          "Higher values = more detail but lower performance.");
    }

    ImGui::SliderFloat("Height Scale", &heightScaleMult, 0.1f, 3.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Multiplier on the material's parallax height scale. "
                          "1.0 = default, higher = deeper parallax effect.");
    }
}

void ExtendedMaterials::SaveSettings(nlohmann::json& j)
{
    j["enablePOM"] = enablePOM;
    j["enableShadows"] = enableShadows;
    j["maxSteps"] = maxSteps;
    j["heightScaleMult"] = heightScaleMult;
}

void ExtendedMaterials::LoadSettings(const nlohmann::json& j)
{
    if (j.contains("enablePOM")) enablePOM = j["enablePOM"].get<bool>();
    if (j.contains("enableShadows")) enableShadows = j["enableShadows"].get<bool>();
    if (j.contains("maxSteps")) maxSteps = j["maxSteps"].get<uint32_t>();
    if (j.contains("heightScaleMult")) heightScaleMult = j["heightScaleMult"].get<float>();
}
```

- [ ] **Step 3: Register feature in XSEPlugin.cpp**

Add include at top of `src/XSEPlugin.cpp`:
```cpp
#include "Features/ExtendedMaterials.h"
```

Add global instance after `g_linearLighting`:
```cpp
static ExtendedMaterials g_extendedMaterials;
```

Add registration after `g_linearLighting` registration in `kPostPostLoad`:
```cpp
Feature::RegisterFeature(&g_extendedMaterials);
```

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --config Release 2>&1 | head -50`

Expected: Clean compilation.

- [ ] **Step 5: Commit**

```bash
git add src/Features/ExtendedMaterials.h src/Features/ExtendedMaterials.cpp src/XSEPlugin.cpp
git commit -m "feat: add ExtendedMaterials feature class with POM settings CB on b5"
```

---

### Task 8: Create ExtendedMaterials.hlsli POM Shader

**Files:**
- Create: `package/Shaders/Community/ExtendedMaterials/ExtendedMaterials.hlsli`

Adapted from Skyrim CS `features/Extended Materials/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli`. Simplified for FO4 (no TRUE_PBR, no TERRAIN_VARIATION, no LANDSCAPE for this sub-project).

- [ ] **Step 1: Create ExtendedMaterials.hlsli**

```hlsl
#ifndef EXTENDED_MATERIALS_HLSLI
#define EXTENDED_MATERIALS_HLSLI

// Parallax Occlusion Mapping for FO4VR Community Shaders
// Adapted from Skyrim Community Shaders ExtendedMaterials.hlsli
// Reference: https://github.com/doodlum/skyrim-community-shaders

struct DisplacementParams
{
    float DisplacementScale;
    float DisplacementOffset;
    float HeightScale;
    float FlattenAmount;
};

namespace ExtendedMaterials
{
    float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
    {
        return (displacement - 0.5) * params.DisplacementScale + 0.5 + params.DisplacementOffset;
    }

    float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
    {
        return float4(
            AdjustDisplacementNormalized(displacement.x, params),
            AdjustDisplacementNormalized(displacement.y, params),
            AdjustDisplacementNormalized(displacement.z, params),
            AdjustDisplacementNormalized(displacement.w, params));
    }

    float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
    {
        float2 textureDims;
        tex.GetDimensions(textureDims.x, textureDims.y);

        // VR: halve texture dims for more conservative mip selection
        if (CS_IsVR) textureDims /= 2.0;

        float2 texCoordsPerSize = coords * textureDims;
        float2 dxSize = ddx(texCoordsPerSize);
        float2 dySize = ddy(texCoordsPerSize);

        float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));
        float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

        // VR: extra mip bias to reduce stereo shimmer
        if (CS_IsVR) mipLevel++;

        // Stochastic mip selection to reduce banding
        mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

        return mipLevel;
    }

    // Main POM ray-march with contact refinement
    // Returns displaced UV coordinates
    float2 GetParallaxCoords(
        float distance,
        float2 coords,
        float mipLevel,
        float3 viewDir,
        float3x3 tbn,
        float noise,
        Texture2D<float4> tex,
        SamplerState texSampler,
        uint channel,
        DisplacementParams params,
        out float pixelOffset)
    {
        float3 viewDirTS = normalize(mul(tbn, viewDir));
        viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;

        float nearBlendToFar = saturate(distance / 2048.0);

        float scale = params.HeightScale;
        float maxHeight = 0.1 * scale;
        float minHeight = maxHeight * 0.5;

        if (nearBlendToFar < 1.0)
        {
            float maxSteps = CS_IsInterior ? 8.0 : 16.0;
            // VR: halve step count for performance (applied to all VR, not just instanced stereo)
            if (CS_IsVR) maxSteps *= 0.5;
            if (EM_MaxSteps > 0)
                maxSteps = (float)EM_MaxSteps;

            uint numSteps = uint((maxSteps * (1.0 - nearBlendToFar)) + 0.5);
            numSteps = clamp(numSteps, 1, max(6, scale * maxSteps));

            float stepSize = rcp(numSteps);
            float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
            float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

            float prevBound = 1.0;
            float prevHeight = 1.0;

            float2 pt1 = 0;
            float2 pt2 = 0;

            uint numStepsTemp = numSteps;
            bool contactRefinement = false;

            [loop] while (numSteps > 0)
            {
                float4 currentOffset[2];
                currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
                currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
                float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

                float4 currHeight;
                currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
                currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
                currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
                currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

                currHeight = AdjustDisplacementNormalized(currHeight, params);

                bool4 testResult = currHeight >= currentBound;
                [branch] if (any(testResult))
                {
                    float2 outOffset = 0;
                    [flatten] if (testResult.w)
                    {
                        outOffset = currentOffset[1].xy;
                        pt1 = float2(currentBound.w, currHeight.w);
                        pt2 = float2(currentBound.z, currHeight.z);
                    }
                    [flatten] if (testResult.z)
                    {
                        outOffset = currentOffset[0].zw;
                        pt1 = float2(currentBound.z, currHeight.z);
                        pt2 = float2(currentBound.y, currHeight.y);
                    }
                    [flatten] if (testResult.y)
                    {
                        outOffset = currentOffset[0].xy;
                        pt1 = float2(currentBound.y, currHeight.y);
                        pt2 = float2(currentBound.x, currHeight.x);
                    }
                    [flatten] if (testResult.x)
                    {
                        outOffset = prevOffset;
                        pt1 = float2(currentBound.x, currHeight.x);
                        pt2 = float2(prevBound, prevHeight);
                    }
                    if (contactRefinement) {
                        break;
                    } else {
                        contactRefinement = true;
                        prevOffset = outOffset;
                        prevBound = pt2.x;
                        numSteps = numStepsTemp;
                        stepSize /= (float)numSteps;
                        offsetPerStep /= (float)numSteps;
                        continue;
                    }
                }

                prevOffset = currentOffset[1].zw;
                prevBound = currentBound.w;
                prevHeight = currHeight.w;
                numSteps -= 4;
            }

            float delta2 = pt2.x - pt2.y;
            float delta1 = pt1.x - pt1.y;
            float denominator = delta2 - delta1;

            float parallaxAmount = 0.0;
            [flatten] if (denominator == 0.0)
                parallaxAmount = 0.0;
            else
                parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;

            nearBlendToFar *= nearBlendToFar;
            float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
            pixelOffset = lerp(parallaxAmount * scale, 0, nearBlendToFar);
            return lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
        }

        pixelOffset = 0;
        return coords;
    }

    // Cheap parallax self-shadows
    // Reference: Tatarchuk 2006, "Practical Parallax Occlusion Mapping"
    float GetParallaxSoftShadowMultiplier(
        float2 coords,
        float mipLevel,
        float3 L,
        float sh0,
        Texture2D<float4> tex,
        SamplerState texSampler,
        uint channel,
        float quality,
        float noise,
        DisplacementParams params)
    {
        [branch] if (quality > 0.0)
        {
            float2 rayDir = L.xy * 0.1 * params.HeightScale;
            float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
            float4 sh;
            sh.x = AdjustDisplacementNormalized(
                tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
            if (quality > 0.25)
                sh.y = AdjustDisplacementNormalized(
                    tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
            if (quality > 0.5)
                sh.z = AdjustDisplacementNormalized(
                    tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
            if (quality > 0.75)
                sh.w = AdjustDisplacementNormalized(
                    tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);
            return pow(1.0 - saturate(dot(max(0, sh - sh0), 1.0)) * quality, 2.0);
        }
        return 1.0;
    }
}

#endif // EXTENDED_MATERIALS_HLSLI
```

- [ ] **Step 2: Commit**

```bash
git add package/Shaders/Community/ExtendedMaterials/ExtendedMaterials.hlsli
git commit -m "feat: add POM ray-march shader (ported from Skyrim CS ExtendedMaterials)"
```

---

### Task 9: Integrate POM into Lighting.hlsl

**Files:**
- Modify: `package/Shaders/Community/Lighting.hlsl`

This task replaces the vanilla single-step parallax (from Phase B) with the POM ray-march when `EXTENDED_MATERIALS` is defined.

- [ ] **Step 1: Add POM integration to PSMain**

In `package/Shaders/Community/Lighting.hlsl`, replace the vanilla parallax block inside PSMain with:

```hlsl
#if defined(PARALLAX_OCCLUSION_MAPPING)
    // Build TBN matrix for tangent-space view direction
    float3x3 tbn = float3x3(
        normalize(input.Tangent),
        normalize(input.Bitangent),
        normalize(input.Normal));

    // Camera-to-pixel distance for fade and step count
    // REPLACE: Use confirmed CameraPosition field from Phase A PerFrame CB
    float3 cameraPos = PF_Placeholder[0].xyz; // REPLACE with actual field
    float viewDist = length(input.WorldPos - cameraPos);
    float3 viewDir = normalize(cameraPos - input.WorldPos);

    // Screen noise for stochastic mip selection
    float screenNoise = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));

    float mipLevel = ExtendedMaterials::GetMipLevel(uv, TexSpecular, screenNoise);

#ifdef EXTENDED_MATERIALS
    // POM ray-march (Phase C)
    DisplacementParams dispParams;
    float pomScale = ParallaxOccData.x * EM_HeightScaleMult;
    dispParams.DisplacementScale = pomScale;
    dispParams.DisplacementOffset = 0.0;
    dispParams.HeightScale = pomScale;
    dispParams.FlattenAmount = 0.0;

    float pixelOffset = 0;
    if (EM_EnablePOM) {
        uv = ExtendedMaterials::GetParallaxCoords(
            viewDist, uv, mipLevel, viewDir, tbn,
            screenNoise, TexSpecular, SampSpecular, 3,
            dispParams, pixelOffset);
    }
#else
    // Vanilla single-step parallax (fallback if EXTENDED_MATERIALS not defined)
    {
        float height = TexSpecular.Sample(SampSpecular, uv).a;
        float3 viewDirTS = normalize(mul(tbn, viewDir));
        uv += viewDirTS.xy * (height * ParallaxOccData.x);
    }
#endif // EXTENDED_MATERIALS
#endif // PARALLAX_OCCLUSION_MAPPING
```

- [ ] **Step 2: Add parallax shadow integration (after lighting calculation)**

After the main directional light calculation in the lighting model, add:

```hlsl
#if defined(PARALLAX_OCCLUSION_MAPPING) && defined(EXTENDED_MATERIALS)
    if (EM_EnableShadows && EM_EnablePOM) {
        float3 lightDirTS = normalize(mul(tbn, CS_DirLightDirection.xyz));
        float sh0 = TexSpecular.SampleLevel(SampSpecular, uv, mipLevel).a;
        sh0 = ExtendedMaterials::AdjustDisplacementNormalized(sh0, dispParams);
        float shadowMult = ExtendedMaterials::GetParallaxSoftShadowMultiplier(
            uv, mipLevel, lightDirTS, sh0,
            TexSpecular, SampSpecular, 3,
            1.0, screenNoise, dispParams);
        // Apply shadow to directional light contribution
        directionalLight *= shadowMult;
    }
#endif
```

- [ ] **Step 3: Verify compilation with EXTENDED_MATERIALS defined**

Run: `fxc /T ps_5_0 /E PSMain /D PSHADER=1 /D PARALLAX_OCCLUSION_MAPPING=1 /D EXTENDED_MATERIALS=1 /D VC=1 package/Shaders/Community/Lighting.hlsl`

Expected: Successful compilation.

- [ ] **Step 4: Verify compilation without EXTENDED_MATERIALS (vanilla fallback)**

Run: `fxc /T ps_5_0 /E PSMain /D PSHADER=1 /D PARALLAX_OCCLUSION_MAPPING=1 /D VC=1 package/Shaders/Community/Lighting.hlsl`

Expected: Successful compilation (uses vanilla single-step parallax path).

- [ ] **Step 5: Commit**

```bash
git add package/Shaders/Community/Lighting.hlsl
git commit -m "feat: integrate POM ray-march into Lighting.hlsl for parallax permutations"
```

---

### Task 10: Add Rollback Mechanism to Menu

**Files:**
- Modify: `src/ShaderReplacer.h`
- Modify: `src/ShaderReplacer.cpp`
- Modify: `src/Menu.cpp`

- [ ] **Step 1: Add RestoreVanillaShaders method to ShaderReplacer**

In `src/ShaderReplacer.h`, add the slot tracking map and restore method:
```cpp
// Slot address tracking for rollback (populated during replacement)
std::unordered_map<uint64_t, ID3D11PixelShader**> vanillaPSSlots;

// Restore all vanilla PS pointers (undo all replacements)
void RestoreAllVanillaPS();
```

In `src/ShaderReplacer.cpp`, add:
```cpp
void ShaderReplacer::RestoreAllVanillaPS()
{
    uint32_t restored = 0;

    for (auto& [mapKey, vanillaPtr] : vanillaPS) {
        if (!vanillaPtr) continue;

        auto slotIt = vanillaPSSlots.find(mapKey);
        if (slotIt == vanillaPSSlots.end() || !slotIt->second) continue;

        *slotIt->second = vanillaPtr;
        ++restored;

        spdlog::debug("ShaderReplacer: Restored vanilla PS for key {:#018x}", mapKey);
    }

    spdlog::info("ShaderReplacer::RestoreAllVanillaPS - restored {} shaders", restored);
}
```

Also in `ReplaceFilteredPermutations`, add slot tracking alongside the vanilla pointer save:
```cpp
// After: vanillaPS[mapKey] = vanilla;
// Add:   vanillaPSSlots[mapKey] = slotAddr;
```

- [ ] **Step 2: Add rollback button to Menu.cpp**

In `src/Menu.cpp`, in `DrawShaderCacheStatus()`, after the "Clear Shader Cache" button:

```cpp
ImGui::Spacing();
if (ImGui::Button("Restore Vanilla Shaders")) {
    ShaderReplacer::GetSingleton().RestoreAllVanillaPS();
    spdlog::info("Vanilla shaders restored from menu");
}
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Restore all original shaders. Useful for A/B comparison.");
}
```

- [ ] **Step 3: Commit**

```bash
git add src/ShaderReplacer.h src/ShaderReplacer.cpp src/Menu.cpp
git commit -m "feat: add vanilla shader rollback mechanism for A/B comparison"
```

---

### Task 11: In-Game Validation

**Files:** None (testing only)

- [ ] **Step 1: Build and install**

Run: `cmake --build build --config Release`

Copy DLL + shader files to FO4VR Data directory.

- [ ] **Step 2: Test with Vivid Fallout landscape parallax**

Enable "Vivid Fallout - Landscapes - Complex Parallax Occlusion" in MO2.
Load a save in an outdoor area with varied terrain.

Expected: Landscape surfaces show depth/parallax effect. Walking toward surfaces shows increasing detail. Distance fade is smooth.

- [ ] **Step 3: Test with FO4 HD Overhaul Parallax**

Enable "FO4 HD Overhaul Parallax" in MO2.
Load near buildings/walls.

Expected: Object surfaces (walls, floors, concrete) show parallax depth.

- [ ] **Step 4: Test ImGui controls**

Press F10 to open menu. Under "Extended Materials":
- Toggle "Enable POM" off/on — parallax should appear/disappear
- Adjust "Height Scale" slider — depth should increase/decrease
- Toggle "Enable Parallax Shadows" — shadow detail on surfaces should change
- Adjust "Max Steps" — quality/performance tradeoff

- [ ] **Step 5: Test VR stereo**

Wear headset. Look at parallax surfaces:
- Both eyes should see consistent displacement (natural depth)
- No shimmer at distance
- No obvious stereo mismatch

- [ ] **Step 6: Performance check**

Open SteamVR frame timing or use RenderDoc.
Compare frame times with POM enabled vs disabled.
Target: <1ms overhead.

- [ ] **Step 7: Document results**

Create test notes at: `artifacts/fo4vr-community-shaders/<date>/dev_test_notes.md`

Record: screenshots, performance numbers, any issues found, VR observations.
