#include <Windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    [[nodiscard]] std::vector<char> readBytes(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(stream.good(), "could not open " + path.string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>(),
        };
    }

    [[nodiscard]] ComPtr<ID3D11ShaderReflection> reflect(
        const std::vector<char>& bytes,
        const char* name)
    {
        require(bytes.size() >= 20 && bytes[0] == 'D' && bytes[1] == 'X' &&
                bytes[2] == 'B' && bytes[3] == 'C',
            std::string(name) + " is not DXBC");
        ComPtr<ID3D11ShaderReflection> reflection;
        require(SUCCEEDED(D3DReflect(
                    bytes.data(),
                    bytes.size(),
                    __uuidof(ID3D11ShaderReflection),
                    reinterpret_cast<void**>(reflection.GetAddressOf()))) &&
                reflection,
            std::string("D3DReflect rejected ") + name);
        return reflection;
    }

    void requireBinding(
        ID3D11ShaderReflection* reflection,
        const char* name,
        D3D_SHADER_INPUT_TYPE type,
        UINT slot)
    {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        require(SUCCEEDED(
                    reflection->GetResourceBindingDescByName(name, &binding)),
            std::string("missing shader binding ") + name);
        require(binding.Type == type && binding.BindPoint == slot,
            std::string("wrong shader binding ") + name);
    }
}

int main(int argumentCount, char** arguments)
{
    try {
        require(argumentCount == 4,
            "expected qualifier, resolve, and composite shader paths");
        const auto qualifierBytes = readBytes(arguments[1]);
        const auto resolveBytes = readBytes(arguments[2]);
        const auto compositeBytes = readBytes(arguments[3]);
        const auto qualifier = reflect(qualifierBytes, "qualifier");
        const auto resolve = reflect(resolveBytes, "resolve");
        const auto composite = reflect(compositeBytes, "composite");

        UINT threadWidth{};
        UINT threadHeight{};
        UINT threadDepth{};
        qualifier->GetThreadGroupSize(
            &threadWidth, &threadHeight, &threadDepth);
        require(threadWidth == 8 && threadHeight == 8 && threadDepth == 1,
            "qualifier thread group is not 8x8x1");
        requireBinding(qualifier.Get(), "StructuredGlare",
            D3D_SIT_TEXTURE, 0);
        requireBinding(qualifier.Get(), "FilteredGlare",
            D3D_SIT_TEXTURE, 1);
        requireBinding(qualifier.Get(), "ReceiverDepth",
            D3D_SIT_TEXTURE, 2);
        requireBinding(qualifier.Get(), "ProbeOutput",
            D3D_SIT_UAV_RWSTRUCTURED, 0);
        requireBinding(qualifier.Get(), "ImageSpaceConstants",
            D3D_SIT_CBUFFER, 0);

        D3D11_SHADER_DESC resolveDescription{};
        require(SUCCEEDED(resolve->GetDesc(&resolveDescription)) &&
                resolveDescription.OutputParameters == 2,
            "resolve shader lost glare/depth MRT output");
        requireBinding(resolve.Get(), "SceneDepth", D3D_SIT_TEXTURE, 0);
        requireBinding(resolve.Get(), "DirectionalShadow",
            D3D_SIT_TEXTURE, 1);
        requireBinding(resolve.Get(), "IntegratedVolume",
            D3D_SIT_TEXTURE, 2);
        requireBinding(resolve.Get(), "ShadowComparison",
            D3D_SIT_SAMPLER, 0);
        requireBinding(resolve.Get(), "VolumeSampler", D3D_SIT_SAMPLER, 1);

        D3D11_SHADER_DESC compositeDescription{};
        require(SUCCEEDED(composite->GetDesc(&compositeDescription)) &&
                compositeDescription.OutputParameters == 1,
            "composite shader lost its HDR output");
        requireBinding(composite.Get(), "FilteredVolume",
            D3D_SIT_TEXTURE, 0);
        requireBinding(composite.Get(), "ReceiverDepth",
            D3D_SIT_TEXTURE, 1);
        requireBinding(composite.Get(), "FullDepth", D3D_SIT_TEXTURE, 2);
        requireBinding(composite.Get(), "SceneColor", D3D_SIT_TEXTURE, 3);
        requireBinding(composite.Get(), "StructuredVolume",
            D3D_SIT_TEXTURE, 4);
        requireBinding(composite.Get(), "ImageSpaceConstants",
            D3D_SIT_CBUFFER, 0);
        requireBinding(composite.Get(), "VolumetricFrame",
            D3D_SIT_CBUFFER, 1);

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL selected{};
        constexpr D3D_FEATURE_LEVEL requested{ D3D_FEATURE_LEVEL_11_0 };
        require(SUCCEEDED(D3D11CreateDevice(
                    nullptr,
                    D3D_DRIVER_TYPE_WARP,
                    nullptr,
                    0,
                    &requested,
                    1,
                    D3D11_SDK_VERSION,
                    &device,
                    &selected,
                    &context)) &&
                selected == D3D_FEATURE_LEVEL_11_0,
            "D3D11CreateDevice(WARP)");
        ComPtr<ID3D11ComputeShader> qualifierShader;
        ComPtr<ID3D11PixelShader> resolveShader;
        ComPtr<ID3D11PixelShader> compositeShader;
        require(SUCCEEDED(device->CreateComputeShader(
                    qualifierBytes.data(), qualifierBytes.size(), nullptr,
                    &qualifierShader)) &&
                qualifierShader,
            "WARP rejected qualifier shader");
        require(SUCCEEDED(device->CreatePixelShader(
                    resolveBytes.data(), resolveBytes.size(), nullptr,
                    &resolveShader)) &&
                resolveShader,
            "WARP rejected resolve shader");
        require(SUCCEEDED(device->CreatePixelShader(
                    compositeBytes.data(), compositeBytes.size(), nullptr,
                    &compositeShader)) &&
                compositeShader,
            "WARP rejected composite shader");

        std::cout <<
            "Volumetric qualifier, detailed resolve, and bounded composite contracts accepted by reflection and WARP.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
