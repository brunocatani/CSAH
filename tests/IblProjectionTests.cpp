#include "Features/ibl/IblProjectionModel.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using community_shaders::ibl::DiffuseSH;
    using community_shaders::ibl::Float3;

    struct Float4
    {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            fail(message);
        }
    }

    void requireSucceeded(HRESULT result, const std::string& operation)
    {
        if (FAILED(result)) {
            fail(operation + " failed with HRESULT " +
                std::to_string(static_cast<unsigned>(result)));
        }
    }

    [[nodiscard]] std::vector<std::byte> readFile(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        require(stream.is_open(), "could not open " + path.string());
        const auto size = stream.tellg();
        require(size > 0, "shader bytecode is empty");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "could not read shader bytecode");
        return bytes;
    }

    struct D3DDevice
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    [[nodiscard]] D3DDevice createWarpDevice()
    {
        D3DDevice result;
        D3D_FEATURE_LEVEL selectedLevel{};
        constexpr std::array requestedLevels{ D3D_FEATURE_LEVEL_11_0 };
        requireSucceeded(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                requestedLevels.data(),
                static_cast<UINT>(requestedLevels.size()),
                D3D11_SDK_VERSION,
                &result.device,
                &selectedLevel,
                &result.context),
            "D3D11CreateDevice(WARP)");
        require(
            selectedLevel == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");
        return result;
    }

    [[nodiscard]] DiffuseSH projectConstantCubemap(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        ID3D11ComputeShader& shader,
        const Float4& color)
    {
        D3D11_TEXTURE2D_DESC cubeDescription{};
        cubeDescription.Width = 1;
        cubeDescription.Height = 1;
        cubeDescription.MipLevels = 1;
        cubeDescription.ArraySize = 6;
        cubeDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        cubeDescription.SampleDesc.Count = 1;
        cubeDescription.Usage = D3D11_USAGE_IMMUTABLE;
        cubeDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cubeDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        std::array<D3D11_SUBRESOURCE_DATA, 6> initialData{};
        for (auto& subresource : initialData) {
            subresource.pSysMem = &color;
            subresource.SysMemPitch = sizeof(color);
            subresource.SysMemSlicePitch = sizeof(color);
        }
        ComPtr<ID3D11Texture2D> cubeTexture;
        requireSucceeded(
            device.CreateTexture2D(
                &cubeDescription,
                initialData.data(),
                &cubeTexture),
            "CreateTexture2D(cubemap)");

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Format = cubeDescription.Format;
        srvDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDescription.TextureCube.MostDetailedMip = 0;
        srvDescription.TextureCube.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> cubeSrv;
        requireSucceeded(
            device.CreateShaderResourceView(
                cubeTexture.Get(),
                &srvDescription,
                &cubeSrv),
            "CreateShaderResourceView(cubemap)");

        D3D11_TEXTURE2D_DESC outputDescription{};
        outputDescription.Width = 3;
        outputDescription.Height = 1;
        outputDescription.MipLevels = 1;
        outputDescription.ArraySize = 1;
        outputDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        outputDescription.SampleDesc.Count = 1;
        outputDescription.Usage = D3D11_USAGE_DEFAULT;
        outputDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> outputTexture;
        requireSucceeded(
            device.CreateTexture2D(
                &outputDescription,
                nullptr,
                &outputTexture),
            "CreateTexture2D(output)");

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
        uavDescription.Format = outputDescription.Format;
        uavDescription.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11UnorderedAccessView> outputUav;
        requireSucceeded(
            device.CreateUnorderedAccessView(
                outputTexture.Get(),
                &uavDescription,
                &outputUav),
            "CreateUnorderedAccessView(output)");

        auto stagingDescription = outputDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stagingTexture;
        requireSucceeded(
            device.CreateTexture2D(
                &stagingDescription,
                nullptr,
                &stagingTexture),
            "CreateTexture2D(staging)");

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        requireSucceeded(
            device.CreateSamplerState(&samplerDescription, &sampler),
            "CreateSamplerState");

        auto* source = cubeSrv.Get();
        auto* destination = outputUav.Get();
        auto* samplerPointer = sampler.Get();
        context.CSSetShader(&shader, nullptr, 0);
        context.CSSetShaderResources(0, 1, &source);
        context.CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
        context.CSSetSamplers(0, 1, &samplerPointer);
        context.Dispatch(1, 1, 1);
        ID3D11ShaderResourceView* nullSource{};
        ID3D11UnorderedAccessView* nullDestination{};
        context.CSSetShaderResources(0, 1, &nullSource);
        context.CSSetUnorderedAccessViews(0, 1, &nullDestination, nullptr);
        context.CopyResource(stagingTexture.Get(), outputTexture.Get());

        D3D11_QUERY_DESC eventDescription{ D3D11_QUERY_EVENT, 0 };
        ComPtr<ID3D11Query> eventQuery;
        requireSucceeded(
            device.CreateQuery(&eventDescription, &eventQuery),
            "CreateQuery(event)");
        context.End(eventQuery.Get());
        context.Flush();
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        BOOL complete{};
        while (context.GetData(
                   eventQuery.Get(),
                   &complete,
                   sizeof(complete),
                   0) == S_FALSE) {
            require(
                std::chrono::steady_clock::now() < deadline,
                "timed out waiting for WARP projection");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(complete == TRUE, "WARP event query did not complete");

        D3D11_MAPPED_SUBRESOURCE mapped{};
        requireSucceeded(
            context.Map(
                stagingTexture.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped),
            "Map(staging)");
        require(
            mapped.pData && mapped.RowPitch >= sizeof(DiffuseSH),
            "mapped projection has an invalid layout");
        DiffuseSH result{};
        std::memcpy(&result, mapped.pData, sizeof(result));
        context.Unmap(stagingTexture.Get(), 0);
        context.ClearState();
        return result;
    }

    void requireNear(float actual, float expected, float tolerance,
        const std::string& label)
    {
        if (std::abs(actual - expected) > tolerance) {
            fail(label + ": expected " + std::to_string(expected) +
                ", got " + std::to_string(actual));
        }
    }

    [[nodiscard]] Float3 evaluateRadiance(
        const DiffuseSH& coefficients,
        const Float3& direction)
    {
        const auto basis =
            community_shaders::ibl::evaluateFirstOrderSHBasis(direction);
        std::array<float, 3> result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel) {
            for (std::size_t coefficient = 0; coefficient < basis.size();
                 ++coefficient) {
                result[channel] += static_cast<float>(
                    coefficients.rgb[channel][coefficient] *
                    basis[coefficient]);
            }
        }
        return { result[0], result[1], result[2] };
    }

    void verifyValidityAwareFit()
    {
        const DiffuseSH expected{
            .rgb = {
                std::array{ 1.25f, 0.08f, -0.05f, 0.12f },
                std::array{ 1.75f, -0.06f, 0.09f, -0.04f },
                std::array{ 2.25f, 0.04f, 0.03f, 0.08f },
            },
        };
        community_shaders::ibl::DiffuseSHFit fit{};
        constexpr std::uint32_t kExtent = 12;
        for (std::uint32_t face = 0;
             face < community_shaders::ibl::kEnvironmentCubeFaceCount;
             ++face) {
            for (std::uint32_t y = 0; y < kExtent; ++y) {
                for (std::uint32_t x = 0; x < kExtent; ++x) {
                    const auto horizontal =
                        ((static_cast<float>(x) + 0.5f) / kExtent) * 2.0f -
                        1.0f;
                    const auto vertical =
                        ((static_cast<float>(y) + 0.5f) / kExtent) * 2.0f -
                        1.0f;
                    const auto direction =
                        community_shaders::ibl::environmentCubeDirection(
                            static_cast<
                                community_shaders::ibl::EnvironmentCubeFace>(
                                face),
                            horizontal,
                            vertical);
                    const auto validity = face == static_cast<std::uint32_t>(
                                                        community_shaders::ibl::
                                                            EnvironmentCubeFace::
                                                                positiveZ) ?
                        0.0f :
                        1.0f;
                    community_shaders::ibl::accumulateDiffuseSHFit(
                        fit,
                        direction,
                        evaluateRadiance(expected, direction),
                        validity,
                        community_shaders::ibl::cubeTexelSolidAngleWeight(
                            horizontal,
                            vertical));
                }
            }
        }

        const auto coverage =
            community_shaders::ibl::diffuseSHFitCoverage(fit);
        require(
            coverage > 0.80f && coverage < 0.90f,
            "partial-cubemap SH fit reported incorrect coverage");
        DiffuseSH solved{};
        require(
            community_shaders::ibl::solveDiffuseSHFit(fit, solved),
            "partial-cubemap SH fit was singular");
        for (std::size_t channel = 0; channel < 3; ++channel) {
            for (std::size_t coefficient = 0; coefficient < 4;
                 ++coefficient) {
                requireNear(
                    solved.rgb[channel][coefficient],
                    expected.rgb[channel][coefficient],
                    2.0e-4f,
                    "partial-cubemap coefficient");
            }
        }

        community_shaders::ibl::DiffuseSHFit singular{};
        community_shaders::ibl::accumulateDiffuseSHFit(
            singular,
            { 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f },
            1.0f,
            1.0);
        require(
            !community_shaders::ibl::solveDiffuseSHFit(singular, solved),
            "directionally singular SH fit was accepted");
    }

    void verifyAmbientTransform(const DiffuseSH& constantProjection)
    {
        const auto irradiance =
            community_shaders::ibl::evaluateDiffuseIrradiance(
                constantProjection,
                { 0.0f, 1.0f, 0.0f });
        requireNear(irradiance.x, 0.25f, 2.0e-4f, "constant irradiance red");
        requireNear(
            irradiance.y,
            0.5f,
            2.0e-4f,
            "constant irradiance green");
        requireNear(
            irradiance.z,
            0.75f,
            2.0e-4f,
            "constant irradiance blue");

        std::array<float, 16> vanilla{};
        vanilla[12] = 0.5f;
        vanilla[13] = 0.5f;
        vanilla[14] = 0.5f;
        vanilla[15] = 1.0f;
        std::array<float, 16> transform{};
        require(
            community_shaders::ibl::buildDirectionalAmbientTransform(
                constantProjection,
                vanilla,
                1.0f,
                1.0f,
                transform),
            "constant diffuse ambient transform was rejected");
        for (std::size_t index = 0; index < 12; ++index) {
            requireNear(
                transform[index],
                0.0f,
                2.0e-4f,
                "constant diffuse directional row");
        }
        const auto luminance = transform[12] * 0.2126f +
            transform[13] * 0.7152f + transform[14] * 0.0722f;
        requireNear(luminance, 0.5f, 2.0e-4f, "matched ambient luminance");
        requireNear(transform[15], 1.0f, 0.0f, "ambient homogeneous scale");

        vanilla[0] = 0.2f;
        vanilla[4] = -0.1f;
        vanilla[8] = 0.15f;
        require(
            community_shaders::ibl::buildDirectionalAmbientTransform(
                constantProjection,
                vanilla,
                2.0f,
                1.0f,
                transform),
            "directional vanilla brightness match was rejected");
        Float3 vanillaAverage{};
        constexpr std::array<Float3, 6> directions{
            Float3{ -1.0f, 0.0f, 0.0f },
            Float3{ 1.0f, 0.0f, 0.0f },
            Float3{ 0.0f, -1.0f, 0.0f },
            Float3{ 0.0f, 1.0f, 0.0f },
            Float3{ 0.0f, 0.0f, -1.0f },
            Float3{ 0.0f, 0.0f, 1.0f },
        };
        for (const auto& direction : directions) {
            vanillaAverage.x += std::pow(
                                    std::max(
                                        0.0f,
                                        vanilla[0] * direction.x +
                                            vanilla[4] * direction.y +
                                            vanilla[8] * direction.z +
                                            vanilla[12]),
                                    2.0f) /
                directions.size();
            vanillaAverage.y += vanilla[13] * vanilla[13] /
                directions.size();
            vanillaAverage.z += vanilla[14] * vanilla[14] /
                directions.size();
        }
        const auto expectedLuminance = vanillaAverage.x * 0.2126f +
            vanillaAverage.y * 0.7152f + vanillaAverage.z * 0.0722f;
        const auto transformedLuminance =
            transform[12] * transform[12] * 0.2126f +
            transform[13] * transform[13] * 0.7152f +
            transform[14] * transform[14] * 0.0722f;
        requireNear(
            transformedLuminance,
            expectedLuminance,
            3.0e-4f,
            "directional vanilla average luminance");

        auto directional = constantProjection;
        directional.rgb[0][3] = 0.35f;
        require(
            community_shaders::ibl::buildDirectionalAmbientTransform(
                directional,
                vanilla,
                1.0f,
                1.0f,
                transform),
            "directional diffuse ambient transform was rejected");
        require(
            transform[0] < -1.0e-3f,
            "world X directional term was not preserved");
        require(
            !community_shaders::ibl::buildDirectionalAmbientTransform(
                directional,
                vanilla,
                0.0f,
                1.0f,
                transform),
            "zero shader gamma was accepted");
    }

    void run(const std::filesystem::path& root)
    {
        const auto bytecode = readFile(
            root / "package" / "Shaders" / "Community" / "IBL" /
            "DiffuseIblProjectionCS.dxbc");
        require(
            bytecode.size() >= 20 &&
                std::memcmp(bytecode.data(), "DXBC", 4) == 0,
            "projection asset is not DXBC");
        auto d3d = createWarpDevice();
        ComPtr<ID3D11ComputeShader> shader;
        requireSucceeded(
            d3d.device->CreateComputeShader(
                bytecode.data(),
                bytecode.size(),
                nullptr,
                &shader),
            "CreateComputeShader");

        constexpr Float4 kColor{ 0.25f, 0.5f, 0.75f, 1.0f };
        const auto projected = projectConstantCubemap(
            *d3d.device.Get(),
            *d3d.context.Get(),
            *shader.Get(),
            kColor);
        require(
            community_shaders::ibl::validDiffuseSH(projected),
            "constant-cubemap projection produced invalid coefficients");
        require(
            community_shaders::ibl::classifyDiffuseSH(projected) ==
                community_shaders::ibl::DiffuseSHState::usable,
            "constant-cubemap projection was not classified as usable");
        constexpr float kIntegratedL0 = 3.544907701811032f;
        const std::array colors{ kColor.x, kColor.y, kColor.z };
        for (std::size_t channel = 0; channel < 3; ++channel) {
            requireNear(
                projected.rgb[channel][0],
                colors[channel] * kIntegratedL0,
                2.0e-4f,
                "L0 channel " + std::to_string(channel));
            for (std::size_t coefficient = 1; coefficient < 4;
                 ++coefficient) {
                requireNear(
                    projected.rgb[channel][coefficient],
                    0.0f,
                    2.0e-4f,
                    "L1 channel " + std::to_string(channel));
            }
        }
        verifyValidityAwareFit();
        verifyAmbientTransform(projected);

        auto invalid = projected;
        invalid.rgb[1][2] = std::numeric_limits<float>::quiet_NaN();
        require(
            !community_shaders::ibl::validDiffuseSH(invalid),
            "non-finite SH coefficients were accepted");
        require(
            community_shaders::ibl::classifyDiffuseSH(invalid) ==
                community_shaders::ibl::DiffuseSHState::invalid,
            "non-finite SH coefficients were not classified as invalid");

        const DiffuseSH black{};
        require(
            community_shaders::ibl::classifyDiffuseSH(black) ==
                community_shaders::ibl::DiffuseSHState::black,
            "zero-radiance SH coefficients were not classified as black");

        auto negativeL0 = projected;
        negativeL0.rgb[0][0] = -0.25f;
        require(
            community_shaders::ibl::classifyDiffuseSH(negativeL0) ==
                community_shaders::ibl::DiffuseSHState::invalid,
            "negative-radiance L0 was accepted");

        auto impossibleDirectional = projected;
        impossibleDirectional.rgb[0][1] =
            impossibleDirectional.rgb[0][0] * 2.0f;
        require(
            community_shaders::ibl::classifyDiffuseSH(
                impossibleDirectional) ==
                community_shaders::ibl::DiffuseSHState::invalid,
            "physically impossible first-order SH was accepted");
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "Usage: IblProjectionTests <repo-root>\n";
        return EXIT_FAILURE;
    }
    try {
        run(std::filesystem::absolute(arguments[1]));
        std::cout <<
            "FO4VR diffuse IBL projection verified on D3D11 WARP\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
