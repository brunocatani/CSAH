#include "Features/ibl/IblReflectionFreeCapture.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::ibl::ReflectionFreeCaptureRejection;
    using csah::ibl::ReflectionFreeCaptureResources;
    using csah::ibl::ScopedReflectionFreeCapture;

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

    void requireNear(
        float actual,
        float expected,
        const std::string& label)
    {
        if (std::abs(actual - expected) > 0.0001f) {
            fail(label + ": expected " + std::to_string(expected) +
                ", got " + std::to_string(actual));
        }
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

    [[nodiscard]] ComPtr<ID3DBlob> compile(
        const char* source,
        const char* target)
    {
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto result = D3DCompile(
            source,
            std::strlen(source),
            "IblReflectionFreeCaptureTests",
            nullptr,
            nullptr,
            "main",
            target,
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &bytecode,
            &errors);
        if (FAILED(result)) {
            const auto* details = errors ?
                static_cast<const char*>(errors->GetBufferPointer()) :
                "no compiler diagnostics";
            fail(std::string("D3DCompile failed: ") + details);
        }
        return bytecode;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createCube(
        ID3D11Device& device,
        const Float4& color)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 6;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        std::array<D3D11_SUBRESOURCE_DATA, 6> initialData{};
        for (auto& subresource : initialData) {
            subresource.pSysMem = &color;
            subresource.SysMemPitch = sizeof(color);
            subresource.SysMemSlicePitch = sizeof(color);
        }
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(
                &description,
                initialData.data(),
                &texture),
            "CreateTexture2D(cube)");
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        viewDescription.TextureCubeArray.MostDetailedMip = 0;
        viewDescription.TextureCubeArray.MipLevels = 1;
        viewDescription.TextureCubeArray.First2DArrayFace = 0;
        viewDescription.TextureCubeArray.NumCubes = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(cube)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device& device,
        const Float4& color)
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
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = &color;
        initialData.SysMemPitch = sizeof(color);
        initialData.SysMemSlicePitch = sizeof(color);
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(
                &description,
                &initialData,
                &texture),
            "CreateTexture2D(screen reflection)");
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView(screen reflection)");
        return view;
    }

    struct Output
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
    };

    [[nodiscard]] Output createOutput(ID3D11Device& device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 4;
        description.Height = 2;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;
        Output result;
        requireSucceeded(
            device.CreateTexture2D(&description, nullptr, &result.texture),
            "CreateTexture2D(output)");
        requireSucceeded(
            device.CreateRenderTargetView(
                result.texture.Get(),
                nullptr,
                &result.view),
            "CreateRenderTargetView(output)");
        return result;
    }

    [[nodiscard]] ComPtr<ID3D11UnorderedAccessView> createUavSentinel(
        ID3D11Device& device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32_UINT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, nullptr, &texture),
            "CreateTexture2D(UAV sentinel)");
        ComPtr<ID3D11UnorderedAccessView> view;
        requireSucceeded(
            device.CreateUnorderedAccessView(texture.Get(), nullptr, &view),
            "CreateUnorderedAccessView(UAV sentinel)");
        return view;
    }

    [[nodiscard]] Float4 readFirstPixel(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        ID3D11Texture2D& source)
    {
        D3D11_TEXTURE2D_DESC description{};
        source.GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        requireSucceeded(
            device.CreateTexture2D(&description, nullptr, &staging),
            "CreateTexture2D(staging)");
        context.CopyResource(staging.Get(), &source);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        requireSucceeded(
            context.Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(staging)");
        require(
            mapped.pData && mapped.RowPitch >= sizeof(Float4),
            "mapped output has an invalid layout");
        Float4 result{};
        std::memcpy(&result, mapped.pData, sizeof(result));
        context.Unmap(staging.Get(), 0);
        return result;
    }

    void run()
    {
        constexpr auto vertexSource = R"(
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput result;
    result.uv = float2((vertexId << 1) & 2, vertexId & 2);
    result.position = float4(
        result.uv * float2(2.0, -2.0) + float2(-1.0, 1.0),
        0.0,
        1.0);
    return result;
}
)";
        constexpr auto pixelSource = R"(
TextureCubeArray<float4> Environment : register(t8);
Texture2D<float4> ScreenReflection : register(t14);
SamplerState LinearSampler : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    const float4 base = float4(0.125, 0.25, 0.375, 1.0);
    const float4 environment = Environment.SampleLevel(
        LinearSampler,
        float4(1.0, 0.0, 0.0, 0.0),
        0.0);
    const float4 screen = ScreenReflection.SampleLevel(
        LinearSampler,
        float2(0.5, 0.5),
        0.0);
    return base + environment + screen;
}
)";

        auto d3d = createDevice();
        const auto vertexBytecode = compile(vertexSource, "vs_5_0");
        const auto pixelBytecode = compile(pixelSource, "ps_5_0");
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        requireSucceeded(
            d3d.device->CreateVertexShader(
                vertexBytecode->GetBufferPointer(),
                vertexBytecode->GetBufferSize(),
                nullptr,
                &vertexShader),
            "CreateVertexShader");
        requireSucceeded(
            d3d.device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(),
                pixelBytecode->GetBufferSize(),
                nullptr,
                &pixelShader),
            "CreatePixelShader");

        constexpr Float4 environmentColor{ 0.5f, 0.25f, 0.125f, 0.0f };
        constexpr Float4 screenColor{ 0.25f, 0.5f, 0.75f, 0.0f };
        auto environment = createCube(*d3d.device.Get(), environmentColor);
        auto screen = createTexture(*d3d.device.Get(), screenColor);
        auto output = createOutput(*d3d.device.Get());
        auto uavSentinel = createUavSentinel(*d3d.device.Get());

        D3D11_DEPTH_STENCIL_DESC depthStencilDescription{};
        depthStencilDescription.DepthEnable = TRUE;
        depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStencilDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilDescription.StencilEnable = TRUE;
        depthStencilDescription.StencilReadMask =
            D3D11_DEFAULT_STENCIL_READ_MASK;
        depthStencilDescription.StencilWriteMask =
            D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthStencilDescription.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilDepthFailOp =
            D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilPassOp =
            D3D11_STENCIL_OP_REPLACE;
        depthStencilDescription.FrontFace.StencilFunc =
            D3D11_COMPARISON_ALWAYS;
        depthStencilDescription.BackFace = depthStencilDescription.FrontFace;
        ComPtr<ID3D11DepthStencilState> depthStencilState;
        requireSucceeded(
            d3d.device->CreateDepthStencilState(
                &depthStencilDescription,
                &depthStencilState),
            "CreateDepthStencilState");

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        requireSucceeded(
            d3d.device->CreateSamplerState(&samplerDescription, &sampler),
            "CreateSamplerState");

        auto* outputView = output.view.Get();
        auto* uavView = uavSentinel.Get();
        constexpr UINT preserveCounter = UINT_MAX;
        d3d.context->OMSetRenderTargetsAndUnorderedAccessViews(
            1,
            &outputView,
            nullptr,
            1,
            1,
            &uavView,
            &preserveCounter);
        constexpr UINT stencilReference = 0x5A;
        d3d.context->OMSetDepthStencilState(
            depthStencilState.Get(),
            stencilReference);
        auto* environmentView = environment.Get();
        auto* screenView = screen.Get();
        auto* samplerView = sampler.Get();
        d3d.context->PSSetShaderResources(8, 1, &environmentView);
        d3d.context->PSSetShaderResources(14, 1, &screenView);
        d3d.context->PSSetSamplers(0, 1, &samplerView);
        d3d.context->VSSetShader(vertexShader.Get(), nullptr, 0);
        d3d.context->PSSetShader(pixelShader.Get(), nullptr, 0);
        d3d.context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        constexpr D3D11_VIEWPORT viewport{
            0.0f,
            0.0f,
            4.0f,
            2.0f,
            0.0f,
            1.0f,
        };
        d3d.context->RSSetViewports(1, &viewport);

        ReflectionFreeCaptureResources resources;
        require(
            resources.initialize(d3d.device.Get()),
            "reflection-free resources did not initialize");
        require(
            resources.prepareScratch(output.view.Get()),
            "reflection-free scratch did not prepare");
        require(
            resources.scratchMatches(output.view.Get()),
            "prepared scratch did not match output");

        {
            ScopedReflectionFreeCapture capture(
                d3d.context.Get(),
                resources);
            require(capture.active(), "reflection-free scope did not activate");
            require(
                capture.rejection() == ReflectionFreeCaptureRejection::none,
                "active reflection-free scope retained a rejection reason");
            ID3D11DepthStencilState* captureDepthStencilStateRaw{};
            UINT captureStencilReference{};
            d3d.context->OMGetDepthStencilState(
                &captureDepthStencilStateRaw,
                &captureStencilReference);
            ComPtr<ID3D11DepthStencilState> captureDepthStencilState;
            captureDepthStencilState.Attach(captureDepthStencilStateRaw);
            require(
                captureDepthStencilState != nullptr,
                "reflection-free depth/stencil state was not bound");
            D3D11_DEPTH_STENCIL_DESC captureDepthStencilDescription{};
            captureDepthStencilState->GetDesc(
                &captureDepthStencilDescription);
            require(
                captureDepthStencilDescription.DepthWriteMask ==
                    D3D11_DEPTH_WRITE_MASK_ZERO,
                "reflection-free duplicate still permits depth writes");
            require(
                captureDepthStencilDescription.StencilWriteMask == 0,
                "reflection-free duplicate still permits stencil writes");
            require(
                captureStencilReference == stencilReference,
                "reflection-free duplicate changed the stencil reference");
            d3d.context->Draw(3, 0);
            require(capture.restore(), "reflection-free scope did not restore");
        }

        ID3D11RenderTargetView* restoredOutputRaw{};
        ID3D11ShaderResourceView* restoredEnvironmentRaw{};
        ID3D11ShaderResourceView* restoredScreenRaw{};
        ID3D11UnorderedAccessView* restoredUavRaw{};
        ID3D11DepthStencilState* restoredDepthStencilStateRaw{};
        UINT restoredStencilReference{};
        d3d.context->OMGetRenderTargets(1, &restoredOutputRaw, nullptr);
        d3d.context->PSGetShaderResources(
            8,
            1,
            &restoredEnvironmentRaw);
        d3d.context->PSGetShaderResources(14, 1, &restoredScreenRaw);
        d3d.context->OMGetRenderTargetsAndUnorderedAccessViews(
            0,
            nullptr,
            nullptr,
            1,
            1,
            &restoredUavRaw);
        d3d.context->OMGetDepthStencilState(
            &restoredDepthStencilStateRaw,
            &restoredStencilReference);
        ComPtr<ID3D11RenderTargetView> restoredOutput;
        ComPtr<ID3D11ShaderResourceView> restoredEnvironment;
        ComPtr<ID3D11ShaderResourceView> restoredScreen;
        ComPtr<ID3D11UnorderedAccessView> restoredUav;
        ComPtr<ID3D11DepthStencilState> restoredDepthStencilState;
        restoredOutput.Attach(restoredOutputRaw);
        restoredEnvironment.Attach(restoredEnvironmentRaw);
        restoredScreen.Attach(restoredScreenRaw);
        restoredUav.Attach(restoredUavRaw);
        restoredDepthStencilState.Attach(restoredDepthStencilStateRaw);
        require(
            restoredOutput.Get() == output.view.Get(),
            "OM render target identity was not restored");
        require(
            restoredEnvironment.Get() == environment.Get(),
            "PS t8 identity was not restored");
        require(
            restoredScreen.Get() == screen.Get(),
            "PS t14 identity was not restored");
        require(
            restoredUav.Get() == uavSentinel.Get(),
            "untouched OM UAV identity changed");
        require(
            restoredDepthStencilState.Get() == depthStencilState.Get(),
            "OM depth/stencil state identity was not restored");
        require(
            restoredStencilReference == stencilReference,
            "OM stencil reference was not restored");

        d3d.context->Draw(3, 0);
        const auto reflectionFree = readFirstPixel(
            *d3d.device.Get(),
            *d3d.context.Get(),
            *resources.scratchTexture());
        const auto visible = readFirstPixel(
            *d3d.device.Get(),
            *d3d.context.Get(),
            *output.texture.Get());
        requireNear(reflectionFree.x, 0.125f, "reflection-free red");
        requireNear(reflectionFree.y, 0.25f, "reflection-free green");
        requireNear(reflectionFree.z, 0.375f, "reflection-free blue");
        requireNear(visible.x, 0.875f, "visible red");
        requireNear(visible.y, 1.0f, "visible green");
        requireNear(visible.z, 1.25f, "visible blue");

        D3D11_BLEND_DESC rgbOnlyBlendDescription{};
        rgbOnlyBlendDescription.RenderTarget[0].RenderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_RED |
            D3D11_COLOR_WRITE_ENABLE_GREEN |
            D3D11_COLOR_WRITE_ENABLE_BLUE;
        ComPtr<ID3D11BlendState> rgbOnlyBlendState;
        requireSucceeded(
            d3d.device->CreateBlendState(
                &rgbOnlyBlendDescription,
                &rgbOnlyBlendState),
            "CreateBlendState(RGB-only)");
        d3d.context->OMSetBlendState(
            rgbOnlyBlendState.Get(),
            nullptr,
            UINT_MAX);
        {
            ScopedReflectionFreeCapture accepted(
                d3d.context.Get(),
                resources);
            require(
                accepted.active(),
                "RGB-only radiance target was rejected");
            require(
                accepted.rejection() ==
                    ReflectionFreeCaptureRejection::none,
                "RGB-only radiance target retained a rejection reason");
            require(
                accepted.restore(),
                "RGB-only radiance target did not restore");
        }
        ID3D11BlendState* restoredRgbOnlyBlendStateRaw{};
        d3d.context->OMGetBlendState(
            &restoredRgbOnlyBlendStateRaw,
            nullptr,
            nullptr);
        ComPtr<ID3D11BlendState> restoredRgbOnlyBlendState;
        restoredRgbOnlyBlendState.Attach(restoredRgbOnlyBlendStateRaw);
        require(
            restoredRgbOnlyBlendState.Get() == rgbOnlyBlendState.Get(),
            "RGB-only blend-state identity was not preserved");

        auto incompleteRadianceBlendDescription = rgbOnlyBlendDescription;
        incompleteRadianceBlendDescription.RenderTarget[0]
            .RenderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_RED |
            D3D11_COLOR_WRITE_ENABLE_GREEN;
        ComPtr<ID3D11BlendState> incompleteRadianceBlendState;
        requireSucceeded(
            d3d.device->CreateBlendState(
                &incompleteRadianceBlendDescription,
                &incompleteRadianceBlendState),
            "CreateBlendState(incomplete radiance)");
        d3d.context->OMSetBlendState(
            incompleteRadianceBlendState.Get(),
            nullptr,
            UINT_MAX);
        {
            ScopedReflectionFreeCapture rejected(
                d3d.context.Get(),
                resources);
            require(
                !rejected.active(),
                "incomplete radiance write mask was accepted");
            require(
                rejected.rejection() == ReflectionFreeCaptureRejection::
                    renderTargetWriteMask,
                "incomplete radiance mask reported the wrong rejection");
        }
        d3d.context->OMSetBlendState(nullptr, nullptr, UINT_MAX);

        D3D11_BLEND_DESC unsafeBlendDescription{};
        unsafeBlendDescription.RenderTarget[0].BlendEnable = TRUE;
        unsafeBlendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        unsafeBlendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        unsafeBlendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        unsafeBlendDescription.RenderTarget[0].SrcBlendAlpha =
            D3D11_BLEND_ONE;
        unsafeBlendDescription.RenderTarget[0].DestBlendAlpha =
            D3D11_BLEND_ONE;
        unsafeBlendDescription.RenderTarget[0].BlendOpAlpha =
            D3D11_BLEND_OP_ADD;
        unsafeBlendDescription.RenderTarget[0].RenderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_ALL;
        ComPtr<ID3D11BlendState> unsafeBlendState;
        requireSucceeded(
            d3d.device->CreateBlendState(
                &unsafeBlendDescription,
                &unsafeBlendState),
            "CreateBlendState");
        d3d.context->OMSetBlendState(
            unsafeBlendState.Get(),
            nullptr,
            UINT_MAX);
        {
            ScopedReflectionFreeCapture rejected(
                d3d.context.Get(),
                resources);
            require(
                !rejected.active(),
                "blended duplicate draw was not rejected");
            require(
                rejected.rejection() ==
                    ReflectionFreeCaptureRejection::blendEnabled,
                "blended duplicate reported the wrong rejection reason");
        }
        ID3D11BlendState* observedBlendStateRaw{};
        d3d.context->OMGetBlendState(
            &observedBlendStateRaw,
            nullptr,
            nullptr);
        ComPtr<ID3D11BlendState> observedBlendState;
        observedBlendState.Attach(observedBlendStateRaw);
        require(
            observedBlendState.Get() == unsafeBlendState.Get(),
            "rejected duplicate changed the blend state");
        d3d.context->OMSetBlendState(nullptr, nullptr, UINT_MAX);

        D3D11_BUFFER_DESC streamOutputDescription{};
        streamOutputDescription.ByteWidth = 64;
        streamOutputDescription.Usage = D3D11_USAGE_DEFAULT;
        streamOutputDescription.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        ComPtr<ID3D11Buffer> streamOutputBuffer;
        requireSucceeded(
            d3d.device->CreateBuffer(
                &streamOutputDescription,
                nullptr,
                &streamOutputBuffer),
            "CreateBuffer(stream output)");
        auto* streamOutputTarget = streamOutputBuffer.Get();
        constexpr UINT streamOutputOffset = 0;
        d3d.context->SOSetTargets(
            1,
            &streamOutputTarget,
            &streamOutputOffset);
        {
            ScopedReflectionFreeCapture rejected(
                d3d.context.Get(),
                resources);
            require(
                !rejected.active(),
                "stream-output duplicate draw was not rejected");
            require(
                rejected.rejection() ==
                    ReflectionFreeCaptureRejection::streamOutput,
                "stream-output duplicate reported the wrong rejection reason");
        }
        ID3D11Buffer* observedStreamOutputRaw{};
        d3d.context->SOGetTargets(1, &observedStreamOutputRaw);
        ComPtr<ID3D11Buffer> observedStreamOutput;
        observedStreamOutput.Attach(observedStreamOutputRaw);
        require(
            observedStreamOutput.Get() == streamOutputBuffer.Get(),
            "rejected duplicate changed the stream-output target");
        d3d.context->SOSetTargets(0, nullptr, nullptr);

        auto otherDevice = createDevice();
        auto foreignOutput = createOutput(*otherDevice.device.Get());
        require(
            !resources.prepareScratch(foreignOutput.view.Get()),
            "foreign-device output was accepted");
        require(
            resources.scratchMatches(output.view.Get()),
            "foreign-device rejection destroyed the live scratch set");

        d3d.context->ClearState();
        otherDevice.context->ClearState();
    }
}

int main()
{
    try {
        run();
        std::cout <<
            "FO4VR IBL reflection-free capture transaction verified on D3D11 WARP\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
