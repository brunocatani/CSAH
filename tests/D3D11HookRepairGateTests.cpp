#include "render/D3D11HookRepairGate.h"

#include <cstdlib>
#include <iostream>

namespace
{
    using community_shaders::render::d3d11_hook_repair::Decision;
    using community_shaders::render::d3d11_hook_repair::State;
    using community_shaders::render::d3d11_hook_repair::advance;

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void testStableOwnershipNeedsNoRepair()
    {
        State state{};
        expect(
            advance(state, true, true, 0x1000, 12) == Decision::noAction,
            "owned hook remains untouched");
    }

    void testStableBypassRequiresTwoObservations()
    {
        State state{};
        expect(
            advance(state, true, false, 0x2000, 0) ==
                Decision::observeDisplacement,
            "first bypass observation does not mutate the chain");
        expect(
            advance(state, true, false, 0x2000, 0) ==
                Decision::repairDisplacement,
            "unchanged bind count proves the displaced target bypasses us");
    }

    void testReachableDownstreamChainIsPreserved()
    {
        State state{};
        (void)advance(state, true, false, 0x3000, 4);
        expect(
            advance(state, true, false, 0x3000, 9) ==
                Decision::preserveReachableChain,
            "a downstream chain that reaches our hook is preserved");
        expect(
            advance(state, true, false, 0x3000, 9) ==
                Decision::preserveReachableChain,
            "proven downstream reachability remains stable without new binds");
    }

    void testNewTargetRequiresFreshObservation()
    {
        State state{};
        (void)advance(state, true, false, 0x4000, 0);
        expect(
            advance(state, true, false, 0x5000, 0) ==
                Decision::observeDisplacement,
            "a new displaced target is observed before mutation");
    }
}

int main()
{
    testStableOwnershipNeedsNoRepair();
    testStableBypassRequiresTwoObservations();
    testReachableDownstreamChainIsPreserved();
    testNewTargetRequiresFreshObservation();
    return EXIT_SUCCESS;
}
