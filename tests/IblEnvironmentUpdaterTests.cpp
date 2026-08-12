#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblEnvironmentUpdater.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using community_shaders::ibl::EnvironmentUpdateConsumeResult;
    using community_shaders::ibl::EnvironmentProvider;
    using community_shaders::ibl::EnvironmentProviderState;
    using community_shaders::ibl::EnvironmentUpdater;

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

    struct Device
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    [[nodiscard]] Device createDevice()
    {
        Device result;
        constexpr std::array levels{ D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selected{};
        requireSucceeded(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                levels.data(),
                static_cast<UINT>(levels.size()),
                D3D11_SDK_VERSION,
                &result.device,
                &selected,
                &result.context),
            "D3D11CreateDevice(WARP)");
        require(
            selected == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");
        return result;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createPackedRadiance(
        ID3D11Device& device)
    {
        constexpr UINT width = 32;
        constexpr UINT height = 16;
        constexpr std::uint32_t redOne = 15U << 6;
        constexpr std::uint32_t blueOne = (15U << 5) << 22;
        std::array<std::uint32_t, width * height> pixels{};
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                pixels[y * width + x] = x < width / 2 ? redOne : blueOne;
            }
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{
            pixels.data(),
            width * sizeof(std::uint32_t),
            0,
        };
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(radiance)");
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView(radiance)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createPackedDepth(
        ID3D11Device& device)
    {
        constexpr UINT width = 32;
        constexpr UINT height = 16;
        constexpr std::uint32_t halfDepth = 0x007FFFFFU;
        std::array<std::uint32_t, width * height> pixels{};
        pixels.fill(halfDepth);
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R24G8_TYPELESS;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{
            pixels.data(),
            width * sizeof(std::uint32_t),
            0,
        };
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(depth)");
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(depth)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11Buffer> createSceneConstants(
        ID3D11Device& device,
        float forwardSign = 1.0F)
    {
        std::array<Float4, 85> rows{};
        for (std::size_t eye = 0; eye < 2; ++eye) {
            const auto base = 63 + eye * 4;
            rows[base] = { 1.0f, 0.0f, 0.0f, 0.0f };
            rows[base + 1] = { 0.0f, 1.0f, 0.0f, 0.0f };
            rows[base + 2] = { 0.0f, 0.0f, 1.0f, 0.0f };
            rows[base + 3] = { 0.0f, 0.0f, forwardSign, 0.0f };
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(rows));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA initial{ rows.data(), 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        requireSucceeded(
            device.CreateBuffer(&description, &initial, &buffer),
            "CreateBuffer(scene constants)");
        return buffer;
    }

    void run(const std::filesystem::path& root)
    {
        const auto bytecode = readFile(
            root / "package" / "Shaders" / "Community" / "IBL" /
            "UpdateEnvironmentCS.dxbc");
        require(
            bytecode.size() >= 20 &&
                std::memcmp(bytecode.data(), "DXBC", 4) == 0,
            "environment update asset is not DXBC");
        const auto filterBytecode = readFile(
            root / "package" / "Shaders" / "Community" / "IBL" /
            "FilterEnvironmentCS.dxbc");
        require(
            filterBytecode.size() >= 20 &&
                std::memcmp(filterBytecode.data(), "DXBC", 4) == 0,
            "environment filter asset is not DXBC");

        auto d3d = createDevice();
        EnvironmentProvider provider;
        require(
            provider.initialize(d3d.device.Get(), 16),
            "provider initialization failed");
        EnvironmentUpdater updater;
        require(
            updater.initialize(
                d3d.device.Get(),
                bytecode.data(),
                bytecode.size(),
                filterBytecode.data(),
                filterBytecode.size(),
                16),
            "updater initialization failed");

        const auto radiance = createPackedRadiance(*d3d.device.Get());
        const auto depth = createPackedDepth(*d3d.device.Get());
        const auto constants = createSceneConstants(*d3d.device.Get());
        require(
            updater.dispatchUpdate(
                d3d.context.Get(),
                provider,
                radiance.Get(),
                depth.Get(),
                constants.Get(),
                false),
            "environment update dispatch failed");
        const auto providerAfterDispatch = provider.snapshot();
        require(
            providerAfterDispatch.state == EnvironmentProviderState::updating,
            "private generation was not retained pending validation");
        require(
            providerAfterDispatch.publishedGeneration == 0 &&
                provider.publishedEnvironment() == nullptr &&
                provider.publishedValidity() == nullptr,
            "unvalidated generation escaped into publication");

        d3d.context->Flush();
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        EnvironmentUpdateConsumeResult result{};
        do {
            result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::pending) {
                require(
                    std::chrono::steady_clock::now() < deadline,
                    "timed out waiting for environment validation");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (result == EnvironmentUpdateConsumeResult::pending);
        require(
            result == EnvironmentUpdateConsumeResult::completed,
            "environment validation did not complete");

        const auto summary = updater.snapshot();
        require(!summary.pending, "published update remained pending");
        require(
            summary.dispatches == 1 && summary.publishedUpdates == 1 &&
                summary.completedReadbacks == 1 &&
                summary.failedUpdates == 0,
            "environment update counters changed");
        require(
            summary.sampleCount == 16 * 16 * 6,
            "update did not stage every mip-zero cube face");
        require(
            summary.coveredSamples > 0 && summary.averageValidity > 0.0f,
            "published pair contained no directional validity");
        require(
            summary.nonBlackSamples > 0,
            "projected cube contained no stereo radiance");
        require(
            summary.peak > 0.49f && summary.peak < 0.51f,
            "stereo merge no longer averages both synthetic eyes");
        require(
            summary.faceAverageLuminance[4] >
                summary.faceAverageLuminance[5] + 0.1f,
            "world-space +Z/-Z cube orientation changed");
        require(
            std::abs(summary.average.x - summary.average.z) < 0.01f,
            "left/right packed eyes were not merged symmetrically");
        require(
            provider.snapshot().state == EnvironmentProviderState::ready &&
                provider.snapshot().publishedGeneration == 1 &&
                provider.publishedEnvironment() != nullptr &&
                provider.publishedValidity() != nullptr,
            "validated radiance/validity pair was not atomically published");
        require(
            updater.consumeUpdate(d3d.context.Get(), provider) ==
                EnvironmentUpdateConsumeResult::idle,
            "completed readback was consumed twice");

        const auto firstCoveredSamples = summary.coveredSamples;
        const auto oppositeConstants = createSceneConstants(
            *d3d.device.Get(),
            -1.0F);
        require(
            updater.dispatchUpdate(
                d3d.context.Get(),
                provider,
                radiance.Get(),
                depth.Get(),
                oppositeConstants.Get(),
                true),
            "history-backed environment update dispatch failed");
        d3d.context->Flush();
        const auto secondDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::pending) {
                require(
                    std::chrono::steady_clock::now() < secondDeadline,
                    "timed out waiting for history validation");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (result == EnvironmentUpdateConsumeResult::pending);
        require(
            result == EnvironmentUpdateConsumeResult::completed,
            "history validation did not complete");

        const auto accumulated = updater.snapshot();
        require(accumulated.historyUsed, "published history was not consumed");
        require(
            accumulated.dispatches == 2 &&
                accumulated.publishedUpdates == 2 &&
                accumulated.completedReadbacks == 2 &&
                accumulated.failedUpdates == 0,
            "history update counters changed");
        require(
            accumulated.coveredSamples > firstCoveredSamples * 3 / 2,
            "opposite view did not accumulate directional coverage");
        require(
            accumulated.faceAverageLuminance[4] > 0.01F &&
                accumulated.faceAverageLuminance[5] > 0.01F,
            "temporal history did not retain both opposite cube faces");
        require(
            provider.snapshot().publishedGeneration == 2,
            "history-backed pair was not atomically published");
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "Usage: IblEnvironmentUpdaterTests <repo-root>\n";
        return EXIT_FAILURE;
    }
    try {
        run(std::filesystem::absolute(arguments[1]));
        std::cout <<
            "FO4VR stereo environment updater verified on D3D11 WARP\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "IBL environment updater tests failed: " <<
            exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
