#ifndef SHARED_DATA_HLSLI
#define SHARED_DATA_HLSLI

cbuffer SharedDataCB : register(b3) {
    float4 CS_DirLightDirection;
    float4 CS_DirLightColor;
    float4 CS_AmbientColor[6];
    float4 CS_FogParams;       // x=near, y=far, z=power, w=clamp
    float4 CS_WeatherData;     // x=transition, y=precipitation, z=wetness, w=windSpeed
    float  CS_Timer;
    uint   CS_FrameCount;
    uint   CS_IsInterior;
    uint   CS_IsVR;
    float  CS_Gamma;
    float3 CS_Padding;
};

#endif
