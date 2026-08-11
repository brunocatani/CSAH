#include "Features/ibl/IblSceneRadianceProbeModel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "IBL scene-radiance probe model test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool near(float left, float right) noexcept
    {
        return std::abs(left - right) <= 0.00001f;
    }
}

int main()
{
    using namespace community_shaders::ibl;

    constexpr auto coordinates = sceneProbeCoordinates(5376, 2880);
    static_assert(coordinates[0].x == 672 && coordinates[0].y == 720);
    static_assert(coordinates[1].x == 2016 && coordinates[1].y == 720);
    static_assert(coordinates[2].x == 3360 && coordinates[2].y == 720);
    static_assert(coordinates[3].x == 4704 && coordinates[3].y == 720);
    static_assert(coordinates[4].y == 2160 && coordinates[7].y == 2160);

    constexpr auto r11One = 15u << 6;
    constexpr auto g11One = (15u << 6) << 11;
    constexpr auto b10One = (15u << 5) << 22;
    const auto floatOne = decodeR11G11B10Float(
        r11One | g11One | b10One);
    require(
        near(floatOne.red, 1.0f) && near(floatOne.green, 1.0f) &&
            near(floatOne.blue, 1.0f),
        "R11G11B10 one decoding");
    const auto floatZero = decodeR11G11B10Float(0);
    require(
        near(floatZero.red, 0.0f) && near(floatZero.green, 0.0f) &&
            near(floatZero.blue, 0.0f),
        "R11G11B10 zero decoding");

    constexpr auto unorm = decodeR8G8B8A8Unorm(0xFF0080FFu);
    static_assert(unorm.red == 1.0f);
    require(near(unorm.green, 128.0f / 255.0f), "RGBA8 green decoding");
    require(near(unorm.blue, 0.0f), "RGBA8 blue decoding");

    std::array<SceneProbeRgb, kSceneProbeSampleCount> stereoSamples{};
    stereoSamples[0] = { 1.0f, 0.0f, 0.0f };
    stereoSamples[1] = { 1.0f, 0.0f, 0.0f };
    stereoSamples[2] = { 0.0f, 0.0f, 1.0f };
    stereoSamples[3] = { 0.0f, 0.0f, 1.0f };
    stereoSamples[4] = stereoSamples[0];
    stereoSamples[5] = stereoSamples[1];
    stereoSamples[6] = stereoSamples[2];
    stereoSamples[7] = stereoSamples[3];
    const auto summary = summarizeSceneProbe(stereoSamples);
    require(summary.validSamples == 8, "all samples are valid");
    require(summary.nonBlackSamples == 8, "all samples are non-black");
    require(near(summary.average.red, 0.5f), "combined red average");
    require(near(summary.average.blue, 0.5f), "combined blue average");
    require(near(summary.leftEyeAverage.red, 1.0f), "left-eye partition");
    require(near(summary.rightEyeAverage.blue, 1.0f), "right-eye partition");
    require(
        near(meanAbsoluteSceneProbeDifference(stereoSamples, stereoSamples),
            0.0f),
        "identical samples have zero difference");
    const std::array<SceneProbeRgb, kSceneProbeSampleCount> blackSamples{};
    require(
        near(
            meanAbsoluteSceneProbeDifference(stereoSamples, blackSamples),
            1.0f / 3.0f),
        "mean absolute RGB difference");

    std::cout << "IBL scene-radiance probe model tests passed.\n";
    return EXIT_SUCCESS;
}
