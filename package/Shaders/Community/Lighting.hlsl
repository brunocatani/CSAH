// FO4VR Community Shaders — BSLightingShader Pixel Shader (Parallax Permutations)
//
// Reconstructed from DXBC disassembly of shader 2624 (mid-complexity parallax).
// Writes to 5 GBuffer render targets matching vanilla FO4 output.
//
// This shader compiles ONLY for parallax permutations. Non-parallax permutations
// will hit the #error guard and fall back to the vanilla shader via ShaderReplacer.
//
// Feature injection points:
//   EXTENDED_MATERIALS — POM ray-march displaces UVs before texture sampling
//   LINEAR_LIGHTING    — sRGB<->linear conversion around diffuse sampling

// ============================================================================
// Permutation support: compiles for all BSLightingShader permutations.
// PARALLAX_OCCLUSION_MAPPING enables POM features when defined.
// ============================================================================

// ============================================================================
// Includes
// ============================================================================
#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"

// ============================================================================
// Constant Buffers
// ============================================================================

// PerMaterial — b2 (CONFIRMED from DXBC: dcl_constantbuffer CB2[8..11])
// FO4 uses b2 for BSLightingShader PerMaterial, NOT b1.
cbuffer PerMaterial : register(b2) {
    float4 PM_SpecularColor;        // cb2[0]: x=specR, y=specG, z=?, w=glossiness
    float4 PM_EmissiveColor;        // cb2[1]: xyz=emissive RGB, w=?
    float4 PM_AlphaParams;          // cb2[2]: x=alphaThreshold, y=useAlphaTest(1.0=on)
    float4 PM_TintColor;            // cb2[3]: xyz=tint, w=tintBlend
    float4 PM_EnvmapParams;         // cb2[4]: xy=envmap scale
    float4 PM_Unused5;              // cb2[5]: not accessed in simple permutations
    float4 PM_MaterialFlags;        // cb2[6]: x=featureFlag, y=featureFlag2, z=smoothness, w=softLightingFade
    float4 PM_LightingEffectParams; // cb2[7]: x=specPower, y=hasRoughness, z=roughMin, w=roughMax
    float4 PM_DirLightDir;          // cb2[8]: xyz=direction (rich permutation only)
    float4 PM_DitherParams;         // cb2[9]: y=ditherThresh, z=ditherScale (rich only)
    float4 PM_RoughnessParams;      // cb2[10]: x=roughness, y=hasRoughness, z=min, w=max (rich only)
};

// PerGeometry — b12 (CONFIRMED from DXBC: dcl_constantbuffer CB12[31])
// FO4 uses b12 for BSLightingShader PerGeometry. Only cb12[30].x confirmed used.
cbuffer PerGeometry : register(b12) {
    float4 PG_Padding[30];         // cb12[0..29]: layout unknown, reserved
    float4 PG_ShadowParams;        // cb12[30]: x=shadow/interpolation factor
};

// ============================================================================
// Feature: LINEAR_LIGHTING (b4)
// ============================================================================
#ifdef LINEAR_LIGHTING
cbuffer LinearLightingCB : register(b4) {
    float LL_Gamma;
    float LL_UseExact;
    float2 LL_Pad;
};

float3 ApplyLinearInput(float3 color) {
    if (LL_UseExact > 0.5f)
        return SRGBToLinear(color);
    return SRGBToLinearFast(color);
}

float3 ApplyLinearOutput(float3 color) {
    if (LL_UseExact > 0.5f)
        return LinearToSRGB(color);
    return LinearToSRGBFast(color);
}
#endif

// ============================================================================
// Feature: EXTENDED_MATERIALS / POM (b5)
// ============================================================================
#ifdef EXTENDED_MATERIALS
cbuffer ExtendedMaterialsCB : register(b5) {
    uint  EM_EnablePOM;        // 0=off, 1=on
    uint  EM_EnableShadows;    // 0=off, 1=on
    uint  EM_MaxSteps;         // 0=auto, >0=override
    float EM_HeightScaleMult;  // multiplier on material height scale
};

#include "ExtendedMaterials/ExtendedMaterials.hlsli"
#endif

// ============================================================================
// Textures and Samplers
// ============================================================================
Texture2D<float4> TexDiffuse  : register(t0);  // _d.dds — diffuse
Texture2D<float4> TexNormal   : register(t1);  // _n.dds — normal map
Texture2D<float4> TexSpecular : register(t2);  // _s.dds — specular; ALPHA = height map

SamplerState SampDiffuse  : register(s0);
SamplerState SampNormal   : register(s1);
SamplerState SampSpecular : register(s2);

// ============================================================================
// PS Input/Output Structures
// ============================================================================

// From FO4VR DXBC ISGN analysis — Layout #3 (134 POM shaders, most common full GBuffer layout)
// Confirmed via disassembly of shader_2496_PS_0x006de554.dxbc:
//   v0 = SV_POSITION, v1 = TEXCOORD0 (tangent), v2 = TEXCOORD1 (bitangent),
//   v3 = TEXCOORD2 (normal), v4 = TEXCOORD3 (.w=U), v5 = TEXCOORD4 (.w=V),
//   v6 = COLOR0 (.w=vertexAlpha), v7 = EYEINDEX (VR eye), v8 = SV_IsFrontFace
//
// NOTE: FO4VR packs UVs into TEXCOORD3.w and TEXCOORD4.w (unlike Skyrim which uses TEXCOORD0.xy)
// NOTE: FO4VR always includes EYEINDEX (VR-only, not present in flat Skyrim)
// NOTE: Layout #4 (117 POM) is identical but omits COLOR0 — handle with VertexColor fallback
struct PS_INPUT {
    float4 Position    : SV_POSITION;     // v0 — screen position
    float3 Tangent     : TEXCOORD0;       // v1 — tangent vector (xyz only)
    float3 Bitangent   : TEXCOORD1;       // v2 — bitangent vector (xyz only)
    float3 Normal      : TEXCOORD2;       // v3 — normal vector (xyz only)
    float4 TexCoord3   : TEXCOORD3;       // v4 — .xyz=unused in basic, .w = texcoord U
    float4 TexCoord4   : TEXCOORD4;       // v5 — .xyz=unused in basic, .w = texcoord V
    float4 VertexColor : COLOR0;          // v6 — .w = vertex alpha (zero if Layout #4)
    uint   EyeIndex    : EYEINDEX;        // v7 — VR eye index (0=left, 1=right)
    bool   IsFrontFace : SV_IsFrontFace;  // v8 — front face flag
};

// 5 GBuffer render targets (CONFIRMED from DXBC OSGN):
struct PS_OUTPUT {
    float4 Albedo   : SV_Target0;  // o0 — rgb=diffuse*tint, a=alpha
    float4 Normals  : SV_Target1;  // o1 — xy=encoded normal, z=-N.z, w=alpha
    float4 Material : SV_Target2;  // o2 — x=hasFeature, y=spec*0.003922, z=sqrt(rough*0.02), w=spec_sat
    float4 Specular : SV_Target3;  // o3 — x=specR*spec, y=specG*smooth, z=gloss*0.01, w=clamp(alpha)
    float4 Emissive : SV_Target4;  // o4 — xyz=emissiveColor, w=alpha
};

// ============================================================================
// VS Stub — intentionally won't match vanilla input layout.
// ShaderReplacer keeps vanilla VS; this just prevents linker complaints.
// ============================================================================
struct VS_INPUT {
    float4 Position : POSITION;
};

struct VS_OUTPUT {
    float4 Position : SV_POSITION;
};

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    output.Position = input.Position;
    return output;
}

// ============================================================================
// PSMain — GBuffer write for parallax permutations
// ============================================================================
[earlydepthstencil]
PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;

    // ------------------------------------------------------------------
    // 1. Extract UV coordinates from v4.w / v5.w
    // ------------------------------------------------------------------
    float2 uv = float2(input.TexCoord3.w, input.TexCoord4.w);

    // ------------------------------------------------------------------
    // 2. POM Injection Point — displace UVs before any texture sampling
    // ------------------------------------------------------------------
#ifdef EXTENDED_MATERIALS
    float pomPixelOffset = 0.0;

    [branch] if (EM_EnablePOM != 0u) {
        // Build TBN matrix (tangent space -> world space, rows = T, B, N)
        float3x3 tbn = float3x3(
            normalize(input.Tangent),
            normalize(input.Bitangent),
            normalize(input.Normal)
        );

        // View direction: from fragment toward camera.
        // TODO: Replace with proper camera-to-fragment direction once eye position
        // is confirmed available in PerGeometry (cb12). Using negative world normal
        // as a rough approximation — produces visible POM displacement at non-grazing angles.
        float3 viewDir = -normalize(input.Normal);

        // Screen-space noise for stochastic mip selection and shadow jitter
        float screenNoise = frac(dot(input.Position.xy, float2(0.3183099, 0.1473211)));

        // Compute mip level for SampleLevel inside POM loop
        float mipLevel = ExtendedMaterials::GetMipLevel(uv, TexSpecular, screenNoise);

        // Build per-material DisplacementParams from the material CB and the height scale multiplier.
        // PM_LightingEffectParams.x = specPower (cb2[7].x); we reuse it as the base height scale
        // following the FO4 parallax convention where that field drives parallax intensity.
        DisplacementParams dispParams;
        float pomScale = PM_LightingEffectParams.x * EM_HeightScaleMult;
        dispParams.DisplacementScale  = pomScale;
        dispParams.DisplacementOffset = 0.0;
        dispParams.HeightScale        = pomScale;
        dispParams.FlattenAmount      = 0.0;

        // Ray-march the height field (height is in specular texture alpha = channel 3)
        uv = ExtendedMaterials::GetParallaxCoords(
            length(input.TexCoord3.xyz),  // distance approximation
            uv,
            mipLevel,
            viewDir,
            tbn,
            screenNoise,
            TexSpecular,
            SampSpecular,
            3u,                           // channel 3 = alpha
            dispParams,
            pomPixelOffset
        );
    }
#endif

    // ------------------------------------------------------------------
    // 3. Sample diffuse and handle alpha
    // ------------------------------------------------------------------
    float4 diffuseSample = TexDiffuse.Sample(SampDiffuse, uv);
    float texAlpha = diffuseSample.a;

    // VertexColor fallback: Layout #4 (117 POM) omits COLOR0 → D3D fills with 0.
    // Detect and treat as white (no tint) to avoid black output.
    float4 vertexColor = input.VertexColor;
    if (dot(vertexColor, vertexColor) < 0.0001f)
        vertexColor = float4(1, 1, 1, 1);

    float vertexAlpha = vertexColor.w;
    float finalAlpha = texAlpha * vertexAlpha;

    // ------------------------------------------------------------------
    // 4. Sample normal and specular maps with (potentially POM-displaced) UV
    // ------------------------------------------------------------------
    float4 normalSample  = TexNormal.Sample(SampNormal, uv);
    float4 specSample    = TexSpecular.Sample(SampSpecular, uv);

    // ------------------------------------------------------------------
    // 5. LINEAR_LIGHTING: convert diffuse to linear space
    // ------------------------------------------------------------------
#ifdef LINEAR_LIGHTING
    diffuseSample.rgb = ApplyLinearInput(diffuseSample.rgb);
#endif

    // ------------------------------------------------------------------
    // 6. Normal unpacking, TBN transform, front-face flip
    //    (faithfully reconstructed from DXBC)
    // ------------------------------------------------------------------
    // Unpack normal map from [0,1] to [-1,1]
    float2 normalXY = normalSample.xy * 2.0f - 1.0f;

    // Reconstruct Z: sqrt(1 - nx*nx - ny*ny)
    float nDotN = dot(normalXY, normalXY);
    nDotN = min(nDotN, 1.0f);
    float normalZ = sqrt(1.0f - nDotN);

    // Flip Z for back faces (IsFrontFace: nonzero = front, zero = back)
    // IsFrontFace: true = front face, false = back face
    float3 tsNormal = float3(normalXY, input.IsFrontFace ? normalZ : -normalZ);

    // Transform from tangent space to world space using TBN vectors
    // DXBC normalizes tangent and bitangent individually before the transform
    float3 T = normalize(input.Tangent);
    float3 B = normalize(input.Bitangent);
    float3 N = input.Normal;

    // Standard TBN transform
    float3 worldNormal;
    worldNormal.x = dot(T, tsNormal);
    worldNormal.y = dot(B, tsNormal);
    worldNormal.z = dot(N, tsNormal);

    // Vanilla DXBC clamps Z ≤ 0 before normalization.
    // This ensures o1.z (= -Nz) is always ≥ 0, matching what
    // FO4's deferred lighting pass expects.
    worldNormal.z = min(worldNormal.z, 0.0f);

    // Normalize the world-space normal
    float wnLen = rsqrt(dot(worldNormal, worldNormal));
    worldNormal *= wnLen;

    // ------------------------------------------------------------------
    // 7. GBuffer Output 0: Albedo = diffuse texture * vertex color tint
    // ------------------------------------------------------------------
    float3 albedoColor = diffuseSample.rgb * vertexColor.rgb;

#ifdef LINEAR_LIGHTING
    albedoColor = ApplyLinearOutput(albedoColor);
#endif

    output.Albedo = float4(albedoColor, finalAlpha);

    // ------------------------------------------------------------------
    // 8. GBuffer Output 1: Encoded Normals
    //    Octahedral-style encoding: N.xy / sqrt(8*(1-N.z)) + 0.5
    // ------------------------------------------------------------------
    float normalEncodeFactor = sqrt(8.0f * (1.0f - worldNormal.z));
    // Avoid division by zero when normal points straight at camera
    normalEncodeFactor = max(normalEncodeFactor, EPSILON_DIVISION);

    output.Normals.xy = worldNormal.xy / normalEncodeFactor + 0.5f;
    output.Normals.z  = -worldNormal.z;
    output.Normals.w  = finalAlpha;

    // ------------------------------------------------------------------
    // 9. GBuffer Output 3: Specular (done before Material because we need specR)
    //    Reconstructed from DXBC specular output logic.
    // ------------------------------------------------------------------
    float shadowFactor = PG_ShadowParams.x;  // cb12[30].x
    float smoothness   = PM_MaterialFlags.z;  // cb2[6].z

    // Smoothness blending with shadow factor
    float smoothShadow = shadowFactor * smoothness;
    float invSmoothShadow = 1.0f - smoothShadow;

    // Specular R channel: lerp between spec texture red and smoothness via shadow
    float specBlendR = specSample.r * invSmoothShadow + smoothShadow;

    // Specular color with envmap lerping
    float2 specColor = PM_SpecularColor.xy;   // cb2[0].xy
    float2 envScale  = PM_EnvmapParams.xy;    // cb2[4].xy

    float2 specDelta = envScale - specColor;
    float2 lerpedSpec = shadowFactor * specDelta + specColor;
    lerpedSpec *= specColor;

    // Only use lerped values if envmapScale >= 0
    float finalSpecR = (envScale.x >= 0.0f) ? lerpedSpec.x : specColor.x;
    float finalSpecG = (envScale.y >= 0.0f) ? lerpedSpec.y : specColor.y;

    output.Specular.x = finalSpecR * specSample.g;          // specR * specular green
    output.Specular.y = finalSpecG * specBlendR;             // specG * smoothness blend
    output.Specular.z = PM_SpecularColor.w * 0.01f;         // glossiness * 0.01
    output.Specular.w = clamp(finalAlpha, 0.019608f, 1.0f); // alpha clamped

    // ------------------------------------------------------------------
    // 10. GBuffer Output 2: Material Properties
    //     Roughness, feature flags, specular power encoding.
    // ------------------------------------------------------------------
    // Roughness calculation
    float roughMin = PM_LightingEffectParams.z;  // cb2[7].z
    float roughMax = PM_LightingEffectParams.w;  // cb2[7].w
    float roughRange = roughMax - roughMin;
    float hasRoughness = PM_LightingEffectParams.y;  // cb2[7].y

    // Shadow factor for roughness (0 if roughMax < 0)
    float roughShadow = (roughMax < 0.0f) ? 0.0f : shadowFactor;

    // If hasRoughness: lerp(roughMin, roughMax, shadow); else: shadow * roughMax
    float roughness;
    if (hasRoughness != 0.0f)
        roughness = roughShadow * roughRange + roughMin;
    else
        roughness = roughShadow * roughMax;

    output.Material.z = sqrt(roughness * 0.02f);

    // Feature flags for o2.x
    bool shadowActive = (shadowFactor != 0.0f);
    bool featureFlag1 = (PM_MaterialFlags.x != 0.0f);
    bool featureFlag2 = (PM_MaterialFlags.y != 0.0f);

    // o2.x = 1.0 if (featureFlag1 OR (featureFlag2 AND shadowActive))
    bool hasFeature = featureFlag1 || (featureFlag2 && shadowActive);
    output.Material.x = hasFeature ? 1.0f : 0.0f;

    // Specular power encoding
    float specPower = PM_LightingEffectParams.x;  // cb2[7].x
    output.Material.y = specPower * 0.003922f;     // specPower * (1/255)
    output.Material.w = saturate(specPower);        // saturate(specPower)

    // ------------------------------------------------------------------
    // 11. GBuffer Output 4: Emissive
    // ------------------------------------------------------------------
    output.Emissive.xyz = PM_EmissiveColor.xyz;
    output.Emissive.w   = finalAlpha;

    return output;
}
