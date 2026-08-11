#pragma once

#include <array>
#include <cstdint>

namespace community_shaders::linear_lighting
{
    struct Settings;
}

namespace community_shaders::render
{
    struct DirectionalProducerEnergySample
    {
        bool captured{};
        std::uint32_t descriptor{};
        float gamma{};
        float multiplier{ 1.0f };
        float vanillaExponent{};
        std::array<float, 3> source{};
        std::array<float, 3> gammaOutput{};
        std::array<float, 3> finalOutput{};
    };

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
        DirectionalProducerEnergySample directionalEnergySample{};
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
