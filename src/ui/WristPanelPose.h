#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace community_shaders::ui::wrist_panel_pose
{
    struct Config
    {
        float positionX{};
        float positionY{};
        float positionZ{};
        float rotationXDegrees{};
        float rotationYDegrees{};
        float rotationZDegrees{};
        bool flipX{};
        bool flipY{};
        bool flipZ{};
        // Precomputed from the Euler/flip fields so the ROCK frame callback
        // performs no trigonometry. Tests enforce that both representations
        // remain equivalent under the regular Prober composition contract.
        std::array<float, 4> localOrientation{
            0.0f, 0.0f, 0.0f, 1.0f };
    };

    inline constexpr Config kProberPanelPose{
        .positionX = 7.75f,
        .positionY = 9.0f,
        .positionZ = -16.5f,
        .rotationXDegrees = 0.0f,
        .rotationYDegrees = 90.0f,
        .rotationZDegrees = 6.0f,
        .flipX = true,
        .flipY = true,
        .flipZ = false,
        .localOrientation = {
            0.7061377159f,
            0.0370071096f,
            0.7061377159f,
            -0.0370071096f,
        },
    };

    [[nodiscard]] inline std::optional<std::array<float, 4>>
    normalizedQuaternion(std::array<float, 4> quaternion) noexcept
    {
        float normSquared{};
        for (const auto component : quaternion) {
            if (!std::isfinite(component)) {
                return std::nullopt;
            }
            normSquared += component * component;
        }
        if (!std::isfinite(normSquared) || normSquared < 1.0e-6f) {
            return std::nullopt;
        }
        const auto inverseNorm = 1.0f / std::sqrt(normSquared);
        for (auto& component : quaternion) {
            component *= inverseNorm;
        }
        return quaternion;
    }

    [[nodiscard]] inline std::optional<std::array<float, 4>>
    multiplyQuaternions(
        const std::array<float, 4>& left,
        const std::array<float, 4>& right) noexcept
    {
        return normalizedQuaternion({
            left[3] * right[0] + left[0] * right[3] +
                left[1] * right[2] - left[2] * right[1],
            left[3] * right[1] - left[0] * right[2] +
                left[1] * right[3] + left[2] * right[0],
            left[3] * right[2] + left[0] * right[1] -
                left[1] * right[0] + left[2] * right[3],
            left[3] * right[3] - left[0] * right[0] -
                left[1] * right[1] - left[2] * right[2],
        });
    }

    [[nodiscard]] inline std::optional<std::array<float, 4>>
    localOrientationFromEulerAndFlips(const Config& config) noexcept
    {
        if (!std::isfinite(config.rotationXDegrees) ||
            !std::isfinite(config.rotationYDegrees) ||
            !std::isfinite(config.rotationZDegrees)) {
            return std::nullopt;
        }
        constexpr float degreesToHalfRadians = 0.00872664625997164788f;
        const auto halfX = config.rotationXDegrees * degreesToHalfRadians;
        const auto halfY = config.rotationYDegrees * degreesToHalfRadians;
        const auto halfZ = config.rotationZDegrees * degreesToHalfRadians;
        const std::array<float, 4> xRotation{
            std::sin(halfX), 0.0f, 0.0f, std::cos(halfX) };
        const std::array<float, 4> yRotation{
            0.0f, std::sin(halfY), 0.0f, std::cos(halfY) };
        const std::array<float, 4> zRotation{
            0.0f, 0.0f, std::sin(halfZ), std::cos(halfZ) };

        // Column-vector convention: qZ*qY*qX applies local X, then Y, then Z.
        const auto yThenX = multiplyQuaternions(yRotation, xRotation);
        if (!yThenX) {
            return std::nullopt;
        }
        auto orientation = multiplyQuaternions(zRotation, *yThenX);
        if (!orientation) {
            return std::nullopt;
        }

        constexpr std::array<float, 4> flipX{ 1.0f, 0.0f, 0.0f, 0.0f };
        constexpr std::array<float, 4> flipY{ 0.0f, 1.0f, 0.0f, 0.0f };
        constexpr std::array<float, 4> flipZ{ 0.0f, 0.0f, 1.0f, 0.0f };
        for (const auto& [enabled, flip] :
            std::array{
                std::pair{ config.flipX, flipX },
                std::pair{ config.flipY, flipY },
                std::pair{ config.flipZ, flipZ },
            }) {
            if (enabled) {
                orientation = multiplyQuaternions(*orientation, flip);
                if (!orientation) {
                    return std::nullopt;
                }
            }
        }
        return normalizedQuaternion(*orientation);
    }

    [[nodiscard]] inline std::optional<std::array<float, 4>>
    composePanelOrientation(
        const std::array<float, 4>& inheritedHandFacingBasis) noexcept
    {
        return multiplyQuaternions(
            inheritedHandFacingBasis,
            kProberPanelPose.localOrientation);
    }
}
