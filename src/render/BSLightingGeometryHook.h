#pragma once

#include <cstdint>

namespace csah::linear_lighting
{
    struct Settings;
}

namespace csah::render
{
    enum class GeometrySourceStage : std::uint32_t
    {
        none = 0,
        lightingState = 1,
        emissiveMultiplier = 2,
    };

    struct GeometryHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        bool techniqueVtableCellOwned{};
        bool geometryVtableCellOwned{};
        bool dFLightProducerCallsitesOwned{};
        bool dFLightProducerEnabled{};
        std::uint64_t techniqueCalls{};
        std::uint64_t calls{};
        std::uint64_t acceptedUpdates{};
        std::uint64_t rejectedSources{};
        std::uint64_t ambientDescriptors{};
        std::uint64_t directionalDescriptors{};
        std::uint64_t otherDescriptors{};
        std::uint64_t ambientTransformCalls{};
        std::uint64_t ambientTransformPrepared{};
        std::uint64_t diffuseAmbientPrepared{};
        std::uint64_t latestDiffuseGeneration{};
        float latestDiffuseCoverage{};
        float latestDiffuseMaximumCoefficientDelta{};
        std::uint64_t ambientTransformPassThrough{};
        std::uint64_t directionalPowCalls{};
        std::uint64_t directionalPowModified{};
        std::uint64_t directionalPowPassThrough{};
        std::uint64_t invalidPowResults{};
        std::uint64_t validationFailures{};
        GeometrySourceStage deepestStage{};
        std::uint32_t lastDescriptor{};
        float lastSourceEmissiveMultiplier{};
        float activeDirectionalGamma{};
        float activeDirectionalMultiplier{};
        float activeAmbientGamma{};
        float activeAmbientMultiplier{};
    };

    // Installs a process-lifetime patch on the verified Fallout4VR.exe 1.2.72
    // active VR-extended surface-lighting vtable. The exact cell target and
    // function bytes are checked before the write. No flat-FO4 address or
    // CommonLib relocation is used here.
    [[nodiscard]] bool installBSLightingGeometryHook() noexcept;
    [[nodiscard]] bool validateBSLightingGeometryHook(
        const char* trigger) noexcept;
    void publishDFLightProducerSettings(
        const linear_lighting::Settings& settings) noexcept;
    [[nodiscard]] GeometryHookSnapshot geometryHookSnapshot() noexcept;
}
