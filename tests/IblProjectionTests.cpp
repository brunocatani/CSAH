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

        auto invalid = projected;
        invalid.rgb[1][2] = std::numeric_limits<float>::quiet_NaN();
        require(
            !community_shaders::ibl::validDiffuseSH(invalid),
            "non-finite SH coefficients were accepted");
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
