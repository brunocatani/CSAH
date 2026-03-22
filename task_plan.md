# FO4VR Community Shaders — POM Implementation Plan

## Goal
Implement Parallax Occlusion Mapping (POM) for Fallout 4 VR by porting Skyrim Community Shaders' Extended Materials system. Hook BSLightingShader's rendering pipeline, intercept parallax-flagged pixel shaders, and replace them with custom HLSL that includes POM ray-marching.

## Current Status: SHADER REPLACEMENT WORKING — Need parallax PS identification

### What Works
- Full plugin infrastructure (D3D init, features, hooks, ImGui menu, EngineFixes)
- BSLightingShader singleton found (base+0x68794b0, type=8, confirmed)
- VR vtable correctly identified (base+0x30bbdb8)
- HLSL shader code ready (Lighting.hlsl + ExtendedMaterials.hlsli with POM)
- ShaderCache compilation pipeline ready
- Draw-time PS swap code ready (just needs working hook point)

### Critical Blocker
FO4VR does NOT call BSLightingShader::BeginTechnique through any hookable path:
- Detour on function body: E9 JMP confirmed patched, never fires
- Vtable[4] patch: installed, never fires (devirtualized calls)
- ID3D11DeviceContext::PSSetShader vtable[9] hook: installed, never fires
- All xrefs to BeginTechnique are .pdata (exception handling), not code calls

---

## Phases

### Phase 1: Plugin Infrastructure [COMPLETE]
- F4SE plugin, D3D init, feature system, ImGui menu, settings

### Phase 2: Shader Pipeline [COMPLETE]
- ShaderCache (HLSL compilation, disk cache), ShaderReplacer, FXP analysis

### Phase 3: HLSL Shaders [COMPLETE]
- Lighting.hlsl (reconstructed BS pixel shader), ExtendedMaterials.hlsli (POM)

### Phase 4: Hook BSLightingShader Rendering [IN PROGRESS - BLOCKED]
**Status:** `blocked`

**Approaches tried:**
1. Scatter table pointer swap — tables empty at runtime (FXP loader timing)
2. LoadShaders Detour — broke FXP loading (void() vs void(void*) mismatch)
3. BSLightingShader::BeginTechnique Detour — function genuinely never called
4. BSLightingShader vtable[4] patch — devirtualized, never dispatched through vtable
5. ID3D11DeviceContext::PSSetShader vtable hook — hook never fires

**Next approaches to try:**
- A. Compare cached context vs device->GetImmediateContext() (latest build, untested)
- B. Hook d3d11.dll PSSetShader at DLL level (IAT hook) instead of COM vtable
- C. Find FO4VR's actual render dispatch via Ghidra (function pointer tables)
- D. Hook the Renderer::SetShaders function (FUN_141d92c00) called from base BeginTechnique

### Phase 5: In-Game Validation [PENDING]
- Test POM effect on parallax texture packs (Vivid Fallout, FO4 HD Overhaul)

---

## Key Addresses (FO4VR, confirmed)

| Item | RVA | Notes |
|------|-----|-------|
| BSLightingShader vtable | 0x30bbdb8 | VR, from constructor |
| BSLightingShader singleton | 0x68794b0 | From global LoadShaders |
| BSLightingShader::BeginTechnique | 0x28B5C10 | vtable[4], 3-param, NEVER CALLED |
| BSLightingShader::SetupMaterial | 0x28B6240 | vtable[6] |
| BSShader::BeginTechnique (base) | 0x2814BE0 | 6-param, fires for non-lighting |
| Renderer::SetShaders | 0x1D92C00 | Called from base BeginTechnique |
| FXP Loader | 0x2814260 | Loads scatter tables |
| Global LoadShaders | 0x27F4800 | void(), creates all shaders |
| Geometry submission | 0x27A3F90 | Creates BSRenderPass for lighting |
| Main render frame | 0x0D83DE0 | Orchestrates entire frame |
| Renderer | 0x60F3CE8 | |
| Device | 0x60F3CF0 | |
| Context | 0x60F3D08 | May NOT be the rendering context |

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| Scatter tables empty | 5+ tests | FXP loading works but tables at +0xB8 empty — abandoned approach |
| LoadShaders Detour broke FXP | 1 test | Removed — void() function hooked as void(void*) |
| BeginTechnique never fires | Detour + vtable | Function genuinely never called at runtime |
| PSSetShader hook silent | vtable[9] on cached context | May be wrong context — need to compare with GetImmediateContext |
| Other mods pollute log | spdlog sharing | Set named logger "CS" but still shares default |
