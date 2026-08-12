#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
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

    std::vector<std::filesystem::path> shaders(
        const std::filesystem::path& directory)
    {
        std::vector<std::filesystem::path> result;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dxbc") {
                result.push_back(entry.path());
            }
        }
        std::ranges::sort(result);
        return result;
    }

    std::vector<char> readBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(stream.good(), "could not open " + path.string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>(),
        };
    }
}

int main(int argumentCount, char** arguments)
{
    try {
        require(argumentCount == 2, "expected repository root argument");
        const auto root = std::filesystem::path(arguments[1]);
        const auto originals = shaders(
            root / "package/Shaders/Community/VerifiedIBLMaterial");
        const auto replacements = shaders(
            root / "package/Shaders/Community/IBLMaterial");
        require(
            originals.size() == 41 && replacements.size() == 41,
            "IBL material shader family must contain 41 original/replacement pairs");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL selected{};
        constexpr D3D_FEATURE_LEVEL requested{ D3D_FEATURE_LEVEL_11_0 };
        require(
            SUCCEEDED(D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                &requested,
                1,
                D3D11_SDK_VERSION,
                &device,
                &selected,
                &context)),
            "D3D11CreateDevice(WARP)");
        require(
            selected == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");

        for (std::size_t index = 0; index < originals.size(); ++index) {
            require(
                originals[index].filename() == replacements[index].filename(),
                "IBL material pair ordering changed");
            for (const auto& path : { originals[index], replacements[index] }) {
                const auto bytes = readBytes(path);
                require(
                    bytes.size() >= 20 &&
                        bytes[0] == 'D' && bytes[1] == 'X' &&
                        bytes[2] == 'B' && bytes[3] == 'C',
                    "invalid DXBC container: " + path.string());
                ComPtr<ID3D11PixelShader> shader;
                require(
                    SUCCEEDED(device->CreatePixelShader(
                        bytes.data(),
                        bytes.size(),
                        nullptr,
                        &shader)) &&
                        shader,
                    "WARP rejected " + path.string());
            }
        }

        std::cout
            << "FO4VR IBL 41-pair DFComposite bytecode family accepted by D3D11 WARP\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
