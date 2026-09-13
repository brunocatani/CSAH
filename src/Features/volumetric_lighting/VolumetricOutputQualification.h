#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace csah::volumetric_lighting
{
    struct OutputProbe final
    {
        float structured{};
        float filtered{};
        float depth{};
        float positiveContrast{};
    };

    struct OutputQualification final
    {
        bool passed{};
        std::uint32_t failureMask{};
        float structuredMean{};
        float structuredMaximum{};
        float filteredMean{};
        float positiveContrastMaximum{};
        float sunIntensity{};
        float glareMaximum{};
    };

    enum OutputQualificationFailure : std::uint32_t
    {
        OutputFailure_None = 0,
        OutputFailure_ProbeCount = 1u << 0u,
        OutputFailure_NonFinite = 1u << 1u,
        OutputFailure_ProbeRange = 1u << 2u,
        OutputFailure_LightRange = 1u << 3u,
    };

    [[nodiscard]] inline OutputQualification qualifyOutput(
        std::span<const OutputProbe> probes,
        float sunIntensity,
        float glareRed,
        float glareGreen,
        float glareBlue) noexcept
    {
        constexpr std::size_t expectedProbeCount = 128;
        constexpr float maximumIntegratedLighting = 1.25f;
        constexpr float maximumNativeLightValue = 65504.0f;
        OutputQualification output;
        output.sunIntensity = sunIntensity;
        output.glareMaximum = std::max({ glareRed, glareGreen, glareBlue });
        if (probes.size() != expectedProbeCount) {
            output.failureMask |= OutputFailure_ProbeCount;
        }
        if (!std::isfinite(sunIntensity) || !std::isfinite(glareRed) ||
            !std::isfinite(glareGreen) || !std::isfinite(glareBlue)) {
            output.failureMask |= OutputFailure_NonFinite;
        } else if (sunIntensity < 0.0f ||
                   sunIntensity > maximumNativeLightValue ||
                   glareRed < 0.0f || glareRed > maximumNativeLightValue ||
                   glareGreen < 0.0f || glareGreen > maximumNativeLightValue ||
                   glareBlue < 0.0f || glareBlue > maximumNativeLightValue) {
            output.failureMask |= OutputFailure_LightRange;
        }

        double structuredSum{};
        double filteredSum{};
        for (const auto& probe : probes) {
            if (!std::isfinite(probe.structured) ||
                !std::isfinite(probe.filtered) ||
                !std::isfinite(probe.depth) ||
                !std::isfinite(probe.positiveContrast)) {
                output.failureMask |= OutputFailure_NonFinite;
                continue;
            }
            if (probe.structured < 0.0f ||
                probe.structured > maximumIntegratedLighting ||
                probe.filtered < 0.0f ||
                probe.filtered > maximumIntegratedLighting ||
                probe.depth < 0.0f || probe.depth > 1.0001f ||
                probe.positiveContrast < 0.0f ||
                probe.positiveContrast > maximumIntegratedLighting) {
                output.failureMask |= OutputFailure_ProbeRange;
            }
            structuredSum += probe.structured;
            filteredSum += probe.filtered;
            output.structuredMaximum = std::max(
                output.structuredMaximum, probe.structured);
            output.positiveContrastMaximum = std::max(
                output.positiveContrastMaximum, probe.positiveContrast);
        }
        if (!probes.empty()) {
            output.structuredMean = static_cast<float>(
                structuredSum / static_cast<double>(probes.size()));
            output.filteredMean = static_cast<float>(
                filteredSum / static_cast<double>(probes.size()));
        }
        output.passed = output.failureMask == OutputFailure_None;
        return output;
    }
}
