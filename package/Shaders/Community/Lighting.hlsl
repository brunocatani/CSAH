// Community Shaders at Home (CSAH) — BSLightingShader Pixel Shader
//
// Reconstructed from DXBC disassembly of vanilla VR shaders:
//   shader_2544_PS (DEFAULT technique, Layout #3, 6 MRTs)
//   shader_2546_PS (DEFAULT technique, Layout #4, 6 MRTs)
//
// Every instruction traced register-by-register from vanilla DXBC.
// CB offsets, texture registers, and MRT layout are Ghidra+DXBC verified.

// ============================================================================
// Constant Buffers (DXBC-verified)
// ============================================================================

// PerMaterial — CB2[0..6] (dcl_constantbuffer CB2[7], immediateIndexed)
cbuffer PerMaterial : register(b2) {
    float4 cb2_0;   // cb2[0] — SpecularColor (unused in DEFAULT technique)
    float4 cb2_1;   // cb2[1] — .xyz=EmissiveColor, .w=alphaTestThreshold
    float4 cb2_2;   // cb2[2] — unused in DEFAULT
    float4 cb2_3;   // cb2[3] — unused in DEFAULT
    float4 cb2_4;   // cb2[4] — unused in DEFAULT
    float4 cb2_5;   // cb2[5] — .x=featureFlag1, .y=featureFlag2, .w=fadeFactor (-1=none)
    float4 cb2_6;   // cb2[6] — .x=specPower, .y=hasRoughness, .z=roughMin, .w=roughMax
};

// PerGeometry — CB12[0..70] (dcl_constantbuffer CB12[71], dynamicIndexed)
cbuffer PerGeometry : register(b12) {
    float4 cb12[71];
    // cb12[50].x = shadow/interpolation factor
    // cb12[51-54] = VR eye 0 reprojection matrix (TEXCOORD4 projection)
    // cb12[55-58] = VR eye 1 reprojection matrix (TEXCOORD4 projection)
    // cb12[63-66] = VR eye 0 projection matrix (TEXCOORD3 projection)
    // cb12[67-70] = VR eye 1 projection matrix (TEXCOORD3 projection)
};

// ============================================================================
// Textures and Samplers (DXBC: t0/s0, t1/s1, t3/s3)
// ============================================================================
Texture2D<float4> TexDiffuse        : register(t0);  // _d.dds
Texture2D<float4> TexNormal         : register(t1);  // _n.dds (uses .zw for normal XY)
Texture2D<float4> TexSecondaryNorm  : register(t3);  // secondary normal/detail map

SamplerState SampDiffuse    : register(s0);
SamplerState SampNormal     : register(s1);
SamplerState SampSecondary  : register(s3);

// ============================================================================
// Input / Output Structures
// ============================================================================

// Layout #3: TEXCOORD0-4, COLOR0, EYEINDEX, SV_IsFrontFace
// Layout #4: Same but no COLOR0
struct PS_INPUT {
    float4 Position    : SV_POSITION;     // v0
    float3 Tangent     : TEXCOORD0;       // v1
    float3 Bitangent   : TEXCOORD1;       // v2
    float3 Normal      : TEXCOORD2;       // v3
    float4 TexCoord3   : TEXCOORD3;       // v4 — .xyz=position data, .w=UV.x
    float4 TexCoord4   : TEXCOORD4;       // v5 — .xyz=position data, .w=UV.y
    float4 VertexColor : COLOR0;          // v6 — .xyz only (Layout #4: zero-filled)
    uint   EyeIndex    : EYEINDEX;        // v7
    bool   IsFrontFace : SV_IsFrontFace;  // v8
};

// 6 MRTs (DXBC OSGN verified)
struct PS_OUTPUT {
    float4 Albedo       : SV_Target0;  // o0: .xyz=faded diffuse*vc, .w=0
    float2 NormalEnc    : SV_Target1;  // o1: .xy=octahedral encoded normal (2 channels ONLY)
    float4 Material     : SV_Target2;  // o2: .x=featureFlag, .y=specPow/255, .z=sqrt(rough*0.02), .w=sat(specPow)
    float4 SecNormal    : SV_Target3;  // o3: .xyz=TBN-transformed secondary normal, .w=1/255
    float3 Emissive     : SV_Target4;  // o4: .xyz=emissive color
    float2 MotionVector : SV_Target5;  // o5: .xy=VR stereo motion vector
};

// Community Shaders SharedData at b3 (bound by State::BindSharedData)
cbuffer SharedData : register(b3) {
    float4 CS_DirLightDirection;    // [0]
    float4 CS_DirLightColor;        // [1]
    float4 CS_AmbientColor[6];      // [2-7]
    float4 CS_FogParams;            // [8]
    float4 CS_WeatherData;          // [9]
    float  CS_Timer;                // [10].x
    uint   CS_FrameCount;           // [10].y
    uint   CS_IsInterior;           // [10].z
    uint   CS_IsVR;                 // [10].w
    float  CS_Gamma;                // [11].x
    float  CS_DebugMRTMode;         // [11].y — 0=off, 1-6=isolate MRT
    float2 CS_Pad;                  // [11].zw
};

// VS stub (vanilla VS is kept)
struct VS_INPUT  { float4 Position : POSITION; };
struct VS_OUTPUT { float4 Position : SV_POSITION; };
VS_OUTPUT VSMain(VS_INPUT input) { VS_OUTPUT o; o.Position = input.Position; return o; }

// ============================================================================
// PSMain — 1:1 reconstruction of vanilla DEFAULT technique GBuffer PS
//
// Source: shader_2544_PS_disasm.asm (Layout #3) and
//         shader_2546_PS_disasm.asm (Layout #4)
// ============================================================================
[earlydepthstencil]
PS_OUTPUT PSMain(PS_INPUT input) {
    PS_OUTPUT output;

    // ---- UV extraction (DXBC lines 4-5) ----
    float2 uv = float2(input.TexCoord3.w, input.TexCoord4.w);

    // ---- Sample diffuse (DXBC line 6 / line 2) ----
    float4 diffuse = TexDiffuse.Sample(SampDiffuse, uv);

    // ---- Alpha test (Layout #4, DXBC lines 3-5 of shader 2546) ----
    // cb2[1].w is alpha test threshold. If diffuse.a < threshold, discard.
    clip(diffuse.a - cb2_1.w);

    // ---- Fade factor (DXBC lines 1-3) ----
    // If cb2[5].w == -1.0 (sentinel), no fade. Otherwise fade = 1 - cb2[5].w * shadow.
    float shadow = cb12[50].x;
    float fadeFactor = (cb2_5.w == -1.0) ? 1.0 : (1.0 - cb2_5.w * shadow);

    // ---- Vertex color (Layout #3 uses v6.xyz, Layout #4 has no COLOR0) ----
    float3 vcRGB = input.VertexColor.xyz;
    // Layout #4: D3D fills missing COLOR0 with 0 → detect and use white
    if (dot(vcRGB, vcRGB) < 0.0001) vcRGB = float3(1, 1, 1);

    // ==================================================================
    // MRT0: Albedo (DXBC lines 7-8 / line 9)
    // ==================================================================
    output.Albedo.xyz = fadeFactor * diffuse.rgb * vcRGB;
    output.Albedo.w = 0;  // DXBC line 0: hardcoded 0

    // ==================================================================
    // Normal map sampling and TBN transform (DXBC lines 9-33)
    // ==================================================================

    // Normalize the interpolated normal (DXBC lines 9-11)
    float3 N = normalize(input.Normal);

    // Sample primary normal from t1 (DXBC line 12)
    // NOTE: t1.zwxy swizzle → normal.xy comes from texture .zw channels
    float4 normalSample = TexNormal.Sample(SampNormal, uv);
    float2 normalXY = normalSample.zw * 2.0 - 1.0;  // DXBC line 15: unpack .zw

    // Reconstruct Z (DXBC lines 16-19)
    float nDotN = dot(normalXY, normalXY);
    nDotN = min(nDotN, 1.0);
    float normalZ = sqrt(1.0 - nDotN);

    // Front-face flip (DXBC line 20)
    float3 tsNormal = float3(normalXY, input.IsFrontFace ? normalZ : -normalZ);

    // Compute world normal components (DXBC lines 21-22 for N, 23-30 for T and B)
    // The DXBC computes: wn.z = dot(N, tsNormal), clamped to ≤ 0
    //                    wn.x = dot(normalize(T), tsNormal)
    //                    wn.y = dot(normalize(B), tsNormal)
    float3 T = normalize(input.Tangent);   // DXBC lines 23-25
    float3 B = normalize(input.Bitangent); // DXBC lines 27-29

    float3 worldNormal;
    worldNormal.x = dot(T, tsNormal);         // DXBC line 26
    worldNormal.y = dot(B, tsNormal);         // DXBC line 30
    worldNormal.z = min(dot(N, tsNormal), 0); // DXBC lines 21-22: clamp Z ≤ 0

    worldNormal = normalize(worldNormal);     // DXBC lines 31-33

    // ==================================================================
    // MRT1: Octahedral normal encoding (DXBC lines 34-37)
    // Only writes .xy — 2 channels
    // ==================================================================
    float encFactor = sqrt(worldNormal.z * -8.0 + 8.0);  // DXBC line 34: z*-8+8 = 8*(1-z)
    output.NormalEnc = worldNormal.xy / encFactor + 0.5;  // DXBC lines 36-37

    // ==================================================================
    // MRT2: Material properties (DXBC lines 38-53)
    // ==================================================================

    // Roughness (DXBC lines 38-46)
    float roughRange = cb2_6.w - cb2_6.z;           // line 38: roughMax - roughMin
    float roughShadow = (cb2_6.w < 0.0) ? 0.0 : shadow;  // line 39-40
    float roughLerped = roughShadow * roughRange + cb2_6.z;  // line 41: lerp
    float roughDirect = roughShadow * cb2_6.w;       // line 42
    float roughness = (cb2_6.y != 0.0) ? roughLerped : roughDirect;  // line 43-44
    output.Material.z = sqrt(roughness * 0.02);      // lines 45-46

    // Feature flags (DXBC lines 47-51)
    bool shadowActive = (shadow != 0.0);              // line 47
    bool flag1 = (cb2_5.x != 0.0);                   // line 48
    bool flag2 = (cb2_5.y != 0.0);                   // line 48
    output.Material.x = (flag1 || (flag2 && shadowActive)) ? 1.0 : 0.0;  // lines 49-51

    // Spec power encoding (DXBC lines 52-53)
    output.Material.y = cb2_6.x * 0.003922;          // specPower / 255
    output.Material.w = saturate(cb2_6.x);            // clamped specPower

    // ==================================================================
    // MRT3: Secondary normal (DXBC lines 13-14, 54-60)
    // Sample t3, unpack to [-1,1], normalize, TBN transform
    // ==================================================================
    float3 secNorm = TexSecondaryNorm.Sample(SampSecondary, uv).xyz;
    secNorm = secNorm * 2.0 - 1.0;       // DXBC line 14: unpack
    secNorm = normalize(secNorm);         // DXBC lines 54-56

    output.SecNormal.x = dot(T, secNorm);             // DXBC line 57
    output.SecNormal.y = dot(B, secNorm);             // DXBC line 58
    output.SecNormal.z = dot(N, secNorm);             // DXBC line 59
    output.SecNormal.w = 0.003922;                    // DXBC line 60: 1/255

    // ==================================================================
    // MRT4: Emissive (DXBC line 61)
    // ==================================================================
    output.Emissive = cb2_1.xyz;

    // ==================================================================
    // MRT5: VR Motion Vectors (DXBC lines 62-76)
    // Projects TEXCOORD3.xyz and TEXCOORD4.xyz through VR eye-indexed
    // matrices to compute screen-space motion delta.
    // ==================================================================
    uint eyeOff = input.EyeIndex * 4;  // DXBC line 64: ishl v7.x, 2

    // Project TEXCOORD3.xyz through eye matrix at cb12[63 + eyeOff]
    float4 pos3 = float4(input.TexCoord3.xyz, 1.0);           // DXBC lines 62-63
    float2 proj3;
    proj3.x = dot(cb12[eyeOff + 63], pos3);                    // DXBC line 65
    proj3.y = dot(cb12[eyeOff + 64], pos3);                    // DXBC line 66
    float proj3w = dot(cb12[eyeOff + 66], pos3);               // DXBC line 67
    proj3 /= proj3w;                                            // DXBC line 68

    // Project TEXCOORD4.xyz through eye matrix at cb12[51 + eyeOff]
    float4 pos4 = float4(input.TexCoord4.xyz, 1.0);           // DXBC lines 69-70
    float2 proj4;
    proj4.x = dot(cb12[eyeOff + 51], pos4);                    // DXBC line 71
    proj4.y = dot(cb12[eyeOff + 52], pos4);                    // DXBC line 72
    float proj4w = dot(cb12[eyeOff + 54], pos4);               // DXBC line 73
    proj4 /= proj4w;                                            // DXBC line 74

    // Motion = (proj3 - proj4) * scale (DXBC lines 75-76)
    float2 motionDelta = proj3 - proj4;                         // DXBC line 75
    output.MotionVector = motionDelta * float2(-0.5, 0.5);     // DXBC line 76

    // ==================================================================
    // DEBUG: MRT isolation mode (CS_Debug.x)
    // Press F8 in-game to cycle: 0=off, 1=albedo, 2=normals,
    // 3=material, 4=secNormal, 5=emissive, 6=motionVec
    // When active, writes debug color to MRT0 and neutral to all others
    // ==================================================================
    int debugMode = (int)CS_DebugMRTMode;
    if (debugMode > 0) {
        float3 debugColor = float3(1, 0, 1); // magenta = invalid mode
        switch (debugMode) {
            case 1: debugColor = output.Albedo.xyz; break;           // show albedo
            case 2: debugColor = float3(output.NormalEnc, 0); break; // show encoded normals
            case 3: debugColor = output.Material.xyz; break;         // show material
            case 4: debugColor = output.SecNormal.xyz * 0.5 + 0.5; break; // show sec normal
            case 5: debugColor = output.Emissive; break;             // show emissive
            case 6: debugColor = float3(output.MotionVector * 0.5 + 0.5, 0); break; // show motion
        }
        output.Albedo = float4(debugColor, 0);
        output.NormalEnc = float2(0.5, 0.5);  // neutral up-facing normal
        output.Material = float4(0, 0, 0.1, 0);
        output.SecNormal = float4(0, 0, 1, 0.003922);
        output.Emissive = float3(0, 0, 0);
        output.MotionVector = float2(0, 0);
    }

    return output;
}
