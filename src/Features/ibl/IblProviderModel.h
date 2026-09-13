#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace csah::ibl
{
    constexpr std::uint32_t kEnvironmentCubeFaceCount = 6;
    constexpr std::uint32_t kEnvironmentMaximumMipCount = 10;

    enum class EnvironmentCubeFace : std::uint8_t
    {
        positiveX,
        negativeX,
        positiveY,
        negativeY,
        positiveZ,
        negativeZ,
    };

    struct Float3
    {
        float x{};
        float y{};
        float z{};
    };

    [[nodiscard]] constexpr bool validEnvironmentCubeFace(
        EnvironmentCubeFace face) noexcept
    {
        return static_cast<std::uint32_t>(face) <
            kEnvironmentCubeFaceCount;
    }

    // Exact inverse of the Direct3D TextureCube direction-to-face mapping.
    // Inputs are UAV array-face coordinates in [-1, +1], with vertical
    // increasing down the texture.
    [[nodiscard]] inline Float3 environmentCubeDirection(
        EnvironmentCubeFace face,
        float horizontal,
        float vertical) noexcept
    {
        Float3 direction{};
        switch (face) {
        case EnvironmentCubeFace::positiveX:
            direction = { 1.0f, -vertical, -horizontal };
            break;
        case EnvironmentCubeFace::negativeX:
            direction = { -1.0f, -vertical, horizontal };
            break;
        case EnvironmentCubeFace::positiveY:
            direction = { horizontal, 1.0f, vertical };
            break;
        case EnvironmentCubeFace::negativeY:
            direction = { horizontal, -1.0f, -vertical };
            break;
        case EnvironmentCubeFace::positiveZ:
            direction = { horizontal, -vertical, 1.0f };
            break;
        case EnvironmentCubeFace::negativeZ:
            direction = { -horizontal, -vertical, -1.0f };
            break;
        default:
            return {};
        }

        const auto magnitudeSquared = direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z;
        if (!(magnitudeSquared > 0.0f) ||
            !std::isfinite(magnitudeSquared)) {
            return {};
        }
        const auto inverseMagnitude = 1.0f / std::sqrt(magnitudeSquared);
        return {
            direction.x * inverseMagnitude,
            direction.y * inverseMagnitude,
            direction.z * inverseMagnitude,
        };
    }

    class EnvironmentUpdateCoverage final
    {
    public:
        void reset() noexcept
        {
            completedSubresources_ = 0;
        }

        [[nodiscard]] bool markComplete(
            EnvironmentCubeFace face,
            std::uint32_t mipLevel,
            std::uint32_t mipCount) noexcept
        {
            if (!validEnvironmentCubeFace(face) || mipCount == 0 ||
                mipCount > kEnvironmentMaximumMipCount ||
                mipLevel >= mipCount) {
                return false;
            }
            const auto bitIndex =
                mipLevel * kEnvironmentCubeFaceCount +
                static_cast<std::uint32_t>(face);
            completedSubresources_ |= std::uint64_t{ 1 } << bitIndex;
            return true;
        }

        [[nodiscard]] bool complete(std::uint32_t mipCount) const noexcept
        {
            if (mipCount == 0 || mipCount > kEnvironmentMaximumMipCount) {
                return false;
            }
            const auto bitCount = mipCount * kEnvironmentCubeFaceCount;
            const auto expected = bitCount ==
                    std::numeric_limits<std::uint64_t>::digits ?
                std::numeric_limits<std::uint64_t>::max() :
                (std::uint64_t{ 1 } << bitCount) - 1;
            return completedSubresources_ == expected;
        }

        [[nodiscard]] std::uint64_t bits() const noexcept
        {
            return completedSubresources_;
        }

    private:
        std::uint64_t completedSubresources_{};
    };
}
