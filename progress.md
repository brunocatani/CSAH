# Progress Log — FO4VR Community Shaders

## Session 2026-03-21 (Session A — earlier, context compacted)
- Built full plugin infrastructure end-to-end
- Extracted FXP DXBC shaders, analyzed parallax permutations
- Created ShaderCache, ShaderReplacer, Lighting.hlsl, ExtendedMaterials.hlsli
- Discovered scatter tables empty at runtime
- Multiple Ghidra RE sessions to find BSLightingShader layout
- Code review applied (6 fixes)
- State saved to docs/reference/session-state-2026-03-21.md

## Session 2026-03-21 (Session B — this session)

### Test 1: Scatter table diagnostic (both pointer sources)
- RAW singleton and REF target are SAME object (0x170190f6080)
- type=8, VS count=0, PS count=0, sentinel set, buckets null
- **Result:** Scatter tables genuinely never populated

### Test 2: Base BeginTechnique hook with BSLightingShader tracking
- First 20 calls: all isLighting=false (other shaders)
- BSLightingShader NEVER appears in base BeginTechnique calls
- Other shaders (BSEffectShader, BSWaterShader) DO fire

### Test 3: BSLightingShader vtable[4] hook
- Installed on correct VR vtable (base+0x30bbdb8)
- Original confirmed: base+0x28B5C10 (matches Ghidra)
- Hook NEVER fires — game devirtualizes the call

### Test 4: BSLightingShader Detour (on function body)
- Prologue bytes confirmed: E9 63 A5 FE BF (JMP rel32)
- Detour IS applied correctly
- Hook NEVER fires — function genuinely never called at runtime

### Test 5: PSSetShader vtable hook on cached context
- Installed on Globals::GetContext() context
- Hook installed successfully
- Hook NEVER fires — wrong context object?

### Key discovery: Skyrim CS approach
- Researched Skyrim Community Shaders source code
- They hook BeginTechnique + patch CALL sites inside it
- They call context->PSSetShader() directly
- FO4 is fundamentally different — no BeginTechnique dispatch

### Key discovery: Ghidra deep trace
- All xrefs to BSLightingShader::BeginTechnique are .pdata (exception handling)
- Zero direct CALL instructions in entire binary
- FO4 uses function pointer dispatch tables, not virtual calls
- Geometry submission (FUN_1427a3f90) creates render passes with BSLightingShader

### Latest build (UNTESTED):
- Compares cached context vs device->GetImmediateContext()
- If different, hooks the REAL context
- Unconditional PSSetShader logging (no frame check)

### Test 6: PSSetShader with GetImmediateContext comparison
- Context comparison: cached == GetImmediateContext (SAME address)
- PSSetShader hook FIRES! 5 unique PS logged, 1000 calls in ~1 second
- **BREAKTHROUGH**: D3D-level interception working
- Previous test failure likely due to stale build/deploy

### Test 7: Differential PS tracking with gameplay
- Initial (3000 calls): 6 known + 2 unknown PS
- After gameplay (30000 calls): 33 known + **129 unknown** PS
- 129 unique BSLightingShader pixel shaders identified by exclusion!
- Scatter table scan didn't fire (timing issue — need earlier trigger)

### Test 8: Visual blanket test (RED shader replacement)
- F9 toggles ALL unknown-path PS to flat red
- **RESULT: ALL LIT SURFACES TURNED RED IN VR**
- Hands (different shader) stayed normal
- UI (different pipeline) stayed normal
- Toggle back to normal with F9 works
- **PIPELINE CONFIRMED WORKING END-TO-END**

### Test 9: Scatter table scan (frame 5)
- VS scatter: count=0 buckets=null
- PS scatter: count=0 buckets=null
- **SCATTER TABLES PERMANENTLY EMPTY IN FO4VR** — even without LoadShaders Detour
- FXP data stored elsewhere in VR build

### CURRENT STATUS: SHADER REPLACEMENT WORKING
- PSSetShader D3D hook: WORKING ✓
- BSLightingShader PS identification: WORKING (120 found via differential) ✓
- Visual PS swap: WORKING (red test confirmed) ✓
- Scatter tables: PERMANENTLY EMPTY (cannot use for technique ID mapping)
- **Need: identify which of the 120 PS are parallax permutations**

### Test 10: Early CreatePixelShader hook (temp device)
- D3D_DRIVER_TYPE_HARDWARE: failed (VR blocks it)
- D3D_DRIVER_TYPE_NULL: failed (D3D11 not loaded at PostPostLoad time)
- VR FXP extracted: Shaders012_VR.fxp → 1065 unique parallax DXBC hashes
- Hashes loaded at GameDataReady but zero matches (PS already created before hook)

### Test 11: Real Lighting.hlsl PS replacement
- Removed #error guard from Lighting.hlsl
- F6 compiles default permutation (tech ID 0x0101) and replaces ALL unknown PS
- **SCENE RENDERS THROUGH OUR SHADER CODE IN VR**
- "Silent Hill" look — expected with single generic PS for all 119 permutations
- Diffuse textures visible, normal mapping working, GBuffer writes correct
- Missing: per-permutation material handling, proper fog, specular, lighting params

### Test 12: PS_INPUT layout diagnostics
- TEXCOORD0 has tangent data for distant surfaces, zero for nearby
- Diffuse texture NOT bound at t0 for most surfaces (blue debug = no texture)
- All texture slots (t0-t7) + vertex color = zero for ground surfaces
- ROOT CAUSE: PS input signatures differ per permutation (wildly different layouts)
  - Simple: `SV_POSITION, COLOR, TEXCOORD`
  - Complex: `SV_POSITION, TEXCOORD, TEXCOORD, COLOR, POSITION*3, SV_CullDistance, SV_ClipDistance`
  - VR: adds `EYEINDEX`
- Cannot use ONE PS for ALL 120 permutations — need per-layout PS or match vanilla layout

### MILESTONE: Custom HLSL running on all BSLightingShader surfaces in FO4VR

### SESSION END STATUS (2026-03-21)
**Working pipeline**: PSSetShader hook → differential tracking → 125 unknown PS → F9 visual swap
**Blocked on**: Identifying which PS are parallax (timing issue with CreatePixelShader hook)

### Test 13: Diagnostic HLSL - VS output layout analysis
- TEXCOORD0.xyz has tangent data for DISTANT surfaces, zero for nearby
- Diffuse texture t0 NOT bound for most surfaces
- ALL texture slots t0-t7 return black at T3.w/T4.w UVs
- Vertex color (COLOR0) also zero for ground
- VR FXP ISGN analysis shows 5+ different PS input layouts
- CONCLUSION: Cannot use one PS for all 128 permutations

### FINAL SESSION STATUS (2026-03-21 late night)
**Working**: PSSetShader hook, differential tracking, magenta visual test, F6 Lighting.hlsl compilation
**Blocked**: PS input signature mismatch AND CreatePixelShader early hook timing

### Next session priority approaches:
1. **Disassemble the most common VR DXBC PS** to find the real input layout (ISGN + register mapping)
2. **Try F4SE_PreLoadCallback or DLL_PROCESS_ATTACH** for earlier hook installation
3. **Alternative: don't replace PS — inject via render state modification** (modify CBs/SRVs only)
4. **Alternative: use D3D11 shader reflection** on the CURRENTLY BOUND PS to read its ISGN at runtime

## Session 2026-03-21 (Session C — continuation)

### ISGN Analysis: 105 unique PS input layouts, key discovery
- Wrote `tools/analyze_isgn.py` — parsed all 2283 VR PS from FXP
- 1624 PS have POM bit set, distributed across ~48 layout groups
- **Layout #1 (420 PS, 238 POM)**: SV_POSITION + EYEINDEX + CullDist/ClipDist ONLY — **depth pre-pass, NO texcoords**
- **Layout #3 (144 PS, 134 POM)**: FULL GBuffer layout — SV_POSITION, TEXCOORD0-4, COLOR0, EYEINDEX, SV_IsFrontFace
- **Layout #4 (123 PS, 117 POM)**: Same as #3 but NO COLOR0
- Layout #3 + #4 = 251 POM shaders = the main GBuffer parallax permutations

### DXBC Disassembly: Register Mapping Confirmed
- Wrote `tools/disasm_dxbc.py` — SM5 bytecode disassembler
- Disassembled shader_2496 (Layout #3 POM):
  - v1.xyz TEXCOORD0 = **tangent** (normalized, used in TBN)
  - v2.xyz TEXCOORD1 = **bitangent** (normalized, used in TBN)
  - v3.xyz TEXCOORD2 = **normal** (used in TBN, front-face flip)
  - v4.w TEXCOORD3.w = **UV.u** (mov r1.x, v4.w)
  - v5.w TEXCOORD4.w = **UV.v** (mov r1.y, v5.w)
  - v6 COLOR0 = **vertex color** (mul diffuse * v6)
  - v7 EYEINDEX = VR eye index
  - v8 SV_IsFrontFace = normal Z flip
- **FO4VR packs UVs into TEXCOORD3.w/TEXCOORD4.w** (unlike Skyrim which uses TEXCOORD0.xy!)
- Cross-referenced with Skyrim CS source (E:\fo4dev\skirymvr_mods\source_codes\skyrim-community-shaders)

### Root Cause of Black Output Identified
Previous black output was caused by TWO issues:
1. **Missing EYEINDEX in PS_INPUT** — register assignment mismatch
2. **Replacing depth pre-pass PS** — Layout #1 has no texcoords, our PS reads zeros → black
   - Magenta test worked because it takes NO inputs (void PSMain → writes constant color)

### Fixes Applied
1. Added `uint EyeIndex : EYEINDEX` to PS_INPUT (v7, between COLOR0 and SV_IsFrontFace)
2. Added VertexColor fallback: if all zero (Layout #4), treat as white (1,1,1,1)
3. Added render target count check in Hook_PSSetShader: only replace when >=4 RTs bound (GBuffer pass)
4. Deployed updated Lighting.hlsl to D:\FO4\mods\Community Shaders\Shaders\Community\
5. Build succeeds

### Test 14: Correct ISGN + RT filtering
- Textures visible but flat/dark lighting (ambient only)
- RT filter (>=4 RTs) correctly skips depth pre-pass
- SRV filter (t0+t1+t2) correctly skips simple surfaces

### Test 15: ISGN classification via CreatePixelShader hook
- D3D11CreateDevice Detour → CreatePixelShader hook → ISGN parser
- Captured 2712 PS: L3=233, L4=203, Fwd=498, Depth=552, Unk=1226
- Dual filter (differential + ISGN) reduces false replacements
- Visual still dark/flat

### Test 16: Rainbow normal diagnostic
- Output abs(geometricNormal) as albedo color
- **BRIGHT COLORFUL OUTPUT** — normals ARE correct, view-space, Y-up
- Colors change with camera direction → confirms VIEW-SPACE normals
- Ground = yellow-green (Y dominant), walls = red/blue (X/Z dominant)

### Test 17: Black/white directional test
- White albedo + geometric normal + non-zero T2/T3/T4
- Mix of pure black and pure white → directional lighting WORKS
- But no ambient fill (T2/T3/T4 values killed ambient)

### Test 18: White albedo + geometric normal + T2/T3/T4=zeros
- **CORRECT DIRECTIONAL SHADING** — bright sun side, visible shadow side
- Confirms: Lambert azimuthal encoding CORRECT
- Confirms: T2/T3/T4 must be zeros for proper ambient
- Confirms: geometric normals in view-space work

### Test 19: Textured + geometric normal + T2/T3/T4=zeros
- Result: still dark/flat
- With TBN normal mapping: also dark/flat
- Theory: real texture values (0.1-0.5) × dim ambient (~0.15) = too dark to see

### SESSION END STATUS (2026-03-22)
**CONFIRMED WORKING:**
- D3D11CreateDevice hook → CreatePixelShader ISGN classification
- PSSetShader dual-filter (differential tracking + ISGN layout)
- Lambert azimuthal normal encoding (proven with white albedo test)
- View-space geometric normals (proven with rainbow diagnostic)
- T2/T3/T4=zeros for ambient lighting

**REMAINING ISSUE:**
Real diffuse textures × ambient-only lighting = too dark.
The deferred composite needs specific T2/T3/T4 values for proper brightness.
White albedo works because 0.8 × 0.15 ambient = still visible.
Texture albedo fails because 0.3 × 0.15 ambient = near black.

**NEXT SESSION PRIORITIES:**
1. Use Ghidra to find FO4VR's deferred composite shader — see what T2/T3/T4 values affect ambient brightness
2. Test varying T2/T3/T4 values systematically (one at a time) with white albedo
3. Check if vanilla T2/T3 have specific scaling that boosts lighting output
4. Consider: the "dark" might just be correct for this time of day/weather — try at noon with clear sky
