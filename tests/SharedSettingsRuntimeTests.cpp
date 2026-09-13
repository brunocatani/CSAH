#include "settings/SharedSettingsRuntime.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Shared settings runtime test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using csah::shared_settings::Snapshot;
    using csah::shared_settings::diff;

    const Snapshot baseline{};
    require(!baseline.cloudShadows.enabled && !baseline.hairSpecular.enabled &&
            !baseline.wrappedGrass.enabled && !baseline.subsurfaceScattering.enabled &&
            !baseline.volumetricLighting.enabled,
        "unfinished effects must default to disabled");
    require(!diff(baseline, baseline).any(), "equal snapshots changed");

    auto profiling = baseline;
    profiling.diagnostics.gpuProfilingGroups = 3;
    const auto profilingChanges = diff(baseline, profiling);
    require(profilingChanges.any(), "GPU profiling mode diff");
    require(profilingChanges.diagnostics, "GPU profiling classification");
    require(
        profilingChanges.liveFeatureCount() == 1,
        "GPU profiling live group count");

    auto masterOff = baseline;
    masterOff.masterEnabled = false;
    const auto masterChanges = diff(baseline, masterOff);
    require(masterChanges.any(), "master gate diff");
    require(masterChanges.masterGate, "master gate classification");
    require(
        !masterChanges.dlaa,
        "visual master gate changed DLAA/DLSS ownership");
    require(
        !masterChanges.vanillaFixesGate,
        "visual master gate changed Vanilla Fixes ownership");
    require(
        !masterChanges.vanillaFixes,
        "master gate attempted live Vanilla Fixes teardown");
    require(
        !masterChanges.nativeShadows,
        "visual master gate changed Native Shadows ownership");
    require(
        masterChanges.liveFeatureCount() == 0,
        "master gate incorrectly counted as live feature");

    auto verifyLive = [&](auto mutate, const char* message) {
        auto next = baseline;
        mutate(next);
        const auto changes = diff(baseline, next);
        require(changes.any(), message);
        require(changes.liveFeatureCount() == 1, message);
        require(!changes.nativeShadows, message);
    };

    verifyLive(
        [](Snapshot& value) { value.linearLighting.enabled = true; },
        "Linear Lighting diff");
    verifyLive(
        [](Snapshot& value) { value.dlaa.enabled = false; },
        "DLAA diff");
    verifyLive(
        [](Snapshot& value) { value.ibl.enabled = false; },
        "IBL diff");
    verifyLive(
        [](Snapshot& value) {
            value.ibl.dynamicCubemapsEnabled = false;
        },
        "Dynamic Cubemaps diff");
    verifyLive(
        [](Snapshot& value) {
            value.complexMaterials.parallaxEnabled = false;
        },
        "Complex Materials diff");
    verifyLive(
        [](Snapshot& value) { value.contactShadows.enabled = false; },
        "Contact Shadows diff");
    verifyLive(
        [](Snapshot& value) { value.wrappedGrass.enabled = true; },
        "Wrapped Grass diff");
    verifyLive(
        [](Snapshot& value) { value.hairSpecular.enabled = true; },
        "Hair Specular diff");
    verifyLive(
        [](Snapshot& value) {
            value.subsurfaceScattering.enabled = true;
        },
        "Subsurface Scattering diff");
    verifyLive(
        [](Snapshot& value) { value.basicWetness.enabled = true; },
        "Basic Wetness diff");
    verifyLive(
        [](Snapshot& value) { value.pbr.enabled = false; },
        "PBR diff");
    verifyLive(
        [](Snapshot& value) { value.cloudShadows.enabled = true; },
        "Cloud Shadows diff");
    verifyLive(
        [](Snapshot& value) { value.volumetricLighting.enabled = true; },
        "Volumetric Lighting diff");
    auto vanillaFixesOff = baseline;
    vanillaFixesOff.vanillaFixes.enabled = false;
    const auto vanillaFixesGateChanges = diff(baseline, vanillaFixesOff);
    require(vanillaFixesGateChanges.any(), "Vanilla Fixes gate diff");
    require(
        vanillaFixesGateChanges.vanillaFixesGate,
        "Vanilla Fixes gate classification");
    require(
        !vanillaFixesGateChanges.vanillaFixes,
        "Vanilla Fixes gate attempted live renderer teardown");
    require(
        vanillaFixesGateChanges.liveFeatureCount() == 0,
        "Vanilla Fixes gate incorrectly counted as live feature");

    verifyLive(
        [](Snapshot& value) {
            value.vanillaFixes.precipitationOcclusion = false;
        },
        "Vanilla Fixes individual setting diff");
    verifyLive(
        [](Snapshot& value) { value.skylighting.enabled = false; },
        "Skylighting diff");

    auto native = baseline;
    native.nativeShadows.directionalShadowDistance += 250.0f;
    const auto nativeChanges = diff(baseline, native);
    require(nativeChanges.any(), "Native Shadows diff");
    require(nativeChanges.nativeShadows, "Native Shadows classification");
    require(
        nativeChanges.liveFeatureCount() == 0,
        "Native Shadows incorrectly classified as live");

    std::cout << "Shared settings runtime tests passed.\n";
    return EXIT_SUCCESS;
}
