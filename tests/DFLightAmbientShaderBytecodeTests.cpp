#include "Features/linear_lighting/DFLightAmbientShaderPatch.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
    bool expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

    bool readBytecode(
        const std::filesystem::path& path,
        std::vector<std::byte>& bytecode)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        const auto length = stream ? stream.tellg() : std::streampos{ -1 };
        bytecode.resize(
            length > 0 ? static_cast<std::size_t>(length) : 0);
        if (!bytecode.empty()) {
            stream.seekg(0);
            stream.read(
                reinterpret_cast<char*>(bytecode.data()),
                static_cast<std::streamsize>(bytecode.size()));
        }
        return stream && !bytecode.empty();
    }
}

int main(int argc, char** argv)
{
    using namespace community_shaders::linear_lighting;
    if (argc != 2) {
        std::cerr << "expected repository root\n";
        return 1;
    }

    const auto shaderDirectory = std::filesystem::path(argv[1]) /
        "package/Shaders/Community/VerifiedDFLightAmbient";
    const auto shaderPath = shaderDirectory /
        "DFLightAmbient_1d065bd1_2912.dxbc";
    bool passed = true;
    std::size_t verifiedFixtures{};
    for (const auto& entry :
         std::filesystem::directory_iterator(shaderDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dxbc") {
            continue;
        }
        std::vector<std::byte> fixture;
        passed &= expect(
            readBytecode(entry.path(), fixture),
            "verified ambient DXBC fixture could not be read");
        const auto original = fixture;
        passed &= expect(
            recomputeDxbcChecksum(fixture),
            "verified ambient DXBC checksum could not be recomputed");
        passed &= expect(
            fixture == original,
            "recomputed ambient DXBC checksum differs from the compiler checksum");
        ++verifiedFixtures;
    }
    passed &= expect(
        verifiedFixtures == 39,
        "expected 39 verified ambient DXBC fixtures");

    std::vector<std::byte> bytecode;
    passed &= expect(
        readBytecode(shaderPath, bytecode) && bytecode.size() == 2912,
        "verified ambient DXBC fixture could not be read");
    if (!passed) {
        return 1;
    }

    DFLightAmbientGammaOffsets offsets{};
    std::size_t count{};
    constexpr auto vanillaBits = std::bit_cast<std::uint32_t>(
        kVanillaDFLightAmbientShaderGamma);
    for (std::size_t offset = 0;
         offset + sizeof(std::uint32_t) <= bytecode.size();
         ++offset) {
        std::uint32_t observed{};
        std::memcpy(&observed, bytecode.data() + offset, sizeof(observed));
        if (observed == vanillaBits) {
            if (count >= offsets.size()) {
                std::cerr << "ambient DXBC contains extra gamma literals\n";
                return 1;
            }
            offsets[count++] = static_cast<std::uint32_t>(offset);
        }
    }
    passed &= expect(
        count == offsets.size(),
        "ambient DXBC did not expose exactly six gamma literals");

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    const std::array requestedFeatureLevels{ D3D_FEATURE_LEVEL_11_0 };
    auto result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &selectedFeatureLevel,
        context.GetAddressOf());
    passed &= expect(
        SUCCEEDED(result) && selectedFeatureLevel == D3D_FEATURE_LEVEL_11_0,
        "D3D11 WARP device creation failed");
    if (!passed) {
        return 1;
    }

    Microsoft::WRL::ComPtr<ID3D11PixelShader> original;
    result = device->CreatePixelShader(
        bytecode.data(),
        bytecode.size(),
        nullptr,
        original.GetAddressOf());
    passed &= expect(
        SUCCEEDED(result),
        "verified vanilla ambient DXBC was rejected by D3D11 WARP");
    passed &= expect(
        patchDFLightAmbientGamma(bytecode, offsets, 1.8f),
        "verified ambient DXBC gamma patch failed");

    Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement;
    result = device->CreatePixelShader(
        bytecode.data(),
        bytecode.size(),
        nullptr,
        replacement.GetAddressOf());
    passed &= expect(
        SUCCEEDED(result),
        "gamma-patched ambient DXBC was rejected by D3D11 WARP");
    return passed ? 0 : 1;
}
