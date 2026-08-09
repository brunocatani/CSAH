cbuffer PerMaterial : register(b2)
{
    float4 cb2[7];
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[51];
};

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);

#ifndef LINEAR_LIGHTING_TEXTURED_EMISSION
#define LINEAR_LIGHTING_TEXTURED_EMISSION 0
#endif

#if LINEAR_LIGHTING_TEXTURED_EMISSION
Texture2D<float4> TexGlow : register(t3);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
#if LINEAR_LIGHTING_TEXTURED_EMISSION
SamplerState SampGlow : register(s3);
#endif

#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_VERTEX_ALPHA
#define LINEAR_LIGHTING_VERTEX_ALPHA LINEAR_LIGHTING_VERTEX_COLOR
#endif

#ifndef LINEAR_LIGHTING_ALPHA_TEST
#define LINEAR_LIGHTING_ALPHA_TEST 0
#endif

#ifndef LINEAR_LIGHTING_NORMAL_XY
#define LINEAR_LIGHTING_NORMAL_XY 1
#endif

#ifndef LINEAR_LIGHTING_FORCE_EARLY_DEPTH
#define LINEAR_LIGHTING_FORCE_EARLY_DEPTH 1
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 texCoord3 : TEXCOORD3;
    float4 texCoord4 : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
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

#if LINEAR_LIGHTING_FORCE_EARLY_DEPTH
[earlydepthstencil]
#endif
PSOutput PSMain(PSInput input)
{
    PSOutput output;

    float2 uv = float2(input.texCoord3.w, input.texCoord4.w);
    float4 diffuse = TexDiffuse.Sample(SampDiffuse, uv);
#if LINEAR_LIGHTING_VERTEX_COLOR
#if LINEAR_LIGHTING_VERTEX_ALPHA
    diffuse *= input.vertexColor;
#else
    diffuse.xyz *= input.vertexColor.xyz;
#endif
#endif
#if LINEAR_LIGHTING_ALPHA_TEST
    clip(diffuse.w - cb2[1].w);
#endif
    float alphaMask = (cb2[2].y == 1.0) ? diffuse.w : 1.0;
    float alpha = alphaMask * cb2[2].x;

    clip((cb2[2].x * alphaMask) - 0.015686);

    float fade = (cb2[5].w == -1.0) ? 1.0 : ((-cb2[5].w * cb12[50].x) + 1.0);
    output.target0.xyz = fade * LinearLightingDiffuse(diffuse.xyz);
    output.target0.w = alpha;

    float3 sourceNormal = normalize(input.normal);
#if LINEAR_LIGHTING_NORMAL_XY
    float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;
#else
    float2 normalSample = TexNormal.Sample(SampNormal, uv).zw;
#endif
    float2 specularSample = TexSpecular.Sample(SampSpecular, uv).xy;
    float2 tangentNormalXY = (normalSample * 2.0) - 1.0;
    float tangentNormalZ = sqrt(1.0 - min(dot(tangentNormalXY, tangentNormalXY), 1.0));
    float3 tangentNormal = float3(tangentNormalXY, input.isFrontFace ? tangentNormalZ : -tangentNormalZ);

    float3 projectedNormal;
    projectedNormal.z = min(dot(sourceNormal, tangentNormal), 0.0);
    projectedNormal.x = dot(normalize(input.tangent), tangentNormal);
    projectedNormal.y = dot(normalize(input.bitangent), tangentNormal);
    projectedNormal = normalize(projectedNormal);

    float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;
    output.target1.z = -projectedNormal.z;
    output.target1.w = alpha;

    float depthRange = cb2[6].w - cb2[6].z;
    float depthSwitch = (cb2[6].w < 0.0) ? 0.0 : cb12[50].x;
    float depthValue = (depthSwitch * depthRange) + cb2[6].z;
    float depthAlt = depthSwitch * cb2[6].w;
    depthValue = (cb2[6].y != 0.0) ? depthValue : depthAlt;
    output.target2.z = sqrt(depthValue * 0.02);

    bool materialFlag = (cb12[50].x != 0.0 && cb2[5].y != 0.0) || cb2[5].x != 0.0;
    output.target2.x = materialFlag ? 1.0 : 0.0;
    output.target2.y = cb2[6].x * 0.003922;
    output.target2.w = saturate(cb2[6].x);

    output.target3.w = alpha;
    output.target4.w = alpha;

    float specBlendA = cb12[50].x * cb2[5].z;
    float specBlendB = (-cb2[5].z * cb12[50].x) + 1.0;
    float specBlend = (specularSample.x * specBlendB) + specBlendA;

    float2 materialXY = ((cb2[3].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (cb2[3].xy >= 0.0) ? materialXY : cb2[0].xy;

    output.target3.y = materialXY.y * specBlend;
    output.target3.x = materialXY.x * specularSample.y;
    output.target3.z = cb2[0].w * 0.01;
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    float3 glow = LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz);
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
#endif

    return output;
}
