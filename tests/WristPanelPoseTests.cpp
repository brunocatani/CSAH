#include "ui/WristPanelPose.h"

#include <cmath>
#include <iostream>

namespace
{
    constexpr float kTolerance = 0.0005f;
    int failures{};

    [[nodiscard]] bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) <= kTolerance;
    }

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void testExactProberConfiguration()
    {
        using namespace community_shaders::ui::wrist_panel_pose;
        expect(near(kProberPanelPose.positionX, 7.75f),
            "position X matches Prober");
        expect(near(kProberPanelPose.positionY, 9.0f),
            "position Y matches Prober");
        expect(near(kProberPanelPose.positionZ, -16.5f),
            "position Z matches Prober");
        expect(near(kProberPanelPose.rotationXDegrees, 0.0f) &&
                near(kProberPanelPose.rotationYDegrees, 90.0f) &&
                near(kProberPanelPose.rotationZDegrees, 6.0f),
            "Euler rotation matches Prober");
        expect(kProberPanelPose.flipX && kProberPanelPose.flipY &&
                !kProberPanelPose.flipZ,
            "final panel-local flips match Prober");
    }

    void testPrecomputedOrientationMatchesProberComposition()
    {
        using namespace community_shaders::ui::wrist_panel_pose;
        const auto derived =
            localOrientationFromEulerAndFlips(kProberPanelPose);
        expect(derived.has_value(), "Prober Euler/flip orientation is valid");
        if (!derived) {
            return;
        }
        for (std::size_t component{};
             component < derived->size();
             ++component) {
            expect(near(
                       (*derived)[component],
                       kProberPanelPose.localOrientation[component]),
                "precomputed hot-path orientation matches Prober semantics");
        }
    }

    void testIdentityHandGoldenOrientation()
    {
        using namespace community_shaders::ui::wrist_panel_pose;
        constexpr std::array<float, 4> inheritedHandFacingBasis{
            -0.5f, 0.5f, 0.5f, -0.5f };
        constexpr std::array<float, 4> expected{
            0.0f, 0.6691306064f, -0.7431448255f, 0.0f };
        const auto result =
            composePanelOrientation(inheritedHandFacingBasis);
        expect(result.has_value(), "identity hand pose composes successfully");
        if (!result) {
            return;
        }
        for (std::size_t component{}; component < result->size(); ++component) {
            expect(near((*result)[component], expected[component]),
                "identity hand pose matches Prober golden orientation");
        }
    }
}

int main()
{
    testExactProberConfiguration();
    testPrecomputedOrientationMatchesProberComposition();
    testIdentityHandGoldenOrientation();
    if (failures) {
        return 1;
    }
    std::cout << "Wrist panel pose tests passed.\n";
    return 0;
}
