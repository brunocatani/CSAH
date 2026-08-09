#include "ui/LinearLightingTelemetryGate.h"

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

    void testMilestonesAppearOnlyWhenProven()
    {
        using namespace community_shaders::ui::linear_lighting_telemetry;
        State state{};
        Sample sample{};
        auto events = advance(state, sample);
        expect(!events.activationReady && !events.firstShaderBind &&
                !events.firstReplacementBind && !events.firstGeometryCall &&
                !events.firstGeometryUpdate && !events.settingsApplied,
            "empty runtime does not produce proof events");

        sample.enabled = true;
        sample.gpuResourcesReady = true;
        sample.geometryProviderReady = true;
        sample.frameDataUploads = 1;
        events = advance(state, sample);
        expect(events.activationReady,
            "fully initialized enabled runtime produces activation proof");

        sample.pixelShaderBindCalls = 4;
        sample.replacementBinds = 1;
        sample.geometryCalls = 1;
        sample.geometryUpdates = 1;
        events = advance(state, sample);
        expect(events.firstShaderBind && events.firstReplacementBind &&
                events.firstGeometryCall && events.firstGeometryUpdate,
            "observed hook, replacement, and geometry work produce proof events");
    }

    void testMilestonesAreNotRepeated()
    {
        using namespace community_shaders::ui::linear_lighting_telemetry;
        State state{};
        Sample sample{
            .enabled = true,
            .gpuResourcesReady = true,
            .geometryProviderReady = true,
            .frameDataUploads = 1,
            .pixelShaderBindCalls = 5,
            .replacementBinds = 3,
            .geometryCalls = 2,
            .geometryUpdates = 2,
        };
        (void)advance(state, sample);
        const auto events = advance(state, sample);
        expect(!events.activationReady && !events.firstShaderBind &&
                !events.firstReplacementBind && !events.firstGeometryCall &&
                !events.firstGeometryUpdate,
            "steady runtime does not repeat milestone logs");
    }

    void testGeometryCallDoesNotClaimUpdate()
    {
        using namespace community_shaders::ui::linear_lighting_telemetry;
        State state{};
        Sample sample{
            .geometryCalls = 3,
        };
        auto events = advance(state, sample);
        expect(events.firstGeometryCall && !events.firstGeometryUpdate,
            "observed geometry call reports classification without claiming an update");
        events = advance(state, sample);
        expect(!events.firstGeometryCall && !events.firstGeometryUpdate,
            "geometry-call classification milestone is not repeated");
    }

    void testAppliedRevisionAdvancesMonotonically()
    {
        using namespace community_shaders::ui::linear_lighting_telemetry;
        State state{};
        Sample sample{};
        sample.queuedSettingsRevision = 2;
        sample.appliedSettingsRevision = 1;
        auto events = advance(state, sample);
        expect(events.settingsApplied,
            "first render-applied revision produces proof");
        events = advance(state, sample);
        expect(!events.settingsApplied,
            "same applied revision is not repeated");
        sample.appliedSettingsRevision = 2;
        events = advance(state, sample);
        expect(events.settingsApplied &&
                state.reportedAppliedSettingsRevision == 2,
            "newer applied revision produces a new proof event");
    }
}

int main()
{
    testMilestonesAppearOnlyWhenProven();
    testMilestonesAreNotRepeated();
    testGeometryCallDoesNotClaimUpdate();
    testAppliedRevisionAdvancesMonotonically();
    if (failures) {
        return 1;
    }
    std::cout << "Linear Lighting telemetry gate tests passed.\n";
    return 0;
}
