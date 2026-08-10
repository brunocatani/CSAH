#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_TESSELLATED_INPUTS
#define LINEAR_LIGHTING_TESSELLATED_INPUTS 1
#endif

#ifndef LINEAR_LIGHTING_DISMEMBERMENT
#define LINEAR_LIGHTING_DISMEMBERMENT 1
#endif

cbuffer PerMaterial : register(b2)
{
    float4 cb2[10];
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
};

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);
Texture2D<float4> TexDismembermentDiffuse : register(t9);
Texture2D<float4> TexDismembermentNormal : register(t10);
Texture2D<float4> TexDismembermentSpecular : register(t11);

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
SamplerState SampDismembermentDiffuse : register(s9);
SamplerState SampDismembermentNormal : register(s10);
SamplerState SampDismembermentSpecular : register(s11);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 dismembermentUv : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float2 dismembermentSelector : TEXCOORD5;
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
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

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    const bool useDismemberment = input.dismembermentSelector.y > 0.0;

    float2 specularSample;
    float3 projectedNormal;
    if (useDismemberment)
    {
        specularSample = TexDismembermentSpecular.Sample(
            SampDismembermentSpecular, input.dismembermentUv).xy;
        const float2 normalXY =
            (TexDismembermentNormal.Sample(
                SampDismembermentNormal, input.dismembermentUv).xy * 2.0) -
            1.0;
        const float normalZ = sqrt(1.0 - dot(normalXY, normalXY));
        const float3 tangentNormal = float3(normalXY, normalZ);
        projectedNormal = float3(
            dot(cb2[5].xyz, tangentNormal),
            dot(cb2[6].xyz, tangentNormal),
            min(dot(cb2[7].xyz, tangentNormal), 0.0));
    }
    else
    {
        specularSample = TexSpecular.Sample(SampSpecular, input.uv).xy;
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 modelNormal =
            (TexNormal.Sample(SampNormal, input.uv).xyz * 2.0) - 1.0;
        const float3 tangentNormal = float3(
            modelNormal.x,
            modelNormal.z,
            input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
        const float2 normalXY =
            (TexNormal.Sample(SampNormal, input.uv).xy * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        const float3 tangentNormal = float3(
            normalXY,
            input.isFrontFace ? normalZ : -normalZ);
#endif
        projectedNormal = float3(
            dot(normalize(input.tangent), tangentNormal),
            dot(normalize(input.bitangent), tangentNormal),
            min(dot(normalize(input.normal), tangentNormal), 0.0));
    }
    projectedNormal = normalize(projectedNormal);

    const float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    const float2 materialXYCandidate =
        (((cb2[2].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy) *
        cb2[0].xy;
    const float2 materialXY = (cb2[2].xy >= 0.0) ?
        materialXYCandidate : cb2[0].xy;
    const float specBlendA = cb12[50].x * cb2[4].z;
    const float specBlendB = (-cb2[4].z * cb12[50].x) + 1.0;
    const float specBlend =
        (specularSample.x * specBlendB) + specBlendA;
    output.target3.xy = materialXY * float2(
        specularSample.y,
        specBlend);

    const float depthSwitch = (cb2[9].w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange = cb2[9].w - cb2[9].z;
    const float depthValue = (cb2[9].y != 0.0) ?
        ((depthSwitch * depthRange) + cb2[9].z) :
        (depthSwitch * cb2[9].w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 && cb2[4].y != 0.0) || cb2[4].x != 0.0) ?
        1.0 : 0.0;
    output.target2.y = cb2[9].x * 0.003922;
    output.target2.w = saturate(cb2[9].x);

    float3 diffuse;
    if (useDismemberment)
    {
        diffuse = TexDismembermentDiffuse.Sample(
            SampDismembermentDiffuse, input.dismembermentUv).xyz;
    }
    else
    {
        diffuse = TexDiffuse.Sample(SampDiffuse, input.uv).xyz;
#if LINEAR_LIGHTING_VERTEX_COLOR
        diffuse *= input.vertexColor.xyz;
#endif
    }
    const float fade = (cb2[4].w == -1.0) ?
        1.0 : ((-cb2[4].w * cb12[50].x) + 1.0);
    output.target0.xyz = fade * LinearLightingDiffuse(diffuse);

    const uint matrixBase = input.eyeIndex * 4u;
    const float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    const float currentW = dot(cb12[matrixBase + 66u], currentPosition);
    const float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;
    const float previousW = dot(cb12[matrixBase + 54u], currentPosition);
    const float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], currentPosition),
        dot(cb12[matrixBase + 52u], currentPosition)) / previousW;
    output.target5.xy =
        (currentNdc - previousNdc) * float2(-0.5, 0.5);

    output.target0.w = cb2[0].z;
    output.target3.z = cb2[0].w * 0.01;
    output.target3.w = 1.0;
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
    return output;
}
