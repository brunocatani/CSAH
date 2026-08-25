#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace community_shaders::bloom_glare
{
    struct BloomSettings final
    {
        bool enabled{ true };
        float thresholdEV{ 3.0f };
        float intensity{ 0.035f };
        float radius{ 2.0f };

        [[nodiscard]] bool operator==(
            const BloomSettings&) const noexcept = default;
    };

    struct GlareSettings final
    {
        bool enabled{ true };
        float thresholdEV{ 6.0f };
        float intensity{ 0.20f };
        std::uint32_t fftResolution{ 256 };
        float paddingRatio{ 0.10f };
        std::uint32_t apertureMode{};
        std::uint32_t apertureBlades{ 6 };
        float apertureRotationDegrees{};
        float fStop{ 2.8f };
        float fresnelExponent{ 30.0f };
        float sphericalAberration{};
        float chromaticSpread{ 1.0f };
        float kernelScale{ 1.0f };
        float psfSharpness{ 0.45f };
        float psfNoiseFloor{ 0.001f };

        [[nodiscard]] bool operator==(
            const GlareSettings&) const noexcept = default;
    };

    struct Settings final
    {
        BloomSettings bloom{};
        GlareSettings glare{};

        [[nodiscard]] bool operator==(
            const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline float finiteOr(
        float value,
        float fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }

    [[nodiscard]] inline std::uint32_t sanitizeFftResolution(
        std::uint32_t value) noexcept
    {
        if (value <= 128) {
            return 128;
        }
        if (value <= 256) {
            return 256;
        }
        return 512;
    }

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        Settings result = settings;
        result.bloom.thresholdEV = std::clamp(
            finiteOr(result.bloom.thresholdEV, 3.0f), -7.0f, 16.0f);
        result.bloom.intensity = std::clamp(
            finiteOr(result.bloom.intensity, 0.035f), 0.0f, 1.0f);
        result.bloom.radius = std::clamp(
            finiteOr(result.bloom.radius, 2.0f), 1.0f, 5.0f);

        result.glare.thresholdEV = std::clamp(
            finiteOr(result.glare.thresholdEV, 6.0f), -7.0f, 16.0f);
        result.glare.intensity = std::clamp(
            finiteOr(result.glare.intensity, 0.20f), 0.0f, 2.0f);
        result.glare.fftResolution = sanitizeFftResolution(
            result.glare.fftResolution);
        result.glare.paddingRatio = std::clamp(
            finiteOr(result.glare.paddingRatio, 0.10f), 0.0f, 0.25f);
        result.glare.apertureMode = std::min(
            result.glare.apertureMode, 1u);
        result.glare.apertureBlades = std::clamp(
            result.glare.apertureBlades, 3u, 10u);
        result.glare.apertureRotationDegrees = std::clamp(
            finiteOr(result.glare.apertureRotationDegrees, 0.0f),
            -180.0f,
            180.0f);
        result.glare.fStop = std::clamp(
            finiteOr(result.glare.fStop, 2.8f), 1.0f, 22.0f);
        result.glare.fresnelExponent = std::clamp(
            finiteOr(result.glare.fresnelExponent, 30.0f), 0.0f, 80.0f);
        result.glare.sphericalAberration = std::clamp(
            finiteOr(result.glare.sphericalAberration, 0.0f),
            0.0f,
            100.0f);
        result.glare.chromaticSpread = std::clamp(
            finiteOr(result.glare.chromaticSpread, 1.0f), 0.0f, 3.0f);
        result.glare.kernelScale = std::clamp(
            finiteOr(result.glare.kernelScale, 1.0f), 0.01f, 1.0f);
        result.glare.psfSharpness = std::clamp(
            finiteOr(result.glare.psfSharpness, 0.45f), 0.20f, 1.0f);
        result.glare.psfNoiseFloor = std::clamp(
            finiteOr(result.glare.psfNoiseFloor, 0.001f), 0.0f, 0.01f);
        return result;
    }
}
