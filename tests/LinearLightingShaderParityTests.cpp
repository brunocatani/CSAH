#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr UINT kRenderTargetCount = 6;
    constexpr UINT kFloat4Size = sizeof(float) * 4;
    constexpr float kAbsoluteTolerance = 2.0e-5F;
    constexpr float kRelativeTolerance = 2.0e-5F;

    struct ShaderContract
    {
        std::string_view name;
        UINT mrtCount;
        bool hasVertexColor;
        bool isInstanced;
        bool usesTessellatedInputs{};
    };

    constexpr std::array kShaderContracts{
        ShaderContract{ "DefaultProjectedFiveMrt_L4_00008002", 5, false, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L3_00008003", 5, true, false },
        ShaderContract{ "DefaultSixMrt_L4_00000002", 6, false, false },
        ShaderContract{ "DefaultSixMrt_L3_00000003", 6, true, false },
        ShaderContract{ "DefaultModelSpaceSixMrt_L4_00000006", 6, false, false },
        ShaderContract{ "DefaultModelSpaceSixMrt_L3_00000007", 6, true, false },
        ShaderContract{ "DefaultDefShadowSixMrt_L4_00004002", 6, false, false },
        ShaderContract{ "DefaultDefShadowSixMrt_L3_00004003", 6, true, false },
        ShaderContract{ "EnvmapSixMrt_L4_00000102", 6, false, false },
        ShaderContract{ "EnvmapSixMrt_L3_00000103", 6, true, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_L4_00000106", 6, false, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_L3_00000107", 6, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L4_00008102", 5, false, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L3_00008103", 5, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L4_00008106", 5, false, false },
        ShaderContract{ "TexturedEmissionAlphaTestSixMrt_L4_00004102", 6, false, false },
        ShaderContract{ "TexturedEmissionAlphaTestSixMrt_L3_00004103", 6, true, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016", 6, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5", 5, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_VertexColorNoEarlyDepth_B4D2FE98", 5, true, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L4NoEarlyDepth_15E29A6C", 5, false, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L3NoEarlyDepth_B80CA12A", 5, true, false },
        ShaderContract{ "GlowmapSixMrt_L4NoEarlyDepth_00004006", 6, false, false },
        ShaderContract{ "GlowmapSixMrt_L3NoEarlyDepth_00004007", 6, true, false },
        ShaderContract{ "GlowmapAlphaTestSixMrt_L4NoEarlyDepth_00004106", 6, false, false },
        ShaderContract{ "GlowmapAlphaTestSixMrt_L3NoEarlyDepth_00004107", 6, true, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L4_0000C002", 5, false, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L3_0000C003", 5, true, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L4NoEarlyDepth_0000C006", 5, false, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L3NoEarlyDepth_0000C007", 5, true, false },
        ShaderContract{ "GlowmapAlphaTestBlendFiveMrt_L4_0000C102", 5, false, false },
        ShaderContract{ "GlowmapAlphaTestBlendFiveMrt_L3_0000C103", 5, true, false },
        ShaderContract{ "InstancedSixMrt_L4_08000002", 6, false, true },
        ShaderContract{ "InstancedSixMrt_L3_08000003", 6, true, true },
        ShaderContract{ "ModelSpaceNormalsSixMrt_L4_00002002", 6, false, false },
        ShaderContract{ "ModelSpaceNormalsSixMrt_L3_00002003", 6, true, false },
        ShaderContract{ "ModelSpaceNormalsAlphaTestSixMrt_L4_00002102", 6, false, false },
        ShaderContract{ "TessellatedSixMrt_L4_00080002", 6, false, false, true },
        ShaderContract{ "TessellatedSixMrt_L3_00080003", 6, true, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_L4_00080102", 6, false, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_L3_00080103", 6, true, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_RgbOnlyVertexColor_00080503", 6, true, false, true },
        ShaderContract{ "InstancedLandLodBlendSixMrt_L4_0A000002", 6, false, true },
        ShaderContract{ "InstancedLandLodBlendSixMrt_L3_0A000003", 6, true, true },
        ShaderContract{ "ModelSpaceNormalsSkinnedSixMrt_L4_00002006", 6, false, false },
    };

    struct RenderTargets
    {
        std::array<ComPtr<ID3D11Texture2D>, kRenderTargetCount> gpu;
        std::array<ComPtr<ID3D11RenderTargetView>, kRenderTargetCount> views;
        std::array<ComPtr<ID3D11Texture2D>, kRenderTargetCount> staging;
    };

    using Pixel = std::array<float, 4>;
    using RenderResult = std::array<Pixel, kRenderTargetCount>;

    struct LinearLightingCase
    {
        std::string_view name;
        bool enabled;
        float colorGamma;
        float emitColorGamma;
        float glowmapGamma;
        float vanillaDiffuseColorMult;
        float emitColorMult;
        float glowmapMult;
        float emissiveMult;
    };

    constexpr LinearLightingCase kDisabledCase{
        "disabled",
        false,
        2.2F,
        1.8F,
        1.6F,
        1.3F,
        0.75F,
        1.4F,
        3.0F,
    };
    constexpr LinearLightingCase kIdentityCase{
        "enabled-identity",
        true,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        3.0F,
    };
    constexpr LinearLightingCase kTransformedCase{
        "enabled-transformed",
        true,
        2.2F,
        1.8F,
        1.6F,
        1.3F,
        0.75F,
        1.4F,
        3.0F,
    };

    constexpr Pixel kDiffuseTexture{ 0.25F, 0.5F, 0.75F, 0.8F };
    constexpr Pixel kNormalTexture{ 0.35F, 0.65F, 0.2F, 0.8F };
    constexpr Pixel kSpecularTexture{ 0.45F, 0.7F, 0.15F, 0.9F };
    constexpr Pixel kGlowTexture{ 0.6F, 0.4F, 0.2F, 1.0F };
    constexpr Pixel kVertexColor{ 0.8F, 0.7F, 0.6F, 0.9F };
    constexpr Pixel kEmitColor{ 0.3F, 0.45F, 0.6F, 0.2F };
    constexpr Pixel kInstanceEmitColor{ 0.55F, 0.25F, 0.7F, 0.2F };

    constexpr std::array<float, 4> kClearColor{
        123.25F,
        -456.5F,
        789.75F,
        -101.125F,
    };

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(HRESULT result, const std::string& operation)
    {
        if (SUCCEEDED(result)) {
            return;
        }
        fail(operation + " failed with HRESULT 0x" +
            [] (HRESULT value) {
                std::array<char, 16> buffer{};
                (void)std::snprintf(
                    buffer.data(),
                    buffer.size(),
                    "%08X",
                    static_cast<unsigned int>(value));
                return std::string(buffer.data());
            }(result));
    }

    [[nodiscard]] std::vector<std::byte> readFile(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            fail("could not open shader " + path.string());
        }
        const auto end = stream.tellg();
        if (end <= 0) {
            fail("shader is empty: " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        stream.seekg(0, std::ios::beg);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            fail("could not read shader " + path.string());
        }
        return bytes;
    }

    [[nodiscard]] bool usesTextureSlot(
        std::span<const std::byte> bytecode,
        UINT slot)
    {
        ComPtr<ID3DBlob> assembly;
        require(
            D3DDisassemble(
                bytecode.data(),
                bytecode.size_bytes(),
                0,
                nullptr,
                &assembly),
            "D3DDisassemble(pixel shader)");
        const std::string_view text{
            static_cast<const char*>(assembly->GetBufferPointer()),
            assembly->GetBufferSize(),
        };
        const auto expectedRegister = "t" + std::to_string(slot);
        std::size_t lineStart = 0;
        while (lineStart < text.size()) {
            const auto lineEnd = text.find('\n', lineStart);
            auto line = text.substr(
                lineStart,
                lineEnd == std::string_view::npos ?
                    text.size() - lineStart : lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            const auto lastSpace = line.find_last_of(' ');
            if (line.find("dcl_resource_") != std::string_view::npos &&
                lastSpace != std::string_view::npos &&
                line.substr(lastSpace + 1) == expectedRegister) {
                return true;
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return false;
    }

    [[nodiscard]] ComPtr<ID3DBlob> compileVertexShader(
        bool hasVertexColor,
        bool isInstanced,
        bool usesTessellatedInputs)
    {
        constexpr std::string_view source = R"(
struct VSOutput
{
    float4 position : SV_POSITION;
#if USES_TESSELLATED_INPUTS
    float2 uv : TEXCOORD0;
#if HAS_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
#else
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if HAS_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#endif
#if IS_INSTANCED
    nointerpolation uint instanceDataIndex : COLOR2;
#endif
    nointerpolation uint eyeIndex : EYEINDEX;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    output.position = float4(positions[vertexId], 0.5, 1.0);
#if USES_TESSELLATED_INPUTS
    output.uv = float2(0.25, 0.75);
    output.tangent = float3(1.0, 0.0, 0.0);
    output.bitangent = float3(0.0, 1.0, 0.0);
    output.normal = float3(0.0, 0.0, -1.0);
    output.currentPosition = float4(0.2, -0.3, 0.4, 1.0);
    output.previousPosition = float4(0.15, -0.2, 0.35, 1.0);
#else
    output.tangent = float3(1.0, 0.0, 0.0);
    output.bitangent = float3(0.0, 1.0, 0.0);
    output.normal = float3(0.0, 0.0, -1.0);
    output.currentPosition = float4(0.2, -0.3, 0.4, 0.25);
    output.previousPosition = float4(0.15, -0.2, 0.35, 0.75);
#endif
#if HAS_VERTEX_COLOR
    output.vertexColor = float4(0.8, 0.7, 0.6, 0.9);
#endif
#if IS_INSTANCED
    output.instanceDataIndex = 2;
#endif
    output.eyeIndex = 0;
    return output;
}
)";

        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const D3D_SHADER_MACRO macros[]{
            { "HAS_VERTEX_COLOR", hasVertexColor ? "1" : "0" },
            { "IS_INSTANCED", isInstanced ? "1" : "0" },
            { "USES_TESSELLATED_INPUTS",
                usesTessellatedInputs ? "1" : "0" },
            { nullptr, nullptr },
        };
        const auto result = D3DCompile(
            source.data(),
            source.size(),
            "LinearLightingParityVS",
            macros,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0,
            &bytecode,
            &errors);
        if (FAILED(result)) {
            const auto details = errors ? std::string(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize()) : std::string{};
            fail("vertex shader compilation failed: " + details);
        }
        return bytecode;
    }

    [[nodiscard]] ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device& device,
        std::span<const std::array<float, 4>> values)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(values.size_bytes());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = values.data();
        ComPtr<ID3D11Buffer> buffer;
        require(
            device.CreateBuffer(&description, &initial, &buffer),
            "CreateBuffer");
        return buffer;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device& device,
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
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = pixel.data();
        initial.SysMemPitch = kFloat4Size;
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(input)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView");
        return view;
    }

    [[nodiscard]] RenderTargets createRenderTargets(ID3D11Device& device)
    {
        RenderTargets result;
        D3D11_TEXTURE2D_DESC gpuDescription{};
        gpuDescription.Width = 1;
        gpuDescription.Height = 1;
        gpuDescription.MipLevels = 1;
        gpuDescription.ArraySize = 1;
        gpuDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        gpuDescription.SampleDesc.Count = 1;
        gpuDescription.Usage = D3D11_USAGE_DEFAULT;
        gpuDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

        auto stagingDescription = gpuDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            require(
                device.CreateTexture2D(
                    &gpuDescription,
                    nullptr,
                    &result.gpu[index]),
                "CreateTexture2D(render target)");
            require(
                device.CreateRenderTargetView(
                    result.gpu[index].Get(),
                    nullptr,
                    &result.views[index]),
                "CreateRenderTargetView");
            require(
                device.CreateTexture2D(
                    &stagingDescription,
                    nullptr,
                    &result.staging[index]),
                "CreateTexture2D(staging)");
        }
        return result;
    }

    [[nodiscard]] RenderResult readRenderTargets(
        ID3D11DeviceContext& context,
        const RenderTargets& targets)
    {
        RenderResult result{};
        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            context.CopyResource(
                targets.staging[index].Get(),
                targets.gpu[index].Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(
                context.Map(
                    targets.staging[index].Get(),
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped),
                "Map(render target)");
            std::memcpy(
                result[index].data(),
                mapped.pData,
                sizeof(result[index]));
            context.Unmap(targets.staging[index].Get(), 0);
        }
        return result;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 7> makeMaterialData(
        UINT mrtCount,
        std::size_t caseIndex)
    {
        std::array<std::array<float, 4>, 7> values{};
        const auto switchValue = caseIndex == 0 ? 0.0F : 0.35F;
        values[0] = { 0.4F, 0.7F, 0.25F, 0.8F };
        values[1] = { 0.3F, 0.45F, 0.6F, 0.2F };
        values[2] = mrtCount == 5 ?
            std::array<float, 4>{ 0.75F, 1.0F, 0.0F, 0.0F } :
            std::array<float, 4>{ 0.9F, 0.6F, 0.0F, 0.0F };
        values[3] = { 0.85F, 0.55F, -1.0F, 0.0F };
        values[4] = { 1.0F, 1.0F, 0.4F, caseIndex == 2 ? -1.0F : 0.6F };
        values[5] = mrtCount == 5 ?
            values[4] : std::array<float, 4>{
                0.65F,
                switchValue,
                0.2F,
                0.9F,
            };
        values[6] = { 0.65F, switchValue, 0.2F, 0.9F };
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 71> makeGeometryData(
        std::size_t caseIndex)
    {
        std::array<std::array<float, 4>, 71> values{};
        values[50][0] = caseIndex == 0 ? 0.0F :
            (caseIndex == 1 ? 0.35F : 0.8F);
        values[51] = { 1.0F, 0.0F, 0.0F, 0.0F };
        values[52] = { 0.0F, 1.0F, 0.0F, 0.0F };
        values[54] = { 0.0F, 0.0F, 0.0F, 1.0F };
        values[63] = { 1.0F, 0.0F, 0.0F, 0.0F };
        values[64] = { 0.0F, 1.0F, 0.0F, 0.0F };
        values[66] = { 0.0F, 0.0F, 0.0F, 1.0F };
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 900> makeInstanceData()
    {
        std::array<std::array<float, 4>, 900> values{};
        constexpr std::size_t instanceBase = 2 * 6;
        values[instanceBase + 4] = { 0.4F, 0.7F, 0.65F, 0.8F };
        values[instanceBase + 5] = kInstanceEmitColor;
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 7> makeFrameData(
        const LinearLightingCase& lightingCase)
    {
        const auto uintAsFloat = [] (std::uint32_t value) {
            return std::bit_cast<float>(value);
        };
        return {
            std::array<float, 4>{
                uintAsFloat(lightingCase.enabled ? 1u : 0u),
                uintAsFloat(1u),
                2.5F,
                1.9F,
            },
            std::array<float, 4>{
                lightingCase.colorGamma,
                lightingCase.emitColorGamma,
                lightingCase.glowmapGamma,
                1.7F,
            },
            std::array<float, 4>{ 1.6F, 1.5F, 1.4F, 1.3F },
            std::array<float, 4>{
                1.2F,
                1.1F,
                1.05F,
                lightingCase.vanillaDiffuseColorMult,
            },
            std::array<float, 4>{
                0.85F,
                0.75F,
                0.65F,
                lightingCase.emitColorMult,
            },
            std::array<float, 4>{
                lightingCase.glowmapMult,
                0.35F,
                0.25F,
                0.15F,
            },
            std::array<float, 4>{ 0.05F, 0.0F, 0.0F, 0.0F },
        };
    }

    [[nodiscard]] RenderResult render(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        ID3D11VertexShader& vertexShader,
        ID3D11PixelShader& pixelShader,
        UINT mrtCount,
        bool isInstanced,
        std::size_t caseIndex,
        const LinearLightingCase& lightingCase)
    {
        const auto materialData = makeMaterialData(mrtCount, caseIndex);
        const auto geometryData = makeGeometryData(caseIndex);
        const auto instanceData = makeInstanceData();
        const auto frameData = makeFrameData(lightingCase);
        const std::array<std::array<float, 4>, 1> linearGeometryData{
            std::array<float, 4>{
                lightingCase.emissiveMult,
                0.0F,
                0.0F,
                0.0F,
            },
        };
        const auto materialBuffer = createConstantBuffer(device, materialData);
        const auto geometryBuffer = createConstantBuffer(device, geometryData);
        const auto instanceBuffer = createConstantBuffer(device, instanceData);
        const auto frameBuffer = createConstantBuffer(device, frameData);
        const auto linearGeometryBuffer = createConstantBuffer(
            device,
            linearGeometryData);

        const std::array<Pixel, 4> texturePixels{
            kDiffuseTexture,
            kNormalTexture,
            kSpecularTexture,
            kGlowTexture,
        };
        std::array<ComPtr<ID3D11ShaderResourceView>, 4> textureViews;
        std::array<ID3D11ShaderResourceView*, 4> rawTextureViews{};
        for (std::size_t index = 0; index < textureViews.size(); ++index) {
            textureViews[index] = createTexture(device, texturePixels[index]);
            rawTextureViews[index] = textureViews[index].Get();
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device.CreateSamplerState(&samplerDescription, &sampler),
            "CreateSamplerState");
        const std::array<ID3D11SamplerState*, 4> samplers{
            sampler.Get(),
            sampler.Get(),
            sampler.Get(),
            sampler.Get(),
        };

        auto targets = createRenderTargets(device);
        std::array<ID3D11RenderTargetView*, kRenderTargetCount> rawTargets{};
        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            rawTargets[index] = targets.views[index].Get();
            context.ClearRenderTargetView(
                rawTargets[index],
                kClearColor.data());
        }

        constexpr D3D11_VIEWPORT viewport{
            0.0F,
            0.0F,
            1.0F,
            1.0F,
            0.0F,
            1.0F,
        };
        context.OMSetRenderTargets(
            kRenderTargetCount,
            rawTargets.data(),
            nullptr);
        context.RSSetViewports(1, &viewport);
        context.IASetInputLayout(nullptr);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.VSSetShader(&vertexShader, nullptr, 0);
        context.PSSetShader(&pixelShader, nullptr, 0);
        context.PSSetShaderResources(
            0,
            static_cast<UINT>(rawTextureViews.size()),
            rawTextureViews.data());
        context.PSSetSamplers(
            0,
            static_cast<UINT>(samplers.size()),
            samplers.data());

        auto* rawMaterial = materialBuffer.Get();
        auto* rawGeometry = geometryBuffer.Get();
        auto* rawFrame = frameBuffer.Get();
        auto* rawLinearGeometry = linearGeometryBuffer.Get();
        auto* rawInstance = instanceBuffer.Get();
        context.PSSetConstantBuffers(2, 1, &rawMaterial);
        context.PSSetConstantBuffers(5, 1, &rawFrame);
        context.PSSetConstantBuffers(8, 1, &rawLinearGeometry);
        context.PSSetConstantBuffers(12, 1, &rawGeometry);
        if (isInstanced) {
            context.PSSetConstantBuffers(13, 1, &rawInstance);
        }
        context.Draw(3, 0);

        const auto result = readRenderTargets(context, targets);
        constexpr std::array<ID3D11ShaderResourceView*, 4> nullViews{};
        constexpr std::array<ID3D11RenderTargetView*, kRenderTargetCount>
            nullTargets{};
        context.PSSetShaderResources(
            0,
            static_cast<UINT>(nullViews.size()),
            nullViews.data());
        if (isInstanced) {
            ID3D11Buffer* nullBuffer{};
            context.PSSetConstantBuffers(13, 1, &nullBuffer);
        }
        context.OMSetRenderTargets(
            kRenderTargetCount,
            nullTargets.data(),
            nullptr);
        return result;
    }

    [[nodiscard]] bool approximatelyEqual(float lhs, float rhs)
    {
        if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
            return false;
        }
        const auto difference = std::abs(lhs - rhs);
        const auto scale = (std::max)(std::abs(lhs), std::abs(rhs));
        return difference <= kAbsoluteTolerance + kRelativeTolerance * scale;
    }

    [[nodiscard]] std::string compare(
        const ShaderContract& contract,
        std::string_view scenario,
        std::size_t caseIndex,
        const RenderResult& expected,
        const RenderResult& actual)
    {
        for (UINT target = 0; target < kRenderTargetCount; ++target) {
            for (UINT channel = 0; channel < 4; ++channel) {
                if (approximatelyEqual(
                        expected[target][channel],
                        actual[target][channel])) {
                    continue;
                }
                return std::string(contract.name) + " " +
                    std::string(scenario) + " case " +
                    std::to_string(caseIndex) + " differs at target " +
                    std::to_string(target) + " channel " +
                    std::to_string(channel) + ": expected=" +
                    std::to_string(expected[target][channel]) +
                    " actual=" + std::to_string(actual[target][channel]);
            }
        }
        return {};
    }

    [[nodiscard]] float transformedValue(
        float value,
        float gamma,
        float multiplier)
    {
        return std::pow(std::abs(value), gamma) * multiplier;
    }

    [[nodiscard]] RenderResult makeEnabledExpected(
        const ShaderContract& contract,
        std::size_t caseIndex,
        bool usesGlowmap,
        const RenderResult& vanilla,
        const LinearLightingCase& lightingCase)
    {
        auto expected = vanilla;
        const auto geometrySwitch = caseIndex == 0 ? 0.0F :
            (caseIndex == 1 ? 0.35F : 0.8F);
        const auto fadeControl = caseIndex == 2 ? -1.0F : 0.6F;
        const auto fade = fadeControl == -1.0F ? 1.0F :
            (-fadeControl * geometrySwitch) + 1.0F;
        const auto& emitColor = contract.isInstanced ?
            kInstanceEmitColor : kEmitColor;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            auto diffuse = kDiffuseTexture[channel];
            if (contract.hasVertexColor) {
                diffuse *= kVertexColor[channel];
            }
            expected[0][channel] = fade * transformedValue(
                diffuse,
                lightingCase.colorGamma,
                lightingCase.vanillaDiffuseColorMult);

            const auto safeEmissiveMult = (std::max)(
                lightingCase.emissiveMult,
                1.0e-5F);
            auto emission = transformedValue(
                emitColor[channel] / safeEmissiveMult,
                lightingCase.emitColorGamma,
                lightingCase.emissiveMult * lightingCase.emitColorMult);
            if (usesGlowmap) {
                emission *= transformedValue(
                    kGlowTexture[channel],
                    lightingCase.glowmapGamma,
                    lightingCase.glowmapMult);
            }
            expected[4][channel] = emission;
        }
        return expected;
    }

    void run(const std::filesystem::path& root)
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel{};
        constexpr std::array requestedFeatureLevels{
            D3D_FEATURE_LEVEL_11_0,
        };
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                D3D11_CREATE_DEVICE_SINGLETHREADED,
                requestedFeatureLevels.data(),
                static_cast<UINT>(requestedFeatureLevels.size()),
                D3D11_SDK_VERSION,
                &device,
                &featureLevel,
                &context),
            "D3D11CreateDevice(WARP)");
        if (featureLevel != D3D_FEATURE_LEVEL_11_0) {
            fail("D3D11 WARP did not provide feature level 11_0");
        }

        std::array<ComPtr<ID3D11VertexShader>, 8> vertexShaders;
        for (std::size_t index = 0; index < vertexShaders.size(); ++index) {
            const auto hasVertexColor = (index & 1u) != 0;
            const auto isInstanced = (index & 2u) != 0;
            const auto usesTessellatedInputs = (index & 4u) != 0;
            const auto bytecode = compileVertexShader(
                hasVertexColor,
                isInstanced,
                usesTessellatedInputs);
            require(
                device->CreateVertexShader(
                    bytecode->GetBufferPointer(),
                    bytecode->GetBufferSize(),
                    nullptr,
                    &vertexShaders[index]),
                std::string("CreateVertexShader(") +
                    (hasVertexColor ? "COLOR0" : "no COLOR0") +
                    (isInstanced ? ", instanced" : ", non-instanced") +
                    (usesTessellatedInputs ?
                            ", tessellated inputs)" :
                            ", standard inputs)"));
        }

        const auto verified = root / "package" / "Shaders" / "Community" /
            "VerifiedLinearLighting";
        const auto reconstruction = root / "package" / "Shaders" /
            "Community" / "Reconstruction";
        constexpr std::size_t kCaseCount = 3;
        std::vector<std::string> failures;
        for (const auto& contract : kShaderContracts) {
            const auto vanillaBytes = readFile(
                verified / (std::string(contract.name) + ".dxbc"));
            const auto replacementBytes = readFile(
                reconstruction /
                (std::string(contract.name) +
                    ".LinearLightingCandidate.dxbc"));
            const auto usesGlowmap = usesTextureSlot(vanillaBytes, 3);
            ComPtr<ID3D11PixelShader> vanillaShader;
            ComPtr<ID3D11PixelShader> replacementShader;
            require(
                device->CreatePixelShader(
                    vanillaBytes.data(),
                    vanillaBytes.size(),
                    nullptr,
                    &vanillaShader),
                std::string("CreatePixelShader(vanilla ") +
                    std::string(contract.name) + ")");
            require(
                device->CreatePixelShader(
                    replacementBytes.data(),
                    replacementBytes.size(),
                    nullptr,
                    &replacementShader),
                std::string("CreatePixelShader(replacement ") +
                    std::string(contract.name) + ")");

            for (std::size_t caseIndex = 0; caseIndex < kCaseCount;
                 ++caseIndex) {
                auto* vertexShader = vertexShaders[
                    (contract.hasVertexColor ? 1u : 0u) |
                    (contract.isInstanced ? 2u : 0u) |
                    (contract.usesTessellatedInputs ? 4u : 0u)].Get();
                const auto vanilla = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *vanillaShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kDisabledCase);
                const auto disabled = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kDisabledCase);
                auto mismatch = compare(
                    contract,
                    kDisabledCase.name,
                    caseIndex,
                    vanilla,
                    disabled);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }

                const auto identity = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kIdentityCase);
                mismatch = compare(
                    contract,
                    kIdentityCase.name,
                    caseIndex,
                    vanilla,
                    identity);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }

                const auto transformed = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kTransformedCase);
                const auto expected = makeEnabledExpected(
                    contract,
                    caseIndex,
                    usesGlowmap,
                    vanilla,
                    kTransformedCase);
                mismatch = compare(
                    contract,
                    kTransformedCase.name,
                    caseIndex,
                    expected,
                    transformed);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }
            }
        }
        if (!failures.empty()) {
            std::string message = "shader parity/oracle mismatches:";
            for (const auto& failure : failures) {
                message += "\n  " + failure;
            }
            fail(message);
        }
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "Usage: LinearLightingShaderParityTests <repo-root>\n";
        return EXIT_FAILURE;
    }
    try {
        run(std::filesystem::absolute(arguments[1]));
        std::cout << "Linear Lighting disabled and enabled shader paths verified: "
                  << kShaderContracts.size() << " contracts\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
