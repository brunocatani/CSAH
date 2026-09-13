#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::dlaa
{
    enum class Mode : std::uint32_t
    {
        dlaa = 0,
        dlssQuality = 1,
        dlssBalanced = 2,
        dlssPerformance = 3,
        dlssUltraPerformance = 4,
        centerDlaa = 5,
    };

    enum class ModelPreset : std::uint32_t
    {
        automatic = 0,
        reducedGhostingJ = 1,
        qualityK = 2,
        stableL = 3,
        performanceM = 4,
    };

    struct CenterRegion
    {
        std::uint32_t left{};
        std::uint32_t top{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct Settings
    {
        bool enabled{ true };
        Mode mode{ Mode::dlaa };
        ModelPreset modelPreset{ ModelPreset::qualityK };
        bool motionVectorRepair{ true };
        bool sharpening{};
        float sharpness{ 0.25f };
        float centerWidth{ 0.55f };
        float centerHeight{ 0.55f };
        float centerFeatherPixels{ 48.0f };
        bool visualizeCenter{};
        bool hardResetOnLoad{ true };
        bool verboseDiagnostics{};

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline bool isDlssMode(Mode mode) noexcept
    {
        return mode == Mode::dlssQuality || mode == Mode::dlssBalanced ||
            mode == Mode::dlssPerformance ||
            mode == Mode::dlssUltraPerformance;
    }

    [[nodiscard]] inline std::uint32_t computeJitterPhaseCount(
        std::uint32_t renderWidth,
        std::uint32_t displayWidth) noexcept
    {
        if (renderWidth == 0 || displayWidth == 0) {
            return 1;
        }
        const auto scale = static_cast<double>(displayWidth) /
            static_cast<double>(renderWidth);
        const auto phases = static_cast<std::uint32_t>(std::lround(
            8.0 * scale * scale));
        return (std::max)(1u, phases);
    }

    [[nodiscard]] inline bool isEvaluationViewportValid(
        Mode mode,
        std::uint32_t viewportCount,
        float left,
        float top,
        float width,
        float height,
        std::uint32_t displayPackedWidth,
        std::uint32_t displayHeight,
        float renderWidthScale,
        float renderHeightScale,
        bool renderScaleOwned,
        bool dynamicRegionClosed) noexcept
    {
        if (viewportCount != 1 || left != 0.0f || top != 0.0f ||
            displayPackedWidth == 0 || displayHeight == 0 ||
            !std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(renderWidthScale) ||
            !std::isfinite(renderHeightScale)) {
            return false;
        }
        constexpr float tolerance = 1.0f;
        const auto close = [=](float observed, float expected) noexcept {
            return std::abs(observed - expected) <= tolerance;
        };
        const auto renderWidth = static_cast<float>(displayPackedWidth) *
            renderWidthScale;
        const auto renderHeight = static_cast<float>(displayHeight) *
            renderHeightScale;
        if (close(width, renderWidth) && close(height, renderHeight)) {
            return true;
        }

        // FO4VR closes its dynamic-resolution region immediately before the
        // verified post-image-space callback. At that boundary D3D reports
        // the restored display viewport even though the scene, depth, and
        // motion resources still contain the packed render-resolution inputs
        // consumed by DLSS. Accept that state only for a DLSS mode and only
        // when the caller independently proves both scale ownership and the
        // closed FO4VR region flag.
        return isDlssMode(mode) && renderScaleOwned && dynamicRegionClosed &&
            close(width, static_cast<float>(displayPackedWidth)) &&
            close(height, static_cast<float>(displayHeight));
    }

    [[nodiscard]] inline const char* modeName(Mode mode) noexcept
    {
        switch (mode) {
        case Mode::dlaa:
            return "DLAA";
        case Mode::dlssQuality:
            return "DLSS Quality";
        case Mode::dlssBalanced:
            return "DLSS Balanced";
        case Mode::dlssPerformance:
            return "DLSS Performance";
        case Mode::dlssUltraPerformance:
            return "DLSS Ultra Performance";
        case Mode::centerDlaa:
            return "Center DLAA + TAA Periphery";
        default:
            return "DLAA";
        }
    }

    [[nodiscard]] inline CenterRegion computeCenterRegion(
        std::uint32_t eyeWidth,
        std::uint32_t height,
        float coverageWidth,
        float coverageHeight) noexcept
    {
        if (eyeWidth == 0 || height == 0) {
            return {};
        }
        constexpr std::uint32_t alignment = 8;
        const auto aligned = [](std::uint32_t value, std::uint32_t maximum) {
            value = (std::max)(alignment, value);
            value = (std::min)(value, maximum);
            if (value < maximum) {
                value -= value % alignment;
            }
            return (std::max)(1u, value);
        };
        const auto safeWidth = std::isfinite(coverageWidth) ?
            std::clamp(coverageWidth, 0.25f, 1.0f) : 0.55f;
        const auto safeHeight = std::isfinite(coverageHeight) ?
            std::clamp(coverageHeight, 0.25f, 1.0f) : 0.55f;
        const auto width = aligned(
            static_cast<std::uint32_t>(
                static_cast<float>(eyeWidth) * safeWidth),
            eyeWidth);
        const auto regionHeight = aligned(
            static_cast<std::uint32_t>(
                static_cast<float>(height) * safeHeight),
            height);
        return {
            .left = (eyeWidth - width) / 2u,
            .top = (height - regionHeight) / 2u,
            .width = width,
            .height = regionHeight,
        };
    }

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.sharpness = std::isfinite(result.sharpness) ?
            std::clamp(result.sharpness, 0.0f, 1.0f) : 0.25f;
        result.centerWidth = std::isfinite(result.centerWidth) ?
            std::clamp(result.centerWidth, 0.25f, 1.0f) : 0.55f;
        result.centerHeight = std::isfinite(result.centerHeight) ?
            std::clamp(result.centerHeight, 0.25f, 1.0f) : 0.55f;
        result.centerFeatherPixels =
            std::isfinite(result.centerFeatherPixels) ?
                std::clamp(result.centerFeatherPixels, 0.0f, 128.0f) :
                48.0f;
        if (result.mode != Mode::dlaa &&
            result.mode != Mode::dlssQuality &&
            result.mode != Mode::dlssBalanced &&
            result.mode != Mode::dlssPerformance &&
            result.mode != Mode::dlssUltraPerformance &&
            result.mode != Mode::centerDlaa) {
            result.mode = Mode::dlaa;
        }
        if (result.modelPreset != ModelPreset::automatic &&
            result.modelPreset != ModelPreset::reducedGhostingJ &&
            result.modelPreset != ModelPreset::qualityK &&
            result.modelPreset != ModelPreset::stableL &&
            result.modelPreset != ModelPreset::performanceM) {
            result.modelPreset = ModelPreset::qualityK;
        }
        return result;
    }
}
