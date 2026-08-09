#include "ui/PointerClickGate.h"

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

    void testLeaseMaturityAndNeutralGate()
    {
        using namespace community_shaders::ui::pointer_click_gate;
        State state{};
        auto result = advance(state, 10, true, true, false, true);
        expect(!result.forwardPrimaryDown, "first lease frame is neutral");
        result = advance(state, 11, true, true, false, true);
        expect(state.neutralObserved, "later neutral frame arms the gate");
        result = advance(state, 12, true, true, true, true);
        expect(result.forwardPrimaryDown, "fresh press is forwarded");
    }

    void testHeldInputAndFailureCleanup()
    {
        using namespace community_shaders::ui::pointer_click_gate;
        State state{};
        (void)advance(state, 20, true, true, true, true);
        auto result = advance(state, 21, true, true, true, true);
        expect(!result.forwardPrimaryDown, "held input cannot cross a lease");
        result = advance(state, 22, false, true, false, false);
        expect(result.clearLease && !state.leaseAccepted,
            "route loss retires suppression state");
    }

    void testFrameRegressionRestartsGate()
    {
        using namespace community_shaders::ui::pointer_click_gate;
        State state{};
        (void)advance(state, 50, true, true, false, true);
        const auto result = advance(state, 49, true, true, true, true);
        expect(!result.forwardPrimaryDown && state.firstAcceptedFrame == 49,
            "frame regression restarts maturity");
    }
}

int main()
{
    testLeaseMaturityAndNeutralGate();
    testHeldInputAndFailureCleanup();
    testFrameRegressionRestartsGate();
    if (failures) {
        return 1;
    }
    std::cout << "Pointer click gate tests passed.\n";
    return 0;
}
