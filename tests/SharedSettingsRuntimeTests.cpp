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
    using community_shaders::shared_settings::Snapshot;
    using community_shaders::shared_settings::diff;

    const Snapshot baseline{};
    require(!diff(baseline, baseline).any(), "equal snapshots changed");

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
            value.complexMaterials.parallaxEnabled = false;
        },
        "Complex Materials diff");
    verifyLive(
        [](Snapshot& value) { value.contactShadows.enabled = false; },
        "Contact Shadows diff");
    verifyLive(
        [](Snapshot& value) { value.wrappedGrass.enabled = false; },
        "Wrapped Grass diff");
    verifyLive(
        [](Snapshot& value) { value.hairSpecular.enabled = false; },
        "Hair Specular diff");
    verifyLive(
        [](Snapshot& value) {
            value.subsurfaceScattering.enabled = false;
        },
        "Subsurface Scattering diff");
    verifyLive(
        [](Snapshot& value) { value.basicWetness.enabled = true; },
        "Basic Wetness diff");
    verifyLive(
        [](Snapshot& value) { value.cloudShadows.enabled = false; },
        "Cloud Shadows diff");
    verifyLive(
        [](Snapshot& value) { value.vanillaFixes.enabled = false; },
        "Vanilla Fixes diff");
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
