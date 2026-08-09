#pragma once

#include <cstdint>

namespace community_shaders::ui::wrist_provider_retry
{
    enum class Outcome : std::uint8_t
    {
        Ready,
        RetryableFailure,
        TerminalFailure
    };

    enum class State : std::uint8_t
    {
        AwaitingAttempt,
        Ready,
        TerminalFailure,
        Exhausted
    };

    class Gate
    {
    public:
        explicit constexpr Gate(std::uint32_t maximumAttempts) noexcept :
            maximumAttempts_(maximumAttempts == 0 ? 1 : maximumAttempts)
        {}

        [[nodiscard]] constexpr bool beginAttempt() noexcept
        {
            if (state_ != State::AwaitingAttempt || attemptInFlight_ ||
                attempts_ >= maximumAttempts_) {
                return false;
            }
            ++attempts_;
            attemptInFlight_ = true;
            return true;
        }

        [[nodiscard]] constexpr State complete(Outcome outcome) noexcept
        {
            if (!attemptInFlight_) {
                return state_;
            }
            attemptInFlight_ = false;
            switch (outcome) {
            case Outcome::Ready:
                state_ = State::Ready;
                break;
            case Outcome::TerminalFailure:
                state_ = State::TerminalFailure;
                break;
            case Outcome::RetryableFailure:
                if (attempts_ >= maximumAttempts_) {
                    state_ = State::Exhausted;
                }
                break;
            }
            return state_;
        }

        [[nodiscard]] constexpr State state() const noexcept
        {
            return state_;
        }

        [[nodiscard]] constexpr std::uint32_t attempts() const noexcept
        {
            return attempts_;
        }

        [[nodiscard]] constexpr std::uint32_t maximumAttempts() const noexcept
        {
            return maximumAttempts_;
        }

    private:
        std::uint32_t maximumAttempts_{};
        std::uint32_t attempts_{};
        State state_{ State::AwaitingAttempt };
        bool attemptInFlight_{};
    };
}
