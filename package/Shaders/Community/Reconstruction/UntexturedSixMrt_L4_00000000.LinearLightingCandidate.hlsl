cbuffer PerMaterial : register(b2)
{
    float4 cb2[6];
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
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

[earlydepthstencil]
PSOutput PSMain(PSInput input)
{
    PSOutput output;

    float fade = (cb2[4].w == -1.0) ?
        1.0 : ((-cb2[4].w * cb12[50].x) + 1.0);
    output.target0.xyz = fade * LinearLightingDecodedDiffuse(float3(1.0, 1.0, 1.0));
    output.target0.w = cb2[0].z;
    output.target1.xy = asfloat(uint2(0x7FC00000u, 0x7FC00000u));

    float depthSwitch = (cb2[5].w < 0.0) ? 0.0 : cb12[50].x;
    float depthRange = cb2[5].w - cb2[5].z;
    float depthValue = (cb2[5].y != 0.0) ?
        ((depthSwitch * depthRange) + cb2[5].z) :
        (depthSwitch * cb2[5].w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 && cb2[4].y != 0.0) || cb2[4].x != 0.0) ?
        1.0 : 0.0;
    output.target2.y = cb2[5].x * 0.003922;
    output.target2.w = saturate(cb2[5].x);

    float2 materialXY =
        ((cb2[2].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (cb2[2].xy >= 0.0) ? materialXY : cb2[0].xy;
    output.target3.x = materialXY.x;
    output.target3.y = materialXY.y * cb12[50].x * cb2[4].z;
    output.target3.z = cb2[0].w * 0.01;
    output.target3.w = 1.0;
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);

    uint matrixBase = input.eyeIndex * 4u;
    float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    float currentW = dot(cb12[matrixBase + 66u], currentPosition);
    float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;

    float4 previousPosition = float4(input.previousPosition.xyz, 1.0);
    float previousW = dot(cb12[matrixBase + 54u], previousPosition);
    float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], previousPosition),
        dot(cb12[matrixBase + 52u], previousPosition)) / previousW;
    output.target5.xy = (currentNdc - previousNdc) * float2(-0.5, 0.5);

    return output;
}
