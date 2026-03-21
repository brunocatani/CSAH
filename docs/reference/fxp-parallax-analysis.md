# FXP Parallax Shader Pipeline Analysis

**Source:** DXBC disassembly of BSLightingShader FXP permutations extracted from Shaders011.fxp
**Shaders analyzed:** 2682 (rich POM PS, 5684 bytes), 2624 (mid-complexity POM PS, 3012 bytes)
**Date:** 2026-03-20

---

## CRITICAL FINDING: FO4 Has NO Vanilla Parallax Displacement in Shaders

**UPDATE (post-VS investigation):** The initial analysis assumed parallax happened in the VS.
After disassembling the BSLightingShader VS (shader 2358, 4992 bytes, 9 TEXCOORD outputs),
we confirmed that the VS does NOT perform any parallax displacement. The UV math is:

```
outputUV = inputUV * cb1[0].zw + cb1[0].xy   // simple texture scale+offset transform
```

This is just the standard material texture coordinate transform (tiling/offset from .bgsm),
NOT a height-based parallax offset. There is no height map sampling in the VS at all.

**The PS also has no parallax code** — it receives the texture-transformed UVs in v4.w/v5.w
and samples textures directly without any displacement.

**What this means for our POM implementation:**
- The UVs at v4.w/v5.w are **clean, undisplaced texture coordinates**
- There is **no vanilla parallax to suppress or undo**
- We simply inject POM ray-marching in the PS before texture sampling
- The `PARALLAX_OCCLUSION_MAPPING` flag (0x0800) exists in the technique system but
  the compiled shaders don't implement any displacement — ENB adds parallax via its own
  shader injection, which is why the texture packs were designed for ENB
- This makes our job much simpler: we're ADDING POM, not REPLACING existing parallax

---

## CB Register Allocation

| Slot | HLSL binding | Name           | Owner                  |
|------|-------------|----------------|------------------------|
| b2   | CB2          | PerMaterial    | BSLightingShader (game)|
| b3   | —            | SharedData     | Community Shaders CB   |
| b4   | —            | LinearLighting | Community Shaders CB   |
| b5   | —            | **ExtendedMaterials** | **Our POM CB — SAFE, confirmed unused by game** |
| b12  | CB12         | PerGeometry    | BSLightingShader (game)|

**b5 is confirmed unused** by all disassembled FXP shader permutations. It is safe to bind
our `ExtendedMaterials` constant buffer here without conflicting with any existing game data.

---

## Texture Slot Map

| Register | Sampler | Texture            | Notes                              |
|----------|---------|--------------------|------------------------------------|
| t0       | s0      | Diffuse (_d.dds)   | RGB = albedo, A = alpha mask       |
| t1       | s1      | Normal (_n.dds)    | XY = tangent-space normal          |
| t2       | s2      | Specular (_s.dds)  | **Alpha channel = HEIGHT MAP**     |
| t9       | s9      | Shadow mask / soft lighting | —                        |
| t10      | s10     | Environment map    | Cube or 2D env map                 |
| t11      | s11     | AO / subsurface    | —                                  |
| t12      | s12     | Alpha test projection | Projected UV alpha source       |
| t15      | (none)  | Dither/noise       | Indexed load by screen position    |

**Key:** The height map used for parallax is stored in **t2 alpha channel** (specular texture),
not in a dedicated texture slot. This matches FO4's engine convention. Our POM PS must sample
`t2.a` for the height field during ray-marching.

---

## PS_INPUT Struct — Semantic Mappings

Based on shader 2682 (rich permutation with full TBN world-space data):

```hlsl
struct PS_INPUT
{
    float4 pos          : SV_POSITION;   // v0  — screen xy used for dither noise index
    float3 tangent      : TEXCOORD0;     // v1  — tangent (tangent space)
    float3 bitangent    : TEXCOORD1;     // v2  — bitangent (tangent space)
    float3 normal       : TEXCOORD2;     // v3  — normal (tangent space)
    float4 texcoord3    : TEXCOORD3;     // v4  — .w = texcoord0.U (UV already VS-displaced)
    float4 texcoord4    : TEXCOORD4;     // v5  — .w = texcoord0.V (UV already VS-displaced)
    float4 vertexColor  : COLOR0;        // v6  — xyzw vertex color (rich), or .w only (mid)
    float2 parallaxData : TEXCOORD6;     // v7  — x = parallax depth / view distance factor
    float3 worldNormal  : TEXCOORD7;     // v8  — world-space normal (for TBN reconstruction)
    float3 worldTangent : TEXCOORD8;     // v9  — world-space tangent
    float3 worldBitangent : TEXCOORD9;   // v10 — world-space bitangent
    uint   isFrontFace  : SV_IsFrontFace; // v11 — face culling flag
};
```

**UV extraction:** The displaced UV is NOT in a standard float2 register. It is packed as:
```hlsl
float2 uv = float2(v4.w, v5.w);
```

This is the UV that reaches t0/t1/t2 sample calls. It is already offset by VS parallax.

**Shader 2624 (mid-complexity)** omits v7–v10 (no world-space TBN) and v11 front face; its
input uses only v0–v7 with the same UV packing convention.

---

## PS_OUTPUT — GBuffer Layout (5 Render Targets)

```hlsl
struct PS_OUTPUT
{
    float4 albedo    : SV_Target0;  // o0 — rgb=diffuse*tint, a=alpha
    float4 normals   : SV_Target1;  // o1 — xy=encoded normal, z=-normal.z, w=alpha
    float4 material  : SV_Target2;  // o2 — x=hasFeature, y=specPower*0.003922,
                                    //       z=sqrt(roughness*0.02), w=specPower_sat
    float4 specular  : SV_Target3;  // o3 — x=specColor.r*specular,
                                    //       y=specColor.g*smoothness,
                                    //       z=glossiness*0.01, w=0.019608 (constant)
    float4 emissive  : SV_Target4;  // o4 — xyz=emissiveColor (from cb2[1]), w=alpha
};
```

---

## Normal Encoding Formula (GBuffer o1)

FO4 uses a modified octahedral encoding for normals stored in o1.xy:

```hlsl
// Input: float3 N (tangent-space normal, unit length)
// Output written to o1

o1.z = -N.z;

float normalLen = sqrt(N.z * -8.0 + 8.0);   // = sqrt(8 * (1 - N.z))

o1.x = N.x / normalLen + 0.5;
o1.y = N.y / normalLen + 0.5;
```

**Decode (for reference):**
```hlsl
float2 encoded = o1.xy * 2.0 - 1.0;           // remap [0,1] -> [-1,1]
float nz = -o1.z;
float scale = sqrt(8.0 * (1.0 - nz));
float3 N = float3(encoded * scale, nz);
N = normalize(N);
```

---

## Alpha Test Logic

Alpha testing uses two CB2 values checked at the top of the PS:

```hlsl
// cb2[2].y == 1.0 means alpha test is enabled
if (cb2[2].y == 1.0)
{
    float alpha = diffuseTexture.Sample(s0, uv).a;
    // Discard if: cb2[2].x * alpha - 0.015686 < 0
    // i.e. alpha < 0.015686 / cb2[2].x
    clip(cb2[2].x * alpha - 0.015686);
}
```

Where `cb2[2].x` is the alpha threshold scalar and `0.015686 ≈ 4/255` is a small bias.

---

## Dithering / Temporal Clipping (Rich Shader 2682 Only)

Uses `t15`, a noise/dither texture, indexed by integer screen position:

```hlsl
// Controlled by cb2[9]
int2 screenPos = (int2)v0.xy;
float dither = t15.Load(int3(screenPos & 3, 0)).x;   // 4x4 tiled noise

float ditherThresh = cb2[9].y;
float ditherScale  = cb2[9].z;
// clip if dither value below threshold
clip(dither * ditherScale - ditherThresh);
```

---

## CB2 — PerMaterial Layout (b2)

Used by both shader 2682 and 2624. Indices beyond [7] only present in rich permutation.

| Index   | xyzw components                                                             |
|---------|-----------------------------------------------------------------------------|
| cb2[0]  | x=specularColor.r scale, y=specularColor.g scale, z=?, w=glossiness         |
| cb2[1]  | xyz=emissiveColor, w=?                                                      |
| cb2[2]  | x=alphaThreshold, y=useAlphaTest (1.0 or 0.0), zw=?                        |
| cb2[3]  | xyz=tintColor, w=tintBlend factor                                           |
| cb2[4]  | xy=envmap scale, zw=?                                                       |
| cb2[5]  | (not accessed in simple permutation)                                        |
| cb2[6]  | x=featureFlag, y=featureFlag2, z=smoothness, w=softLightingFade             |
| cb2[7]  | ParallaxOcc / lighting effect params (used by VS for height scale)          |
| cb2[8]  | xyz=directional light direction, w=?  *(rich permutation only)*             |
| cb2[9]  | x=alphaRef, y=ditherThreshold, z=ditherScale, w=projectedUVThreshold *(rich only)* |
| cb2[10] | x=roughness, y=hasRoughness flag, z=roughMin, w=roughMax  *(rich only)*    |

---

## CB12 — PerGeometry Layout (b12)

32 float4 slots (indices 0–31). Only selected indices confirmed from disassembly:

| Index     | Usage observed                                       |
|-----------|------------------------------------------------------|
| cb12[30]  | x = shadow / interpolation factor                    |
| others    | Geometry transform data (world matrix, etc.) — not fully mapped in PS |

---

## Summary of Key Confirmed Facts

1. **CB register layout:** Game uses `b2` (PerMaterial) and `b12` (PerGeometry). Our CB at `b5` is safe.
2. **UV packing:** Displaced UVs arrive in `v4.w` (U) and `v5.w` (V), NOT as a standard float2.
3. **Height map location:** `t2` alpha channel (specular texture), sampler `s2`.
4. **No vanilla parallax:** FO4's compiled shaders have no parallax displacement. UVs are just texture-transformed.
5. **Normal encoding:** Modified octahedral, packed into o1.xy with z stored as `-N.z` in o1.z.
6. **Alpha test:** Enabled via `cb2[2].y == 1.0`, threshold from `cb2[2].x`.
7. **GBuffer:** 5 render targets (albedo, normals, material, specular, emissive).
8. **b5 confirmed unused** across all disassembled FXP PS permutations.

---

## POM Implementation Approach

Since FO4 has NO vanilla parallax in the compiled shaders, our approach is straightforward:

1. Replace the PS for `PARALLAX_OCCLUSION_MAPPING` permutations
2. Read UVs from v4.w/v5.w (clean, texture-transformed coordinates)
3. Before any texture sampling, run POM ray-march against t2.a (height map)
4. Use displaced UVs for all subsequent texture reads (t0, t1, t2)
5. Write to all 5 GBuffer render targets matching vanilla output format

The tangent-space basis from v1/v2/v3 (tangent, bitangent, normal) constructs the view
vector in tangent space, which drives the POM ray direction. World-space TBN (v8/v9/v10)
is available in rich permutations for additional normal mapping quality.

No VS changes needed. PS-only replacement.
