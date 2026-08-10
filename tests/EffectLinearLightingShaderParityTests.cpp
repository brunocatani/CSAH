#include "Features/linear_lighting/LinearLightingSettings.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using community_shaders::linear_lighting::FrameData;
    using community_shaders::linear_lighting::Settings;
    using community_shaders::linear_lighting::makeFrameData;

    using Pixel = std::array<float, 4>;

    constexpr float kTolerance = 1.0e-4F;
    constexpr Pixel kBaseColor{ 0.42F, 0.63F, 0.31F, 0.77F };
    constexpr Pixel kPropertyColor{ 0.8F, 0.45F, 0.7F, 0.9F };
    constexpr Pixel kFogParam{ 0.65F, 0.8F, 0.45F, 0.7F };
    constexpr Pixel kTextureColor{ 0.2F, 0.4F, 0.6F, 0.3F };
    constexpr Pixel kVertexColor{ 0.55F, 0.75F, 0.35F, 0.6F };
    constexpr float kLightingInfluence = 0.35F;

    struct EffectContract
    {
        const char* name;
        bool vertexColored;
        bool textured;
        bool additive;
        bool multiplyBlend;
        bool premultipliedAlpha;
        bool particle;
    };

    constexpr std::array<EffectContract, 28> kEffectContracts{ {
        { "EffectDefault_00000000", false, false, false, false, false, false },
        { "EffectVertexColor_00000001", true, false, false, false, false, false },
        { "EffectTextured_00000004", false, true, false, false, false, false },
        { "EffectVertexColorTextured_00000005", true, true, false, false, false, false },
        { "EffectAdditive_00000020", false, false, true, false, false, false },
        { "EffectVertexColorAdditive_00000021", true, false, true, false, false, false },
        { "EffectTexturedAdditive_00000024", false, true, true, false, false, false },
        { "EffectVertexColorTexturedAdditive_00000025", true, true, true, false, false, false },
        { "EffectMultiplyBlend_00000040", false, false, false, true, false, false },
        { "EffectVertexColorMultiplyBlend_00000041", true, false, false, true, false, false },
        { "EffectTexturedMultiplyBlend_00000044", false, true, false, true, false, false },
        { "EffectVertexColorTexturedMultiplyBlend_00000045", true, true, false, true, false, false },
        { "EffectVertexColorParticle_00000081", true, false, false, false, false, true },
        { "EffectVertexColorTexturedParticle_00000085", true, true, false, false, false, true },
        { "EffectTexturedParticle_0000008C", false, true, false, false, false, true },
        { "EffectVertexColorTexturedAdditiveParticle_000000A5", true, true, true, false, false, true },
        { "EffectPremultipliedAlpha_40000000", false, false, false, false, true, false },
        { "EffectVertexColorPremultipliedAlpha_40000001", true, false, false, false, true, false },
        { "EffectTexturedPremultipliedAlpha_40000004", false, true, false, false, true, false },
        { "EffectVertexColorTexturedPremultipliedAlpha_40000005", true, true, false, false, true, false },
        { "EffectAdditivePremultipliedAlpha_40000020", false, false, true, false, true, false },
        { "EffectVertexColorAdditivePremultipliedAlpha_40000021", true, false, true, false, true, false },
        { "EffectTexturedAdditivePremultipliedAlpha_40000024", false, true, true, false, true, false },
        { "EffectVertexColorTexturedAdditivePremultipliedAlpha_40000025", true, true, true, false, true, false },
        { "EffectVertexColorTexturedMultiplyBlendPremultipliedAlpha_40000045", true, true, false, true, true, false },
        { "EffectVertexColorTexturedParticlePremultipliedAlpha_40000085", true, true, false, false, true, true },
        { "EffectVertexColorTexturedAdditiveParticlePremultipliedAlpha_400000A5", true, true, true, false, true, true },
        { "EffectTexturedMultiplyBlendPremultipliedAlpha_50000044", false, true, false, true, true, false },
    } };

    struct alignas(16) EffectPerMaterial
    {
        Pixel baseColor{};
        Pixel unused{};
        Pixel lightingInfluence{};
    };

    struct alignas(16) EffectPerGeometry
    {
        std::array<Pixel, 20> unused{};
        Pixel propertyColor{};
        Pixel alphaTest{};
    };

    struct RenderTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11Texture2D> staging;
    };

    void require(HRESULT result, const char* operation)
    {
        if (FAILED(result)) {
            throw std::runtime_error(
                std::string(operation) + " failed with HRESULT " +
                std::to_string(static_cast<std::uint32_t>(result)));
        }
    }

    std::vector<std::byte> readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("could not open " + path.string());
        }
        const auto size = stream.tellg();
        if (size <= 0) {
            throw std::runtime_error("empty shader " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("could not read " + path.string());
        }
        return bytes;
    }

    template <class T>
    ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device* device,
        const T& value)
    {
        static_assert(sizeof(T) % 16 == 0);
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(T));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{ &value, 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        require(
            device->CreateBuffer(&description, &initial, buffer.GetAddressOf()),
            "CreateBuffer");
        return buffer;
    }

    ComPtr<ID3D11VertexShader> createVertexShader(
        ID3D11Device* device,
        bool vertexColored,
        bool particle)
    {
        constexpr char source[] = R"(
struct VSOutput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#ifdef EFFECT_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float4 color : COLOR1;
#ifdef EFFECT_PARTICLE
    float3 particleData : TEXCOORD5;
#endif
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    output.texCoord = float4(0.5, 0.5, 0.0, 0.0);
#ifdef EFFECT_VERTEX_COLOR
    output.vertexColor = float4(0.55, 0.75, 0.35, 0.6);
#endif
    output.color = float4(0.65, 0.8, 0.45, 0.7);
#ifdef EFFECT_PARTICLE
    output.particleData = float3(0.2, 0.4, 0.6);
#endif
    output.eyeIndex = 0;
    output.cullDistance = 1.0;
    output.clipDistance = 1.0;
    return output;
}
)";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        std::array<D3D_SHADER_MACRO, 3> macros{};
        std::size_t macroCount = 0;
        if (vertexColored) {
            macros[macroCount++] = { "EFFECT_VERTEX_COLOR", "1" };
        }
        if (particle) {
            macros[macroCount++] = { "EFFECT_PARTICLE", "1" };
        }
        const auto result = D3DCompile(
            source,
            sizeof(source) - 1,
            "EffectLinearLightingParityVS",
            macroCount != 0 ? macros.data() : nullptr,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(result)) {
            const std::string detail = errors ?
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()) :
                "no compiler diagnostics";
            throw std::runtime_error("vertex shader compile failed: " + detail);
        }
        ComPtr<ID3D11VertexShader> shader;
        require(
            device->CreateVertexShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                shader.GetAddressOf()),
            "CreateVertexShader");
        return shader;
    }

    ComPtr<ID3D11PixelShader> createPixelShader(
        ID3D11Device* device,
        const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytes.data(), bytes.size(), nullptr, shader.GetAddressOf()),
            "CreatePixelShader");
        return shader;
    }

    ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device* device,
        const Pixel& pixel)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ pixel.data(), sizeof(Pixel), 0 };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView");
        return view;
    }

    RenderTarget createRenderTarget(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;

        RenderTarget target;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.texture.GetAddressOf()),
            "CreateTexture2D(render target)");
        require(
            device->CreateRenderTargetView(
                target.texture.Get(), nullptr, target.view.GetAddressOf()),
            "CreateRenderTargetView");

        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.staging.GetAddressOf()),
            "CreateTexture2D(staging)");
        return target;
    }

    Pixel render(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* materialBuffer,
        ID3D11Buffer* geometryBuffer,
        ID3D11Buffer* frameBuffer,
        ID3D11ShaderResourceView* texture,
        ID3D11SamplerState* sampler)
    {
        auto target = createRenderTarget(device);
        const float clear[4]{};
        context->ClearRenderTargetView(target.view.Get(), clear);
        ID3D11RenderTargetView* renderTarget = target.view.Get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        const D3D11_VIEWPORT viewport{ 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        context->PSSetConstantBuffers(2, 1, &geometryBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->PSSetShaderResources(0, 1, &texture);
        context->PSSetSamplers(0, 1, &sampler);
        context->Draw(3, 0);

        context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(
            context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(render target)");
        Pixel result{};
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(target.staging.Get(), 0);

        ID3D11ShaderResourceView* nullTexture{};
        context->PSSetShaderResources(0, 1, &nullTexture);
        ID3D11RenderTargetView* nullTarget{};
        context->OMSetRenderTargets(1, &nullTarget, nullptr);
        return result;
    }

    bool nearValue(float actual, float expected)
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <=
                kTolerance * std::max(1.0F, std::abs(expected));
    }

    bool compare(
        const Pixel& actual,
        const Pixel& expected,
        const std::string& label)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel) {
            if (!nearValue(actual[channel], expected[channel])) {
                std::cerr << label << " channel " << channel << " expected "
                          << expected[channel] << " but got " << actual[channel]
                          << '\n';
                return false;
            }
        }
        return true;
    }

    Pixel expectedVanilla(const EffectContract& contract)
    {
        Pixel result{};
        const auto alpha = kBaseColor[3] *
            (contract.vertexColored ?
                    std::pow(kVertexColor[3], 2.2F) :
                    1.0F) *
            (contract.textured ? kTextureColor[3] : 1.0F) *
            kPropertyColor[3];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto baseColor = kBaseColor[channel] *
                (contract.vertexColored ?
                        std::pow(kVertexColor[channel], 2.2F) :
                        1.0F) *
                (contract.textured ? kTextureColor[channel] : 1.0F);
            const auto lightColor = baseColor +
                kLightingInfluence *
                    (kPropertyColor[channel] * baseColor - baseColor);
            if (contract.additive) {
                result[channel] = lightColor * (1.0F - kFogParam[3]);
            } else if (contract.multiplyBlend) {
                const auto fogFactor =
                    std::clamp(1.5F * kFogParam[3], 0.0F, 1.0F);
                const auto foggedColor =
                    lightColor + fogFactor * (1.0F - lightColor);
                result[channel] = 1.0F + alpha * (foggedColor - 1.0F);
            } else {
                result[channel] = lightColor +
                    kFogParam[3] * (kFogParam[channel] - lightColor);
            }
            if (contract.premultipliedAlpha) {
                result[channel] *= alpha;
            }
        }
        result[3] = alpha;
        return result;
    }

    Pixel expectedEnabled(
        const EffectContract& contract,
        const Settings& settings)
    {
        Pixel result{};
        const auto rawAlpha = kBaseColor[3] *
            (contract.vertexColored ? kVertexColor[3] : 1.0F) *
            (contract.textured ? kTextureColor[3] : 1.0F) *
            kPropertyColor[3];
        const auto outputAlpha =
            std::pow(std::abs(rawAlpha), settings.effectAlphaGamma);
        const auto fogFactor =
            std::pow(std::abs(kFogParam[3]), settings.fogAlphaGamma);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            auto base =
                std::pow(std::abs(kBaseColor[channel]), settings.effectGamma);
            if (contract.vertexColored) {
                base *= std::pow(
                    std::abs(kVertexColor[channel]), settings.effectGamma);
            }
            if (contract.textured) {
                base *= std::pow(
                    std::abs(kTextureColor[channel]), settings.effectGamma);
            }
            const auto property = std::pow(
                std::abs(kPropertyColor[channel]), settings.effectGamma);
            const auto lightColor =
                (base + kLightingInfluence * (property * base - base)) *
                settings.otherEffectMultiplier;
            if (contract.additive) {
                result[channel] = lightColor * (1.0F - fogFactor);
            } else if (contract.multiplyBlend) {
                const auto multiplyFogFactor =
                    std::clamp(1.5F * fogFactor, 0.0F, 1.0F);
                const auto foggedColor = lightColor +
                    multiplyFogFactor * (1.0F - lightColor);
                result[channel] =
                    1.0F + outputAlpha * (foggedColor - 1.0F);
            } else {
                const auto fogColor =
                    std::pow(std::abs(kFogParam[channel]), settings.fogGamma);
                result[channel] = lightColor +
                    fogFactor * (fogColor - lightColor);
            }
            if (contract.premultipliedAlpha) {
                result[channel] *= outputAlpha;
            }
        }
        result[3] = outputAlpha;
        return result;
    }

    int run(const std::filesystem::path& root)
    {
        D3D_FEATURE_LEVEL featureLevel{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &featureLevel,
                context.GetAddressOf()),
            "D3D11CreateDevice(WARP)");
        if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
            throw std::runtime_error("WARP did not provide feature level 11");
        }

        EffectPerMaterial materialConstants{};
        materialConstants.baseColor = kBaseColor;
        materialConstants.lightingInfluence[0] = kLightingInfluence;
        EffectPerGeometry geometryConstants{};
        geometryConstants.propertyColor = kPropertyColor;
        geometryConstants.alphaTest = { 0.01F, 0.8F, 0.0F, 0.0F };
        const auto materialBuffer =
            createConstantBuffer(device.Get(), materialConstants);
        const auto geometryBuffer =
            createConstantBuffer(device.Get(), geometryConstants);

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        enabledSettings.effectGamma = 1.65F;
        enabledSettings.effectAlphaGamma = 1.3F;
        enabledSettings.fogGamma = 1.85F;
        enabledSettings.fogAlphaGamma = 1.45F;
        enabledSettings.otherEffectMultiplier = 1.25F;
        const FrameData disabledFrame =
            makeFrameData(disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame =
            makeFrameData(enabledSettings, true, false, 1.0F);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);

        const auto texture = createTexture(device.Get(), kTextureColor);
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device->CreateSamplerState(
                &samplerDescription, sampler.GetAddressOf()),
            "CreateSamplerState");

        const auto defaultVertexShader =
            createVertexShader(device.Get(), false, false);
        const auto vertexColorVertexShader =
            createVertexShader(device.Get(), true, false);
        const auto particleVertexShader =
            createVertexShader(device.Get(), false, true);
        const auto vertexColorParticleVertexShader =
            createVertexShader(device.Get(), true, true);
        bool passed = true;
        for (const auto& contract : kEffectContracts) {
            auto* vertexShader = contract.particle ?
                (contract.vertexColored ?
                        vertexColorParticleVertexShader.Get() :
                        particleVertexShader.Get()) :
                (contract.vertexColored ?
                        vertexColorVertexShader.Get() :
                        defaultVertexShader.Get());
            const auto vanillaShader = createPixelShader(
                device.Get(),
                root /
                    "package/Shaders/Community/VerifiedEffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));
            const auto replacementShader = createPixelShader(
                device.Get(),
                root / "package/Shaders/Community/EffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));

            const auto vanilla = render(
                device.Get(), context.Get(), vertexShader,
                vanillaShader.Get(), materialBuffer.Get(), geometryBuffer.Get(),
                disabledFrameBuffer.Get(), texture.Get(), sampler.Get());
            const auto disabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), materialBuffer.Get(), geometryBuffer.Get(),
                disabledFrameBuffer.Get(), texture.Get(), sampler.Get());
            const auto enabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), materialBuffer.Get(), geometryBuffer.Get(),
                enabledFrameBuffer.Get(), texture.Get(), sampler.Get());

            const std::string label = contract.name;
            passed &= compare(
                vanilla,
                expectedVanilla(contract),
                label + " vanilla model");
            passed &= compare(
                disabled,
                vanilla,
                label + " disabled parity");
            passed &= compare(
                enabled,
                expectedEnabled(contract, enabledSettings),
                label + " enabled model");
        }
        if (!passed) {
            return 1;
        }
        std::cout <<
            "All twenty-eight Effect Linear Lighting parity and enabled model tests passed.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr <<
            "usage: EffectLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "Effect Linear Lighting parity test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
