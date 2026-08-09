#pragma once

#include <cstdint>

namespace community_shaders::ui::linear_lighting_telemetry
{
    struct Sample
    {
        bool enabled{};
        bool gpuResourcesReady{};
        bool geometryProviderReady{};
        std::uint64_t queuedSettingsRevision{};
        std::uint64_t appliedSettingsRevision{};
        std::uint64_t frameDataUploads{};
        std::uint64_t pixelShaderBindCalls{};
        std::uint64_t replacementBinds{};
        std::uint64_t geometryUpdates{};
    };

    struct State
    {
        std::uint64_t reportedAppliedSettingsRevision{};
        bool activationReadyReported{};
        bool shaderBindReported{};
        bool replacementBindReported{};
        bool geometryUpdateReported{};
    };

    struct Events
    {
        bool settingsApplied{};
        bool activationReady{};
        bool firstShaderBind{};
        bool firstReplacementBind{};
        bool firstGeometryUpdate{};
    };

    [[nodiscard]] inline Events advance(
        State& state,
        const Sample& sample) noexcept
    {
        Events events{};
        if (sample.appliedSettingsRevision >
            state.reportedAppliedSettingsRevision) {
            state.reportedAppliedSettingsRevision =
                sample.appliedSettingsRevision;
            events.settingsApplied = true;
        }
        if (!state.activationReadyReported && sample.enabled &&
            sample.gpuResourcesReady && sample.geometryProviderReady &&
            sample.frameDataUploads > 0) {
            state.activationReadyReported = true;
            events.activationReady = true;
        }
        if (!state.shaderBindReported && sample.pixelShaderBindCalls > 0) {
            state.shaderBindReported = true;
            events.firstShaderBind = true;
        }
        if (!state.replacementBindReported && sample.replacementBinds > 0) {
            state.replacementBindReported = true;
            events.firstReplacementBind = true;
        }
        if (!state.geometryUpdateReported && sample.geometryUpdates > 0) {
            state.geometryUpdateReported = true;
            events.firstGeometryUpdate = true;
        }
        return events;
    }
}
