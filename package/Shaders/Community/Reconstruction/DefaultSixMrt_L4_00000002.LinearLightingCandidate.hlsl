#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
#endif

cbuffer PerMaterial : register(b2)
{
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[7];
#else
    float4 cb2[6];
#endif
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
};

#ifndef LINEAR_LIGHTING_INSTANCED
#define LINEAR_LIGHTING_INSTANCED 0
#endif

#if LINEAR_LIGHTING_INSTANCED
cbuffer PerInstance : register(b13)
{
    float4 cb13[900];
};
#endif

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
Texture2D<float4> TexAdditionalAlpha : register(t12);
Texture2D<float4> TexAdditionalAlphaNoise : register(t15);
#endif

#ifndef LINEAR_LIGHTING_TEXTURED_EMISSION
#define LINEAR_LIGHTING_TEXTURED_EMISSION 0
#endif

#if LINEAR_LIGHTING_TEXTURED_EMISSION
Texture2D<float4> TexGlow : register(t3);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif
#if LINEAR_LIGHTING_TEXTURED_EMISSION
SamplerState SampGlow : register(s3);
#endif

#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_VERTEX_ALPHA
#define LINEAR_LIGHTING_VERTEX_ALPHA LINEAR_LIGHTING_VERTEX_COLOR
#endif

#ifndef LINEAR_LIGHTING_FORCE_EARLY_DEPTH
#define LINEAR_LIGHTING_FORCE_EARLY_DEPTH 1
#endif

#ifndef LINEAR_LIGHTING_ALPHA_TEST
#define LINEAR_LIGHTING_ALPHA_TEST 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_TESSELLATED_INPUTS
#define LINEAR_LIGHTING_TESSELLATED_INPUTS 0
#endif

#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]
#else
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[5]
#endif

struct PSInput
{
    float4 position : SV_POSITION;
#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float2 uv : TEXCOORD0;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
#else
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#endif
#if LINEAR_LIGHTING_INSTANCED
    uint instanceDataIndex : COLOR2;
#endif
    uint eyeIndex : EYEINDEX;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float2 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float3 target4 : SV_Target4;
    float2 target5 : SV_Target5;
};

#if LINEAR_LIGHTING_FORCE_EARLY_DEPTH
[earlydepthstencil]
#endif
PSOutput PSMain(PSInput input)
{
    PSOutput output;

#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float2 uv = input.uv;
#else
    float2 uv = float2(input.currentPosition.w, input.previousPosition.w);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    if (cb2[5].y != 0.0)
    {
        float2 coordinateSign = (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((cb2[5].y * (0.5 - noise)) + cb2[5].z - 0.5);
    }
    if (cb2[5].w != 0.0)
    {
        float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, uv).w;
        clip(cb2[5].x - additionalAlpha);
    }
#endif
#if LINEAR_LIGHTING_ALPHA_TEST
    float4 diffuseSample = TexDiffuse.Sample(SampDiffuse, uv);
    float alpha = diffuseSample.w;
#if LINEAR_LIGHTING_VERTEX_ALPHA
    alpha *= input.vertexColor.w;
#endif
    clip(alpha - cb2[1].w);
    float3 diffuse = diffuseSample.xyz;
#else
    float3 diffuse = TexDiffuse.Sample(SampDiffuse, uv).xyz;
#endif
#if LINEAR_LIGHTING_VERTEX_COLOR
    diffuse *= input.vertexColor.xyz;
#endif
    float fade = (cb2[4].w == -1.0) ? 1.0 : ((-cb2[4].w * cb12[50].x) + 1.0);
    output.target0.xyz = fade * LinearLightingDiffuse(diffuse);
    output.target0.w = cb2[0].z;

    float3 sourceNormal = normalize(input.normal);
    float2 specularSample = TexSpecular.Sample(SampSpecular, uv).xy;
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
    float3 modelNormal = (TexNormal.Sample(SampNormal, uv).xyz * 2.0) - 1.0;
    float3 tangentNormal = float3(
        modelNormal.x,
        modelNormal.z,
        input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
    float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;
    float2 tangentNormalXY = (normalSample * 2.0) - 1.0;
    float tangentNormalZ = sqrt(1.0 - min(dot(tangentNormalXY, tangentNormalXY), 1.0));
    float3 tangentNormal = float3(tangentNormalXY, input.isFrontFace ? tangentNormalZ : -tangentNormalZ);
#endif

    float3 projectedNormal;
    projectedNormal.z = min(dot(sourceNormal, tangentNormal), 0.0);
    projectedNormal.x = dot(normalize(input.tangent), tangentNormal);
    projectedNormal.y = dot(normalize(input.bitangent), tangentNormal);
    projectedNormal = normalize(projectedNormal);

    float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    float depthRange = LINEAR_LIGHTING_DEPTH_PARAMETERS.w -
        LINEAR_LIGHTING_DEPTH_PARAMETERS.z;
    float depthSwitch = (LINEAR_LIGHTING_DEPTH_PARAMETERS.w < 0.0) ?
        0.0 : cb12[50].x;
    float depthValue = (depthSwitch * depthRange) +
        LINEAR_LIGHTING_DEPTH_PARAMETERS.z;
    float depthAlt = depthSwitch * LINEAR_LIGHTING_DEPTH_PARAMETERS.w;
    depthValue = (LINEAR_LIGHTING_DEPTH_PARAMETERS.y != 0.0) ?
        depthValue : depthAlt;
    output.target2.z = sqrt(depthValue * 0.02);

    bool materialFlag = (cb12[50].x != 0.0 && cb2[4].y != 0.0) || cb2[4].x != 0.0;
    output.target2.x = materialFlag ? 1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_DEPTH_PARAMETERS.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_DEPTH_PARAMETERS.x);

    float specBlendA = cb12[50].x * cb2[4].z;
    float specBlendB = (-cb2[4].z * cb12[50].x) + 1.0;
    float specBlend = (specularSample.x * specBlendB) + specBlendA;

    float3 emitColor;
#if LINEAR_LIGHTING_INSTANCED
    uint instanceBase = input.instanceDataIndex * 6u;
    float4 instanceMaterial = cb13[instanceBase + 4u];
    output.target0.w = instanceMaterial.z;
    output.target3.x = instanceMaterial.x * specularSample.y;
    output.target3.y = instanceMaterial.y * specBlend;
    output.target3.z = instanceMaterial.w * 0.01;
    emitColor = cb13[instanceBase + 5u].xyz;
#else
    float2 materialXY = ((cb2[2].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (cb2[2].xy >= 0.0) ? materialXY : cb2[0].xy;

    output.target3.y = materialXY.y * specBlend;
    output.target3.x = materialXY.x * specularSample.y;
    output.target3.z = cb2[0].w * 0.01;
    emitColor = cb2[1].xyz;
#endif
    output.target3.w = 1.0;
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    float3 glow = LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz);
    output.target4.xyz = LinearLightingEmitColor(emitColor) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(emitColor);
#endif

    uint matrixBase = input.eyeIndex * 4u;
    float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    float currentW = dot(cb12[matrixBase + 66u], currentPosition);
    float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;

#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float4 previousPosition = currentPosition;
#else
    float4 previousPosition = float4(input.previousPosition.xyz, 1.0);
#endif
    float previousW = dot(cb12[matrixBase + 54u], previousPosition);
    float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], previousPosition),
        dot(cb12[matrixBase + 52u], previousPosition)) / previousW;

    output.target5.xy = (currentNdc - previousNdc) * float2(-0.5, 0.5);
    return output;
}
