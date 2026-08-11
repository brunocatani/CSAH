#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace community_shaders::ibl
{
    constexpr std::size_t kSceneProbeSampleCount = 8;

    struct SceneProbeCoordinate
    {
        std::uint32_t x{};
        std::uint32_t y{};
    };

    struct SceneProbeRgb
    {
        float red{};
        float green{};
        float blue{};
    };

    struct SceneProbeSummary
    {
        SceneProbeRgb average{};
        SceneProbeRgb leftEyeAverage{};
        SceneProbeRgb rightEyeAverage{};
        float peak{};
        std::uint32_t validSamples{};
        std::uint32_t nonBlackSamples{};
    };

    [[nodiscard]] constexpr std::array<
        SceneProbeCoordinate,
        kSceneProbeSampleCount>
    sceneProbeCoordinates(
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        std::array<SceneProbeCoordinate, kSceneProbeSampleCount> result{};
        if (width < 4 || height < 2) {
            return result;
        }

        const std::array<std::uint32_t, 4> xCoordinates{
            width / 8,
            (width * 3) / 8,
            (width * 5) / 8,
            (width * 7) / 8,
        };
        const std::array<std::uint32_t, 2> yCoordinates{
            height / 4,
            (height * 3) / 4,
        };
        std::size_t index{};
        for (const auto y : yCoordinates) {
            for (const auto x : xCoordinates) {
                result[index++] = {
                    std::min(x, width - 1),
                    std::min(y, height - 1),
                };
            }
        }
        return result;
    }

    [[nodiscard]] inline float decodeUnsignedFloat(
        std::uint32_t value,
        std::uint32_t mantissaBits) noexcept
    {
        const auto mantissaMask = (1u << mantissaBits) - 1u;
        const auto mantissa = value & mantissaMask;
        const auto exponent = (value >> mantissaBits) & 0x1Fu;
        if (exponent == 0) {
            return std::ldexp(
                static_cast<float>(mantissa),
                1 - 15 - static_cast<int>(mantissaBits));
        }
        if (exponent == 0x1Fu) {
            return mantissa == 0 ?
                std::numeric_limits<float>::infinity() :
                std::numeric_limits<float>::quiet_NaN();
        }
        return std::ldexp(
            1.0f +
                static_cast<float>(mantissa) /
                    static_cast<float>(1u << mantissaBits),
            static_cast<int>(exponent) - 15);
    }

    [[nodiscard]] inline SceneProbeRgb decodeR11G11B10Float(
        std::uint32_t packed) noexcept
    {
        return {
            decodeUnsignedFloat(packed & 0x7FFu, 6),
            decodeUnsignedFloat((packed >> 11) & 0x7FFu, 6),
            decodeUnsignedFloat((packed >> 22) & 0x3FFu, 5),
        };
    }

    [[nodiscard]] constexpr SceneProbeRgb decodeR8G8B8A8Unorm(
        std::uint32_t packed) noexcept
    {
        constexpr auto inverseByteMaximum = 1.0f / 255.0f;
        return {
            static_cast<float>(packed & 0xFFu) * inverseByteMaximum,
            static_cast<float>((packed >> 8) & 0xFFu) *
                inverseByteMaximum,
            static_cast<float>((packed >> 16) & 0xFFu) *
                inverseByteMaximum,
        };
    }

    [[nodiscard]] inline SceneProbeSummary summarizeSceneProbe(
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& samples)
        noexcept
    {
        SceneProbeSummary result{};
        std::uint32_t leftSamples{};
        std::uint32_t rightSamples{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto& sample = samples[index];
            if (!std::isfinite(sample.red) ||
                !std::isfinite(sample.green) ||
                !std::isfinite(sample.blue) ||
                sample.red < 0.0f || sample.green < 0.0f ||
                sample.blue < 0.0f) {
                continue;
            }
            result.average.red += sample.red;
            result.average.green += sample.green;
            result.average.blue += sample.blue;
            result.peak = std::max({
                result.peak,
                sample.red,
                sample.green,
                sample.blue,
            });
            if (std::max({ sample.red, sample.green, sample.blue }) >
                0.000001f) {
                ++result.nonBlackSamples;
            }

            const auto leftEye = (index % 4) < 2;
            auto& eyeAverage = leftEye ?
                result.leftEyeAverage :
                result.rightEyeAverage;
            eyeAverage.red += sample.red;
            eyeAverage.green += sample.green;
            eyeAverage.blue += sample.blue;
            if (leftEye) {
                ++leftSamples;
            } else {
                ++rightSamples;
            }
            ++result.validSamples;
        }

        const auto scale = [](SceneProbeRgb& value, std::uint32_t count) {
            if (count == 0) {
                return;
            }
            const auto inverse = 1.0f / static_cast<float>(count);
            value.red *= inverse;
            value.green *= inverse;
            value.blue *= inverse;
        };
        scale(result.average, result.validSamples);
        scale(result.leftEyeAverage, leftSamples);
        scale(result.rightEyeAverage, rightSamples);
        return result;
    }

    [[nodiscard]] inline float meanAbsoluteSceneProbeDifference(
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& left,
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& right)
        noexcept
    {
        float sum{};
        std::uint32_t componentCount{};
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto& lhs = left[index];
            const auto& rhs = right[index];
            if (!std::isfinite(lhs.red) || !std::isfinite(lhs.green) ||
                !std::isfinite(lhs.blue) || !std::isfinite(rhs.red) ||
                !std::isfinite(rhs.green) || !std::isfinite(rhs.blue)) {
                continue;
            }
            sum += std::abs(lhs.red - rhs.red);
            sum += std::abs(lhs.green - rhs.green);
            sum += std::abs(lhs.blue - rhs.blue);
            componentCount += 3;
        }
        return componentCount == 0 ?
            0.0f :
            sum / static_cast<float>(componentCount);
    }
}
