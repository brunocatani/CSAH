#include "BloomGlareCommon.hlsli"

RWTexture2D<float2> ApertureOutput : register(u0);

float ApertureTransmission(float2 position, float radius)
{
    const float radialDistance = length(position);
    if (ApertureMode() == 1u) {
        return 1.0 - smoothstep(radius - 2.0, radius + 2.0, radialDistance);
    }
    const uint bladeCount = max(ApertureBlades(), 3u);
    const float sector = 2.0 * Pi / bladeCount;
    const float angle = atan2(position.y, position.x) - GlareOptics.y;
    const float localAngle = frac(angle / sector) * sector - sector * 0.5;
    const float apothem = radius * cos(sector * 0.5);
    const float projected = radialDistance * cos(localAngle);
    return 1.0 - smoothstep(apothem - 2.0, apothem + 2.0, projected);
}

[numthreads(8, 8, 1)]
void CS_Aperture(uint2 tid : SV_DispatchThreadID)
{
    const uint resolution = FftResolution();
    if (tid.x >= resolution || tid.y >= resolution) {
        return;
    }
    const float2 center = resolution * 0.5;
    const float aspect = GlareScreen.x / max(GlareScreen.y, 1.0);
    const float radius = resolution * GlareOptics.w;
    float2 complexSum = 0.0;
    [unroll]
    for (uint sampleY = 0; sampleY < 2u; ++sampleY) {
        [unroll]
        for (uint sampleX = 0; sampleX < 2u; ++sampleX) {
            float2 position = float2(tid) +
                (float2(sampleX, sampleY) + 0.5) * 0.5 - center;
            position.y *= aspect;
            const float transmission = ApertureTransmission(position, radius);
            const float radiusSquared = dot(position, position);
            const float normalizedRadiusSquared =
                radiusSquared / max(radius * radius, 1.0);
            const float phase =
                GlareOptics.z * normalizedRadiusSquared +
                GlarePsf.x * normalizedRadiusSquared *
                    normalizedRadiusSquared;
            complexSum += transmission * float2(cos(phase), sin(phase));
        }
    }
    ApertureOutput[tid] = complexSum * 0.25;
}
