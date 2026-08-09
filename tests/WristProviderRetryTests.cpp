#include "ui/WristProviderRetry.h"

#include <iostream>

namespace
{
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void testDelayedProviderBecomesReady()
    {
        using namespace community_shaders::ui::wrist_provider_retry;
        Gate gate{ 4 };
        expect(gate.beginAttempt(), "initial provider probe starts");
        expect(gate.complete(Outcome::RetryableFailure) ==
                State::AwaitingAttempt,
            "transient startup result leaves a retry available");
        expect(gate.beginAttempt(), "lifecycle retry starts");
        expect(gate.complete(Outcome::Ready) == State::Ready,
            "delayed provider can become ready");
        expect(gate.attempts() == 2, "successful retry records two attempts");
        expect(!gate.beginAttempt(), "ready provider cannot be reprobed");
    }

    void testTerminalFailureStopsImmediately()
    {
        using namespace community_shaders::ui::wrist_provider_retry;
        Gate gate{ 4 };
        expect(gate.beginAttempt(), "terminal probe starts");
        expect(gate.complete(Outcome::TerminalFailure) ==
                State::TerminalFailure,
            "incompatible provider becomes terminal");
        expect(!gate.beginAttempt(), "terminal provider cannot be reprobed");
    }

    void testRetryBudgetIsBounded()
    {
        using namespace community_shaders::ui::wrist_provider_retry;
        Gate gate{ 2 };
        expect(gate.beginAttempt(), "first bounded probe starts");
        expect(gate.complete(Outcome::RetryableFailure) ==
                State::AwaitingAttempt,
            "first transient failure remains retryable");
        expect(gate.beginAttempt(), "last bounded probe starts");
        expect(gate.complete(Outcome::RetryableFailure) == State::Exhausted,
            "last transient failure exhausts the retry budget");
        expect(gate.attempts() == gate.maximumAttempts(),
            "retry attempts never exceed their budget");
        expect(!gate.beginAttempt(), "exhausted provider cannot be reprobed");
    }
}

int main()
{
    testDelayedProviderBecomesReady();
    testTerminalFailureStopsImmediately();
    testRetryBudgetIsBounded();
    if (failures) {
        return 1;
    }
    std::cout << "Wrist provider retry tests passed.\n";
    return 0;
}
