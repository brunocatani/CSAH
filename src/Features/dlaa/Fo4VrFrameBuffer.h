#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace csah::dlaa
{
    struct Float4
    {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    using Matrix4 = std::array<Float4, 4>;

    // Exact FO4VR PerFrameCamera b12 layout reconstructed from the local
    // vanilla DXBC contracts in package/Shaders/Community/Common/FrameBuffer.hlsli.
    // Runtime qualification still verifies the mapped buffer size and screen
    // parameters before any temporal consumer is allowed to use it.
    struct Fo4VrFrameBuffer
    {
        Matrix4 view;
        std::array<Matrix4, 2> projection;
        std::array<Matrix4, 2> viewProjection;
        Matrix4 inverseView;
        std::array<Matrix4, 2> inverseViewProjection;
        std::array<Matrix4, 2> inverseProjection;
        std::array<Matrix4, 2> unknown;
        Float4 projectionParameters;
        Float4 timeParameters;
        // The live FO4VR image-space path leaves this vector zero. Packed
        // render dimensions come from the qualified texture/viewport contract,
        // never from this shader-visible but unused slot.
        Float4 screenParameters;
        std::array<Matrix4, 2> previousViewProjectionUnjittered;
        Float4 positionAdjustLeft;
        Float4 previousPositionAdjustLeft;
        Float4 positionAdjustRight;
        Float4 previousPositionAdjustRight;
        std::array<Matrix4, 2> viewProjectionUnjittered;
        std::array<Float4, 6> fog;
    };

    static_assert(offsetof(Fo4VrFrameBuffer, projection) == 0x40);
    static_assert(offsetof(Fo4VrFrameBuffer, inverseView) == 0x140);
    static_assert(offsetof(Fo4VrFrameBuffer, projectionParameters) == 0x300);
    static_assert(offsetof(Fo4VrFrameBuffer,
                      previousViewProjectionUnjittered) == 0x330);
    static_assert(offsetof(Fo4VrFrameBuffer, positionAdjustLeft) == 0x3B0);
    static_assert(offsetof(Fo4VrFrameBuffer, viewProjectionUnjittered) == 0x3F0);
    static_assert(sizeof(Fo4VrFrameBuffer) == 0x4D0);
}
