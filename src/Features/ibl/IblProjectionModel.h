#pragma once

#include "Features/ibl/IblProviderModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>

namespace csah::ibl
{
    enum class DiffuseSHState
    {
        invalid,
        black,
        usable,
    };

    enum class DiffusePublicationAction
    {
        publish,
        retain,
        clear,
    };

    [[nodiscard]] constexpr DiffusePublicationAction
        chooseDiffusePublicationAction(
            DiffuseSHState candidateState,
            bool publishedUsable,
            std::uint64_t publishedSession,
            std::uint64_t candidateSession) noexcept
    {
        if (candidateState == DiffuseSHState::usable &&
            candidateSession != 0) {
            return DiffusePublicationAction::publish;
        }
        if (candidateState == DiffuseSHState::invalid && publishedUsable &&
            candidateSession != 0 && publishedSession == candidateSession) {
            return DiffusePublicationAction::retain;
        }
        return DiffusePublicationAction::clear;
    }

    struct DiffuseSH
    {
        std::array<std::array<float, 4>, 3> rgb{};
    };

    static_assert(sizeof(DiffuseSH) == sizeof(float) * 12);

    struct DiffuseSHFit
    {
        std::array<std::array<double, 4>, 4> normal{};
        std::array<std::array<double, 4>, 3> radiance{};
        double observedSolidAngle{};
        double totalSolidAngle{};
    };

    [[nodiscard]] inline std::array<double, 4> evaluateFirstOrderSHBasis(
        const Float3& direction) noexcept
    {
        return {
            0.28209479177387814347,
            -0.48860251190291992159 * direction.y,
            0.48860251190291992159 * direction.z,
            -0.48860251190291992159 * direction.x,
        };
    }

    [[nodiscard]] inline double cubeTexelSolidAngleWeight(
        float horizontal,
        float vertical) noexcept
    {
        const auto denominator = 1.0 +
            static_cast<double>(horizontal) * horizontal +
            static_cast<double>(vertical) * vertical;
        return 1.0 / (denominator * std::sqrt(denominator));
    }

    inline void accumulateDiffuseSHFit(
        DiffuseSHFit& fit,
        const Float3& direction,
        const Float3& sample,
        float validity,
        double solidAngleWeight) noexcept
    {
        if (!std::isfinite(validity) || validity < 0.0f ||
            !std::isfinite(solidAngleWeight) || solidAngleWeight <= 0.0 ||
            !std::isfinite(sample.x) || !std::isfinite(sample.y) ||
            !std::isfinite(sample.z) || sample.x < 0.0f || sample.y < 0.0f ||
            sample.z < 0.0f) {
            return;
        }

        fit.totalSolidAngle += solidAngleWeight;
        const auto weight = static_cast<double>(std::clamp(validity, 0.0f, 1.0f)) *
            solidAngleWeight;
        if (!(weight > 0.0)) {
            return;
        }

        const auto basis = evaluateFirstOrderSHBasis(direction);
        for (std::size_t row = 0; row < basis.size(); ++row) {
            for (std::size_t column = 0; column < basis.size(); ++column) {
                fit.normal[row][column] +=
                    weight * basis[row] * basis[column];
            }
        }
        constexpr std::size_t kChannelCount = 3;
        const std::array<double, kChannelCount> channels{
            sample.x,
            sample.y,
            sample.z,
        };
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            for (std::size_t coefficient = 0; coefficient < basis.size();
                 ++coefficient) {
                fit.radiance[channel][coefficient] +=
                    weight * basis[coefficient] * channels[channel];
            }
        }
        fit.observedSolidAngle += weight;
    }

    [[nodiscard]] inline float diffuseSHFitCoverage(
        const DiffuseSHFit& fit) noexcept
    {
        if (!(fit.totalSolidAngle > 0.0) ||
            !std::isfinite(fit.totalSolidAngle) ||
            !std::isfinite(fit.observedSolidAngle)) {
            return 0.0f;
        }
        return static_cast<float>(std::clamp(
            fit.observedSolidAngle / fit.totalSolidAngle,
            0.0,
            1.0));
    }

    [[nodiscard]] inline bool solveDiffuseSHFit(
        const DiffuseSHFit& fit,
        DiffuseSH& coefficients) noexcept
    {
        // Solve the weighted first-order least-squares system with all three
        // colour channels as simultaneous right-hand sides. Partial pivoting
        // rejects directional coverage that cannot constrain every basis
        // coefficient instead of publishing unstable ambient light.
        std::array<std::array<double, 7>, 4> augmented{};
        double maximumMagnitude{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                augmented[row][column] = fit.normal[row][column];
                maximumMagnitude = std::max(
                    maximumMagnitude,
                    std::abs(augmented[row][column]));
            }
            for (std::size_t channel = 0; channel < 3; ++channel) {
                augmented[row][4 + channel] = fit.radiance[channel][row];
            }
        }
        if (!(maximumMagnitude > 0.0) || !std::isfinite(maximumMagnitude)) {
            return false;
        }
        const auto minimumPivot = maximumMagnitude * 1.0e-7;
        for (std::size_t pivotColumn = 0; pivotColumn < 4; ++pivotColumn) {
            auto pivotRow = pivotColumn;
            auto pivotMagnitude = std::abs(
                augmented[pivotRow][pivotColumn]);
            for (std::size_t row = pivotColumn + 1; row < 4; ++row) {
                const auto candidate = std::abs(
                    augmented[row][pivotColumn]);
                if (candidate > pivotMagnitude) {
                    pivotMagnitude = candidate;
                    pivotRow = row;
                }
            }
            if (!(pivotMagnitude > minimumPivot) ||
                !std::isfinite(pivotMagnitude)) {
                return false;
            }
            if (pivotRow != pivotColumn) {
                std::swap(augmented[pivotRow], augmented[pivotColumn]);
            }
            const auto inversePivot =
                1.0 / augmented[pivotColumn][pivotColumn];
            for (auto& value : augmented[pivotColumn]) {
                value *= inversePivot;
            }
            for (std::size_t row = 0; row < 4; ++row) {
                if (row == pivotColumn) {
                    continue;
                }
                const auto factor = augmented[row][pivotColumn];
                for (std::size_t column = 0; column < 7; ++column) {
                    augmented[row][column] -=
                        factor * augmented[pivotColumn][column];
                }
            }
        }

        DiffuseSH solved{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            for (std::size_t coefficient = 0; coefficient < 4;
                 ++coefficient) {
                const auto value = augmented[coefficient][4 + channel];
                if (!std::isfinite(value) ||
                    std::abs(value) >
                        static_cast<double>(std::numeric_limits<float>::max())) {
                    return false;
                }
                solved.rgb[channel][coefficient] = static_cast<float>(value);
            }
        }
        coefficients = solved;
        return true;
    }

    [[nodiscard]] inline Float3 evaluateDiffuseIrradiance(
        const DiffuseSH& coefficients,
        const Float3& direction) noexcept
    {
        constexpr double kL0Basis = 0.28209479177387814347;
        constexpr double kL1Basis = 0.48860251190291992159;
        constexpr double kL1CosineConvolution = 2.0 / 3.0;
        std::array<float, 3> output{};
        for (std::size_t channel = 0; channel < coefficients.rgb.size();
             ++channel) {
            const auto& value = coefficients.rgb[channel];
            const auto irradiance = value[0] * kL0Basis +
                value[1] * (-kL1Basis * direction.y) *
                    kL1CosineConvolution +
                value[2] * (kL1Basis * direction.z) *
                    kL1CosineConvolution +
                value[3] * (-kL1Basis * direction.x) *
                    kL1CosineConvolution;
            output[channel] = static_cast<float>(std::max(0.0, irradiance));
        }
        return { output[0], output[1], output[2] };
    }

    [[nodiscard]] inline bool buildDirectionalAmbientTransform(
        const DiffuseSH& coefficients,
        const std::array<float, kEnvironmentCubeFaceCount>&
            cubeFaceConfidence,
        const std::array<float, 16>& vanillaTransform,
        float shaderGamma,
        float level,
        std::array<float, 16>& result) noexcept
    {
        if (!std::isfinite(shaderGamma) || shaderGamma <= 0.0f ||
            !std::isfinite(level) || level < 0.0f ||
            !std::isfinite(vanillaTransform[15]) ||
            vanillaTransform[15] < 0.0f) {
            return false;
        }

        constexpr std::array<Float3, 6> directions{
            Float3{ -1.0f, 0.0f, 0.0f },
            Float3{ 1.0f, 0.0f, 0.0f },
            Float3{ 0.0f, -1.0f, 0.0f },
            Float3{ 0.0f, 1.0f, 0.0f },
            Float3{ 0.0f, 0.0f, -1.0f },
            Float3{ 0.0f, 0.0f, 1.0f },
        };
        constexpr std::array<EnvironmentCubeFace, directions.size()>
            directionFaces{
                EnvironmentCubeFace::negativeX,
                EnvironmentCubeFace::positiveX,
                EnvironmentCubeFace::negativeY,
                EnvironmentCubeFace::positiveY,
                EnvironmentCubeFace::negativeZ,
                EnvironmentCubeFace::positiveZ,
            };
        std::array<Float3, directions.size()> iblDirectional{};
        std::array<Float3, directions.size()> vanillaEncoded{};
        std::array<Float3, directions.size()> vanillaLinear{};
        std::array<float, directions.size()> confidence{};
        Float3 iblAverage{};
        Float3 vanillaAverage{};
        float confidenceTotal{};
        for (std::size_t index = 0; index < directions.size(); ++index) {
            const auto face = static_cast<std::size_t>(directionFaces[index]);
            if (!std::isfinite(cubeFaceConfidence[face])) {
                return false;
            }
            confidence[index] = std::clamp(
                cubeFaceConfidence[face],
                0.0f,
                1.0f);
            confidenceTotal += confidence[index];
            iblDirectional[index] = evaluateDiffuseIrradiance(
                coefficients,
                directions[index]);
            const auto& direction = directions[index];
            vanillaEncoded[index] = {
                vanillaTransform[0] * direction.x +
                    vanillaTransform[4] * direction.y +
                    vanillaTransform[8] * direction.z +
                    vanillaTransform[12],
                vanillaTransform[1] * direction.x +
                    vanillaTransform[5] * direction.y +
                    vanillaTransform[9] * direction.z +
                    vanillaTransform[13],
                vanillaTransform[2] * direction.x +
                    vanillaTransform[6] * direction.y +
                    vanillaTransform[10] * direction.z +
                    vanillaTransform[14],
            };
            vanillaLinear[index] = {
                std::pow(
                    std::max(0.0f, vanillaEncoded[index].x),
                    shaderGamma),
                std::pow(
                    std::max(0.0f, vanillaEncoded[index].y),
                    shaderGamma),
                std::pow(
                    std::max(0.0f, vanillaEncoded[index].z),
                    shaderGamma),
            };
            iblAverage.x += iblDirectional[index].x * confidence[index];
            iblAverage.y += iblDirectional[index].y * confidence[index];
            iblAverage.z += iblDirectional[index].z * confidence[index];
            vanillaAverage.x += vanillaLinear[index].x * confidence[index];
            vanillaAverage.y += vanillaLinear[index].y * confidence[index];
            vanillaAverage.z += vanillaLinear[index].z * confidence[index];
        }
        if (!(confidenceTotal > 1.0e-4f)) {
            return false;
        }
        const auto inverseConfidence = 1.0f / confidenceTotal;
        iblAverage.x *= inverseConfidence;
        iblAverage.y *= inverseConfidence;
        iblAverage.z *= inverseConfidence;
        vanillaAverage.x *= inverseConfidence;
        vanillaAverage.y *= inverseConfidence;
        vanillaAverage.z *= inverseConfidence;
        constexpr Float3 kLuminance{ 0.2126f, 0.7152f, 0.0722f };
        const auto iblLuminance = iblAverage.x * kLuminance.x +
            iblAverage.y * kLuminance.y + iblAverage.z * kLuminance.z;
        const auto vanillaLuminance = vanillaAverage.x * kLuminance.x +
            vanillaAverage.y * kLuminance.y +
            vanillaAverage.z * kLuminance.z;
        if (!std::isfinite(iblLuminance) || iblLuminance <= 1.0e-6f ||
            !std::isfinite(vanillaLuminance)) {
            return false;
        }
        const auto brightnessMatch = std::clamp(
            vanillaLuminance / iblLuminance,
            0.25f,
            4.0f) * level;
        const auto inverseGamma = 1.0f / shaderGamma;
        std::array<Float3, directions.size()> directional{};
        for (std::size_t index = 0; index < directional.size(); ++index) {
            const Float3 iblEncoded{
                std::pow(
                    std::max(
                        0.0f,
                        iblDirectional[index].x * brightnessMatch),
                    inverseGamma),
                std::pow(
                    std::max(
                        0.0f,
                        iblDirectional[index].y * brightnessMatch),
                    inverseGamma),
                std::pow(
                    std::max(
                        0.0f,
                        iblDirectional[index].z * brightnessMatch),
                    inverseGamma),
            };
            directional[index] = {
                vanillaEncoded[index].x +
                    (iblEncoded.x - vanillaEncoded[index].x) *
                        confidence[index],
                vanillaEncoded[index].y +
                    (iblEncoded.y - vanillaEncoded[index].y) *
                        confidence[index],
                vanillaEncoded[index].z +
                    (iblEncoded.z - vanillaEncoded[index].z) *
                        confidence[index],
            };
        }

        // DFLight exposes one affine first-order ambient transform rather than
        // a per-normal texture lookup. Reconstruct the least-squares transform
        // from six confidence-blended cardinal targets: unsupported targets
        // enter as vanilla, while supported targets enter as matched IBL.
        result = {};
        const auto writeAxis = [&result, &directional](
                                   std::size_t destination,
                                   std::size_t negative,
                                   std::size_t positive) {
            result[destination] =
                (directional[positive].x - directional[negative].x) * 0.5f;
            result[destination + 1] =
                (directional[positive].y - directional[negative].y) * 0.5f;
            result[destination + 2] =
                (directional[positive].z - directional[negative].z) * 0.5f;
        };
        writeAxis(0, 0, 1);
        writeAxis(4, 2, 3);
        writeAxis(8, 4, 5);
        for (const auto& sample : directional) {
            result[12] += sample.x / directional.size();
            result[13] += sample.y / directional.size();
            result[14] += sample.z / directional.size();
        }
        result[15] = 1.0f;
        return std::ranges::all_of(
            result,
            [](float value) { return std::isfinite(value); });
    }

    [[nodiscard]] inline bool validDiffuseSH(
        const DiffuseSH& coefficients) noexcept
    {
        constexpr float kMaximumMagnitude = 1.0e6f;
        for (const auto& channel : coefficients.rgb) {
            for (const auto coefficient : channel) {
                if (!std::isfinite(coefficient) ||
                    std::abs(coefficient) > kMaximumMagnitude) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] inline DiffuseSHState classifyDiffuseSH(
        const DiffuseSH& coefficients) noexcept
    {
        if (!validDiffuseSH(coefficients)) {
            return DiffuseSHState::invalid;
        }

        // The source texture is unsigned R11G11B10_FLOAT radiance. Its L0
        // terms therefore cannot be materially negative, and a first-order
        // directional term cannot exceed sqrt(3) times L0 for non-negative
        // radiance. A small margin covers finite sampling error.
        constexpr float kNumericalTolerance = 1.0e-5f;
        constexpr float kMaximumDirectionalRatio = 1.75f;
        constexpr float kMinimumUsableIntegratedRadiance = 1.0e-5f;
        float integratedRadiance{};
        for (const auto& channel : coefficients.rgb) {
            const auto l0 = channel[0];
            if (l0 < -kNumericalTolerance) {
                return DiffuseSHState::invalid;
            }
            const auto nonNegativeL0 = std::max(0.0f, l0);
            const auto directionalLimit = std::max(
                kNumericalTolerance,
                nonNegativeL0 * kMaximumDirectionalRatio);
            for (std::size_t coefficient = 1; coefficient < channel.size();
                 ++coefficient) {
                if (std::abs(channel[coefficient]) > directionalLimit) {
                    return DiffuseSHState::invalid;
                }
            }
            integratedRadiance += nonNegativeL0;
        }
        return integratedRadiance > kMinimumUsableIntegratedRadiance ?
            DiffuseSHState::usable :
            DiffuseSHState::black;
    }
}
