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
        expect(!events.activationReady && !events.firstReplacementBind &&
                !events.firstGeometryUpdate && !events.settingsApplied,
            "empty runtime does not produce proof events");

        sample.enabled = true;
        sample.gpuResourcesReady = true;
        sample.geometryProviderReady = true;
        sample.frameDataUploads = 1;
        events = advance(state, sample);
        expect(events.activationReady,
            "fully initialized enabled runtime produces activation proof");

        sample.replacementBinds = 1;
        sample.geometryUpdates = 1;
        events = advance(state, sample);
        expect(events.firstReplacementBind && events.firstGeometryUpdate,
            "observed bind and geometry upload produce proof events");
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
            .replacementBinds = 3,
            .geometryUpdates = 2,
        };
        (void)advance(state, sample);
        const auto events = advance(state, sample);
        expect(!events.activationReady && !events.firstReplacementBind &&
                !events.firstGeometryUpdate,
            "steady runtime does not repeat milestone logs");
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
    testAppliedRevisionAdvancesMonotonically();
    if (failures) {
        return 1;
    }
    std::cout << "Linear Lighting telemetry gate tests passed.\n";
    return 0;
}
