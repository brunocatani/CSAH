#pragma once

#include <cstdint>

namespace community_shaders::ui::pointer_click_gate
{
    struct State
    {
        bool leaseAccepted{};
        bool neutralObserved{};
        std::uint64_t firstAcceptedFrame{};
    };

    struct Result
    {
        bool forwardPrimaryDown{};
        bool clearLease{};
    };

    [[nodiscard]] inline bool reset(State& state) noexcept
    {
        const auto hadLease = state.leaseAccepted;
        state = {};
        return hadLease;
    }

    // ROCK consumes a suppression request after native input for the current
    // provider frame. The lease must mature on a later frame and observe a
    // neutral physical level before the wrist UI may forward a press.
    [[nodiscard]] inline Result advance(
        State& state,
        std::uint64_t frameIndex,
        bool centrallyRouted,
        bool rawInputAvailable,
        bool primaryDown,
        bool leaseRequestAccepted) noexcept
    {
        if (!centrallyRouted || !rawInputAvailable ||
            !leaseRequestAccepted) {
            return { false, reset(state) };
        }
        if (!state.leaseAccepted || frameIndex < state.firstAcceptedFrame) {
            state.leaseAccepted = true;
            state.neutralObserved = false;
            state.firstAcceptedFrame = frameIndex;
        }
        const auto mature = frameIndex > state.firstAcceptedFrame;
        if (mature && !primaryDown) {
            state.neutralObserved = true;
        }
        return { mature && state.neutralObserved && primaryDown, false };
    }
}
