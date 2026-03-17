#ifndef COLOR_HLSLI
#define COLOR_HLSLI

// Exact sRGB curve (IEC 61966-2-1)
float3 SRGBToLinear(float3 color) {
    float3 lo = color / 12.92f;
    float3 hi = pow((color + 0.055f) / 1.055f, 2.4f);
    float3 s = step(0.04045f, color);
    return lerp(lo, hi, s);
}

float3 LinearToSRGB(float3 color) {
    float3 lo = color * 12.92f;
    float3 hi = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    float3 s = step(0.0031308f, color);
    return lerp(lo, hi, s);
}

// Fast approximation (pow 2.2)
float3 SRGBToLinearFast(float3 color) { return pow(max(color, 0.0f), 2.2f); }
float3 LinearToSRGBFast(float3 color) { return pow(max(color, 0.0f), 1.0f / 2.2f); }

#endif
