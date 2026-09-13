#include "Features/linear_lighting/LinearLightingSettings.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    template <class T>
    concept HasBloodEffectMultiplier = requires(T value) {
        value.bloodEffectMultiplier;
    };

    template <class T>
    concept HasProjectedEffectMultiplier = requires(T value) {
        value.projectedEffectMultiplier;
    };

    template <class T>
    concept HasDeferredEffectMultiplier = requires(T value) {
        value.deferredEffectMultiplier;
    };

    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Linear Lighting settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool near(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 0.00001f;
    }
}

int main()
{
    using namespace csah::linear_lighting;

    static_assert(!HasBloodEffectMultiplier<Settings>);
    static_assert(!HasProjectedEffectMultiplier<Settings>);
    static_assert(!HasDeferredEffectMultiplier<Settings>);

    const Settings defaults{};
    require(!defaults.enabled, "feature must default disabled");
    require(defaults.preserveNativeDarkness,
        "FO4VR native darkness calibration must default enabled");
    require(near(defaults.colorGamma, 1.8f), "current upstream color gamma");
    require(near(defaults.fogGamma, 1.97f), "current upstream fog gamma");
    require(near(defaults.effectGamma, 1.4f), "current upstream effect gamma");
    require(near(defaults.effectAlphaGamma, 1.55f), "current upstream effect alpha gamma");
    require(near(defaults.vanillaDiffuseColorMultiplier, 1.0f), "current upstream diffuse multiplier");
    require(near(defaults.ambientMultiplier, 1.0f), "current upstream ambient multiplier");
    require(near(defaults.glowmapMultiplier, 0.66f), "current upstream glowmap multiplier");

    Settings invalid = defaults;
    invalid.enabled = true;
    invalid.lightGamma = std::numeric_limits<float>::quiet_NaN();
    invalid.colorGamma = -100.0f;
    invalid.fogGamma = 100.0f;
    invalid.emitColorMultiplier = -1.0f;
    invalid.glowmapMultiplier = std::numeric_limits<float>::infinity();
    const auto safe = sanitize(invalid);
    require(near(safe.lightGamma, defaults.lightGamma), "non-finite gamma uses default");
    require(near(safe.colorGamma, 0.1f), "gamma lower bound");
    require(near(safe.fogGamma, 3.0f), "gamma upper bound");
    require(near(safe.emitColorMultiplier, 0.0f), "multiplier lower bound");
    require(near(safe.glowmapMultiplier, defaults.glowmapMultiplier), "non-finite multiplier uses default");

    const auto disabled = makeFrameData(invalid, false, false, 2.0f);
    require(disabled.enableLinearLighting == 0, "runtime gate disables GPU feature");
    const auto enabled = makeFrameData(invalid, true, true, 2.0f);
    require(enabled.enableLinearLighting == 1, "settings and runtime gate enable GPU feature");
    require(enabled.isDirectionalLightLinear == 1, "directional light space flag");
    require(near(enabled.directionalLightRuntimeMultiplier, 2.0f), "runtime directional multiplier");
    require(near(enabled.lightGamma, kNativeLightingResponseGamma),
        "native darkness calibrates direct-light response");
    require(near(enabled.ambientGamma, kNativeLightingResponseGamma),
        "native darkness calibrates ambient response");
    require(near(enabled.fogGamma, safe.fogGamma),
        "native darkness preserves stronger custom fog response");
    require(near(enabled.skyGamma, kNativeLightingResponseGamma),
        "native darkness calibrates sky response");
    require(near(enabled.colorGamma, safe.colorGamma),
        "native darkness does not alter material colour response");
    require(
        near(std::bit_cast<float>(enabled.lightProducerGammaBits), 2.2f),
        "native producer gamma defaults to 2.2");
    const auto customProducer =
        makeFrameData(invalid, true, true, 2.0f, 1.7f);
    require(
        near(
            std::bit_cast<float>(customProducer.lightProducerGammaBits),
            1.7f),
        "verified producer gamma is published in the ABI slot");
    require(near(enabled.bloodEffectMultiplier, 1.0f),
        "unsupported Blood ABI slot must remain neutral");
    require(near(enabled.projectedEffectMultiplier, 1.0f),
        "unsupported Projected ABI slot must remain neutral");
    require(near(enabled.deferredEffectMultiplier, 1.0f),
        "unsupported Deferred ABI slot must remain neutral");

    Settings defaultEnabled = defaults;
    defaultEnabled.enabled = true;
    const auto defaultFrame =
        makeFrameData(defaultEnabled, true, false, 1.0f);
    require(near(defaultFrame.fogGamma, kNativeLightingResponseGamma),
        "native darkness calibrates default fog response");
    require(near(defaultFrame.colorGamma, defaults.colorGamma),
        "default material colour response remains upstream calibrated");

    Settings artistic = defaults;
    artistic.enabled = true;
    artistic.preserveNativeDarkness = false;
    artistic.lightGamma = 1.6f;
    artistic.ambientGamma = 1.7f;
    artistic.fogGamma = 1.9f;
    artistic.skyGamma = 1.5f;
    const auto artisticFrame =
        makeFrameData(artistic, true, false, 1.0f);
    require(near(artisticFrame.lightGamma, artistic.lightGamma),
        "disabled calibration retains custom light gamma");
    require(near(artisticFrame.ambientGamma, artistic.ambientGamma),
        "disabled calibration retains custom ambient gamma");
    require(near(artisticFrame.fogGamma, artistic.fogGamma),
        "disabled calibration retains custom fog gamma");
    require(near(artisticFrame.skyGamma, artistic.skyGamma),
        "disabled calibration retains custom sky gamma");

    artistic.preserveNativeDarkness = true;
    artistic.lightGamma = 2.4f;
    const auto highGammaFrame =
        makeFrameData(artistic, true, false, 1.0f);
    require(near(highGammaFrame.lightGamma, 2.4f),
        "native darkness preserves stronger custom response");

    std::cout << "Linear Lighting settings tests passed.\n";
    return EXIT_SUCCESS;
}
