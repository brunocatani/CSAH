#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_MEAT_CUFF
#define LINEAR_LIGHTING_MEAT_CUFF 1
#endif

cbuffer PerMaterial : register(b2)
{
    float4 cb2[9];
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[51];
};

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);
Texture2D<float4> TexMeatCuffDiffuse : register(t9);
Texture2D<float4> TexMeatCuffNormal : register(t10);
Texture2D<float4> TexMeatCuffSpecular : register(t11);

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
SamplerState SampMeatCuffDiffuse : register(s9);
SamplerState SampMeatCuffNormal : register(s10);
SamplerState SampMeatCuffSpecular : register(s11);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float2 cuffIndex : TEXCOORD6;
    float3 cuffOrientation : TEXCOORD7;
    float3 cuffBasisX : TEXCOORD8;
    float3 cuffBasisY : TEXCOORD9;
    uint eyeIndex : EYEINDEX;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    const float3 surfaceTangent = normalize(input.tangent);
    const float3 surfaceBitangent = normalize(input.bitangent);
    const float3 surfaceNormal = normalize(input.normal);
    const bool invalidCuffIndex =
        input.cuffIndex.x < 0.0 || input.cuffIndex.x > 10.0;

    const float orientation =
        (dot(cb2[6].xyz, input.cuffOrientation) * 0.5) + 0.5;
    const bool positiveOrientation =
        dot(cb2[7].xyz, input.cuffOrientation) > 0.0;
    const float cuffNormalU = positiveOrientation ?
        orientation * 0.5 : 1.0 - (orientation * 0.5);
    const float2 cuffNormalUv = float2(
        cuffNormalU,
        input.cuffIndex.x * 0.1);
    const float2 cuffMaterialUv = 1.0 - cuffNormalUv;

    float4 diffuseSample = TexMeatCuffDiffuse.Sample(
        SampMeatCuffDiffuse, cuffMaterialUv);
    float2 specularSample = TexMeatCuffSpecular.Sample(
        SampMeatCuffSpecular, cuffMaterialUv).xy;
    const float2 cuffNormalXY =
        (TexMeatCuffNormal.Sample(
            SampMeatCuffNormal, cuffNormalUv).xy * 2.0) - 1.0;
    const float2 baseUv = float2(
        input.currentPosition.w,
        input.previousPosition.w);
    const float4 baseDiffuseSample = TexDiffuse.Sample(SampDiffuse, baseUv);
    const float3 baseNormalSample = TexNormal.Sample(SampNormal, baseUv).xyz;
    const float2 baseSpecularSample = TexSpecular.Sample(
        SampSpecular, baseUv).xy;

    float3 projectedNormal;
    if (invalidCuffIndex)
    {
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 modelNormal = (baseNormalSample * 2.0) - 1.0;
        const float3 tangentNormal = float3(
            modelNormal.x,
            modelNormal.z,
            input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
        const float2 normalXY = (baseNormalSample.xy * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        const float3 tangentNormal = float3(
            normalXY,
            input.isFrontFace ? normalZ : -normalZ);
#endif
        projectedNormal = float3(
            dot(surfaceTangent, tangentNormal),
            dot(surfaceBitangent, tangentNormal),
            dot(surfaceNormal, tangentNormal));
#if LINEAR_LIGHTING_VERTEX_COLOR
        diffuseSample = baseDiffuseSample * input.vertexColor;
#endif
        clip(-1.0);
#if !LINEAR_LIGHTING_VERTEX_COLOR
        diffuseSample = baseDiffuseSample;
#endif
        specularSample = baseSpecularSample;
    }
    else
    {
        const float cuffNormalZ = sqrt(
            1.0 - dot(cuffNormalXY, cuffNormalXY));
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 reconstructedNormal = normalize(
            (cuffNormalXY.y * input.cuffBasisY) +
            (cuffNormalXY.x * input.cuffBasisX) +
            (cuffNormalZ * input.cuffOrientation));
        projectedNormal = float3(
            dot(surfaceTangent, reconstructedNormal),
            dot(surfaceBitangent, reconstructedNormal),
            dot(surfaceNormal, reconstructedNormal));
#else
        const float3 tangentNormal = float3(cuffNormalXY, cuffNormalZ);
        projectedNormal = float3(
            dot(surfaceTangent, tangentNormal),
            dot(surfaceBitangent, tangentNormal),
            dot(surfaceNormal, tangentNormal));
#endif
    }

    const float2 materialXYCandidate =
        (((cb2[3].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy) *
        cb2[0].xy;
    const float2 materialXY = (cb2[3].xy >= 0.0) ?
        materialXYCandidate : cb2[0].xy;
    const float specBlendA = cb12[50].x * cb2[5].z;
    const float specBlendB = (-cb2[5].z * cb12[50].x) + 1.0;
    const float specBlend =
        (specularSample.x * specBlendB) + specBlendA;
    output.target3.x = materialXY.x * specularSample.y;
    output.target3.y = materialXY.y * specBlend;

    projectedNormal.z = min(projectedNormal.z, 0.0);
    projectedNormal = normalize(projectedNormal);
    output.target1.z = -projectedNormal.z;
    const float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    const float depthSwitch = (cb2[8].w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange = cb2[8].w - cb2[8].z;
    const float depthValue = (cb2[8].y != 0.0) ?
        ((depthSwitch * depthRange) + cb2[8].z) :
        (depthSwitch * cb2[8].w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 && cb2[5].y != 0.0) || cb2[5].x != 0.0) ?
        1.0 : 0.0;
    output.target2.y = cb2[8].x * 0.003922;
    output.target2.w = saturate(cb2[8].x);

    const float alphaSource = (cb2[2].y == 1.0) ? diffuseSample.w : 1.0;
    const float alpha = cb2[2].x * alphaSource;
    clip(alpha - 0.015686);
    const float fade = (cb2[5].w == -1.0) ?
        1.0 : ((-cb2[5].w * cb12[50].x) + 1.0);
    output.target0.xyz =
        fade * LinearLightingDiffuse(diffuseSample.xyz);
    output.target0.w = alpha;
    output.target1.w = alpha;
    output.target3.z = cb2[0].w * 0.01;
    output.target3.w = alpha;
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
    output.target4.w = alpha;
    return output;
}
