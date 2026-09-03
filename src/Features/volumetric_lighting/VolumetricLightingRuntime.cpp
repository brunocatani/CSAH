#include "Features/volumetric_lighting/VolumetricLightingRuntime.h"

#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/volumetric_lighting/VolumetricEngineData.h"
#include "Features/volumetric_lighting/VolumetricStateScope.h"
#include "support/Logger.h"

#include "VolumetricBlurHPS.h"
#include "VolumetricBlurVPS.h"
#include "VolumetricCompositePS.h"
#include "VolumetricFullscreenVS.h"
#include "VolumetricGenerateCS.h"
#include "VolumetricHostPassThroughPS.h"
#include "VolumetricIntegrateCS.h"
#include "VolumetricResolvePS.h"
#include "VolumetricTemporalPS.h"

#include <REL/Relocation.h>
#include <F4SE/F4SE.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <wrl/client.h>

namespace community_shaders::volumetric_lighting
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::size_t kHostBytecodeSize = 984;
        constexpr std::array<std::uint32_t, 4> kHostChecksum{
            0x49DDA6DC, 0x06867B4F, 0xEC9E571F, 0xF250865B
        };
        constexpr std::uint64_t kHostHash = 0x08F64FA7FA0F242Aull;
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        constexpr std::uintptr_t kIniSettingVtableRva = 0x02C81230;
        constexpr std::uintptr_t kSunbeamsSettingRecordRva = 0x037C76E8;
        constexpr std::uintptr_t kSunbeamsSettingNameRva = 0x02D7A3C0;
        constexpr std::uintptr_t kSunbeamsAvailabilityRva = 0x0689AC94;
        constexpr std::uintptr_t kSunbeamsConsumerRva = 0x0288D156;
        constexpr char kSunbeamsSettingName[] = "bUseSunbeams:Display";

        constexpr UINT kNativeDepthSlot = 3;
        constexpr UINT kNativeShadowSlot = 5;
        constexpr UINT kNativeShadowSamplerSlot = 5;
        constexpr UINT kNativeLightConstantSlot = 2;
        constexpr UINT kNativeStereoConstantSlot = 8;
        constexpr UINT kNativeCameraConstantSlot = 12;
        constexpr float kWeatherUnitScale = 0.0001f;
        constexpr float kCloudHeight = 140056.0f;
        constexpr float kPlanetRadius = 446148448.0f;
        constexpr ULONGLONG kWeatherRefreshMilliseconds = 250;
        constexpr float kTeleportDistance = 256.0f;
        constexpr float kDepthRejectStart = 0.001f;
        constexpr float kDepthRejectEnd = 0.01f;

        struct alignas(16) FrameConstants final
        {
            float eyeOrigin[2][4]{};
            float volumeParams[4]{};
            float applyParams[4]{};
            float mediumColor[4]{};
            float phaseParams[4]{};
            float frameParams[4]{};
            float windParams[4]{};
            float cloudParams[4]{};
        };
        static_assert(sizeof(FrameConstants) == 144);

        struct alignas(16) TemporalConstants final
        {
            float previousEyeOrigin[2][4]{};
            float previousViewProjection[2][16]{};
            float parameters[4]{};
        };
        static_assert(sizeof(TemporalConstants) == 176);

        struct DirectionalFrame final
        {
            ComPtr<ID3D11ShaderResourceView> depth;
            ComPtr<ID3D11ShaderResourceView> shadow;
            ComPtr<ID3D11SamplerState> comparisonSampler;
            ComPtr<ID3D11Buffer> lightConstants;
            ComPtr<ID3D11Buffer> stereoConstants;
            ComPtr<ID3D11Buffer> cameraConstants;
            std::uint64_t generation{};
            std::uint64_t settingsRevision{};

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return depth && shadow && comparisonSampler &&
                    lightConstants && stereoConstants && cameraConstants;
            }
        };

        struct Target final
        {
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11ShaderResourceView> source;
            ComPtr<ID3D11RenderTargetView> target;
        };

        struct Volume final
        {
            ComPtr<ID3D11Texture3D> texture;
            ComPtr<ID3D11ShaderResourceView> source;
            ComPtr<ID3D11UnorderedAccessView> output;
        };

        struct GpuState final
        {
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            ComPtr<ID3D11VertexShader> fullscreenVertex;
            ComPtr<ID3D11ComputeShader> generateCompute;
            ComPtr<ID3D11ComputeShader> integrateCompute;
            ComPtr<ID3D11PixelShader> resolvePixel;
            ComPtr<ID3D11PixelShader> temporalPixel;
            ComPtr<ID3D11PixelShader> blurHorizontalPixel;
            ComPtr<ID3D11PixelShader> blurVerticalPixel;
            ComPtr<ID3D11PixelShader> compositePixel;
            ComPtr<ID3D11Buffer> frameConstants;
            ComPtr<ID3D11Buffer> temporalConstants;
            ComPtr<ID3D11SamplerState> linearSampler;
            ComPtr<ID3D11DepthStencilState> depthState;
            ComPtr<ID3D11RasterizerState> rasterState;
            Volume rawVolume;
            Volume integratedVolume;
            Target currentVolume;
            Target blurVolume;
            Target receiverDepth;
            std::array<Target, 2> historyVolume;
            std::array<Target, 2> historyDepth;
            DirectionalFrame directionalFrame;
            StereoFrame previousCamera;
            WeatherFrame weather;
            ULONGLONG weatherRefreshTick{};
            std::uint32_t fullWidth{};
            std::uint32_t fullHeight{};
            std::uint32_t halfWidth{};
            std::uint32_t halfHeight{};
            std::uint32_t volumeWidth{};
            std::uint32_t volumeHeight{};
            std::uint32_t volumeDepth{};
            std::uint32_t resourceQuality{ std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t historyWriteIndex{};
            std::uint64_t frameIndex{};
            std::uint64_t appliedSettingsRevision{};
            bool staticReady{};
            bool sizeReady{};
            bool historyValid{};
        };

        struct State final
        {
            std::atomic_bool enabled{ true };
            std::atomic_uint32_t quality{ 2 };
            std::atomic<float> intensity{ 1.0f };
            std::atomic<float> baseScattering{ 0.06f };
            std::atomic<float> shaftIntensity{ 1.35f };
            std::atomic<float> densityContribution{ 0.55f };
            std::atomic<float> densityScale{ 1.0f };
            std::atomic<float> windSpeed{ 6.0f };
            std::atomic<float> phaseContribution{ 0.30f };
            std::atomic<float> maxDistance{ 6000.0f };
            std::atomic<float> temporalWeight{ 0.90f };
            std::atomic_bool diagnosticSuppressed{};
            std::atomic_bool started{};
            std::atomic_bool nativeContractValid{};
            std::atomic_bool engineDataValid{};
            std::atomic_bool gpuFailed{};
            std::atomic_uint64_t settingsRevision{ 1 };
            std::uintptr_t moduleBase{};
            std::uint8_t* settingValue{};
            std::uint8_t* availabilityValue{};
            std::array<std::atomic<ID3D11PixelShader*>, 8> hostShaders{};
            GpuState gpu;
            std::atomic_uint64_t hostMatches{};
            std::atomic_uint64_t hostCreations{};
            std::atomic_uint64_t directionalCaptures{};
            std::atomic_uint64_t renderedFrames{};
            std::atomic_uint64_t rejectedFrames{};
            std::atomic_bool firstHostLogged{};
            std::atomic_bool firstCaptureLogged{};
            std::atomic_bool firstRenderLogged{};
            std::atomic_bool firstRejectLogged{};
        };

        State& state() noexcept
        {
            static State value;
            return value;
        }

        thread_local bool gInternalRender{};

        class InternalRenderScope final
        {
        public:
            InternalRenderScope() noexcept : previous_(gInternalRender)
            {
                gInternalRender = true;
            }
            ~InternalRenderScope() noexcept { gInternalRender = previous_; }
            InternalRenderScope(const InternalRenderScope&) = delete;
            InternalRenderScope& operator=(const InternalRenderScope&) = delete;

        private:
            bool previous_{};
        };

        [[nodiscard]] Settings readSettings(const State& value) noexcept
        {
            return sanitize({
                .enabled = value.enabled.load(std::memory_order_acquire),
                .quality = value.quality.load(std::memory_order_relaxed),
                .intensity = value.intensity.load(std::memory_order_relaxed),
                .baseScattering = value.baseScattering.load(
                    std::memory_order_relaxed),
                .shaftIntensity = value.shaftIntensity.load(
                    std::memory_order_relaxed),
                .densityContribution = value.densityContribution.load(
                    std::memory_order_relaxed),
                .densityScale = value.densityScale.load(
                    std::memory_order_relaxed),
                .windSpeed = value.windSpeed.load(std::memory_order_relaxed),
                .phaseContribution = value.phaseContribution.load(
                    std::memory_order_relaxed),
                .maxDistance = value.maxDistance.load(
                    std::memory_order_relaxed),
                .temporalWeight = value.temporalWeight.load(
                    std::memory_order_relaxed),
            });
        }

        [[nodiscard]] bool readableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress);
            const auto regionEnd = regionBegin + information.RegionSize;
            return begin >= regionBegin && begin <= regionEnd &&
                size <= regionEnd - begin;
        }

        [[nodiscard]] bool writableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!readableRange(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            (void)VirtualQuery(address, &information, sizeof(information));
            constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & writable) != 0;
        }

        void writeBoolean(std::uint8_t* destination, bool value) noexcept
        {
            if (destination) {
                (void)InterlockedExchange8(
                    reinterpret_cast<volatile char*>(destination),
                    value ? 1 : 0);
            }
        }

        [[nodiscard]] std::uint64_t fnv1a(
            const void* data,
            std::size_t size) noexcept
        {
            auto hash = kFnvOffset;
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= kFnvPrime;
            }
            return hash;
        }

        [[nodiscard]] bool matchesHost(
            const void* bytecode,
            std::size_t length) noexcept
        {
            if (!bytecode || length != kHostBytecodeSize) {
                return false;
            }
            const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
            return std::memcmp(bytes, "DXBC", 4) == 0 &&
                std::memcmp(
                    bytes + 4,
                    kHostChecksum.data(),
                    sizeof(kHostChecksum)) == 0 &&
                fnv1a(bytecode, length) == kHostHash;
        }

        [[nodiscard]] bool bufferAtLeast(
            ID3D11Buffer* buffer,
            UINT minimumSize) noexcept
        {
            if (!buffer) {
                return false;
            }
            D3D11_BUFFER_DESC description{};
            buffer->GetDesc(&description);
            return description.ByteWidth >= minimumSize &&
                (description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
        }

        [[nodiscard]] bool validDepth(
            ID3D11ShaderResourceView* view,
            ID3D11Device* device,
            UINT& width,
            UINT& height) noexcept
        {
            width = 0;
            height = 0;
            if (!view || !device) {
                return false;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
            view->GetDesc(&viewDescription);
            ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11Device> owner;
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDevice(owner.GetAddressOf());
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            width = description.Width;
            height = description.Height;
            return owner.Get() == device &&
                viewDescription.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
                description.ArraySize == 1 && description.SampleDesc.Count == 1 &&
                width >= 2 && (width & 1u) == 0 && height != 0;
        }

        [[nodiscard]] bool validShadow(
            ID3D11ShaderResourceView* view,
            ID3D11Device* device) noexcept
        {
            if (!view || !device) {
                return false;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
            view->GetDesc(&viewDescription);
            ComPtr<ID3D11Resource> resource;
            view->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11Device> owner;
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDevice(owner.GetAddressOf());
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            return owner.Get() == device &&
                viewDescription.ViewDimension ==
                    D3D11_SRV_DIMENSION_TEXTURE2DARRAY &&
                viewDescription.Texture2DArray.ArraySize >= 2 &&
                description.ArraySize >= 2 && description.SampleDesc.Count == 1;
        }

        [[nodiscard]] bool validComparisonSampler(
            ID3D11SamplerState* sampler) noexcept
        {
            if (!sampler) {
                return false;
            }
            D3D11_SAMPLER_DESC description{};
            sampler->GetDesc(&description);
            return D3D11_DECODE_IS_COMPARISON_FILTER(description.Filter) &&
                description.ComparisonFunc != D3D11_COMPARISON_NEVER;
        }

        void releaseSizeResources(GpuState& gpu) noexcept
        {
            gpu.rawVolume = {};
            gpu.integratedVolume = {};
            gpu.currentVolume = {};
            gpu.blurVolume = {};
            gpu.receiverDepth = {};
            gpu.historyVolume = {};
            gpu.historyDepth = {};
            gpu.fullWidth = 0;
            gpu.fullHeight = 0;
            gpu.halfWidth = 0;
            gpu.halfHeight = 0;
            gpu.volumeWidth = 0;
            gpu.volumeHeight = 0;
            gpu.volumeDepth = 0;
            gpu.resourceQuality = std::numeric_limits<std::uint32_t>::max();
            gpu.historyWriteIndex = 0;
            gpu.historyValid = false;
            gpu.previousCamera = {};
            gpu.sizeReady = false;
        }

        [[nodiscard]] HRESULT createTarget(
            ID3D11Device* device,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            Target& output) noexcept
        {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            auto result = device->CreateTexture2D(
                &description, nullptr, output.texture.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) {
                result = device->CreateShaderResourceView(
                    output.texture.Get(),
                    nullptr,
                    output.source.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = device->CreateRenderTargetView(
                    output.texture.Get(),
                    nullptr,
                    output.target.ReleaseAndGetAddressOf());
            }
            return result;
        }

        [[nodiscard]] HRESULT createVolume(
            ID3D11Device* device,
            UINT width,
            UINT height,
            UINT depth,
            Volume& output) noexcept
        {
            D3D11_TEXTURE3D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.Depth = depth;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R16G16_FLOAT;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            auto result = device->CreateTexture3D(
                &description, nullptr, output.texture.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) {
                result = device->CreateShaderResourceView(
                    output.texture.Get(),
                    nullptr,
                    output.source.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = device->CreateUnorderedAccessView(
                    output.texture.Get(),
                    nullptr,
                    output.output.ReleaseAndGetAddressOf());
            }
            return result;
        }

        [[nodiscard]] bool createStaticResources(GpuState& gpu) noexcept
        {
            if (!gpu.device) {
                return false;
            }
            InternalRenderScope internal;
            auto result = gpu.device->CreateVertexShader(
                fo4vr_cs_volumetric_fullscreen_vs,
                sizeof(fo4vr_cs_volumetric_fullscreen_vs),
                nullptr,
                gpu.fullscreenVertex.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateComputeShader(
                    fo4vr_cs_volumetric_generate_cs,
                    sizeof(fo4vr_cs_volumetric_generate_cs),
                    nullptr,
                    gpu.generateCompute.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateComputeShader(
                    fo4vr_cs_volumetric_integrate_cs,
                    sizeof(fo4vr_cs_volumetric_integrate_cs),
                    nullptr,
                    gpu.integrateCompute.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreatePixelShader(
                    fo4vr_cs_volumetric_resolve_ps,
                    sizeof(fo4vr_cs_volumetric_resolve_ps),
                    nullptr,
                    gpu.resolvePixel.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreatePixelShader(
                    fo4vr_cs_volumetric_temporal_ps,
                    sizeof(fo4vr_cs_volumetric_temporal_ps),
                    nullptr,
                    gpu.temporalPixel.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreatePixelShader(
                    fo4vr_cs_volumetric_blur_h_ps,
                    sizeof(fo4vr_cs_volumetric_blur_h_ps),
                    nullptr,
                    gpu.blurHorizontalPixel.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreatePixelShader(
                    fo4vr_cs_volumetric_blur_v_ps,
                    sizeof(fo4vr_cs_volumetric_blur_v_ps),
                    nullptr,
                    gpu.blurVerticalPixel.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = gpu.device->CreatePixelShader(
                    fo4vr_cs_volumetric_composite_ps,
                    sizeof(fo4vr_cs_volumetric_composite_ps),
                    nullptr,
                    gpu.compositePixel.ReleaseAndGetAddressOf());
            }

            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(FrameConstants);
            buffer.Usage = D3D11_USAGE_DYNAMIC;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateBuffer(
                    &buffer, nullptr, gpu.frameConstants.ReleaseAndGetAddressOf());
            }
            buffer.ByteWidth = sizeof(TemporalConstants);
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateBuffer(
                    &buffer,
                    nullptr,
                    gpu.temporalConstants.ReleaseAndGetAddressOf());
            }

            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampler.MinLOD = 0.0f;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateSamplerState(
                    &sampler, gpu.linearSampler.ReleaseAndGetAddressOf());
            }

            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateDepthStencilState(
                    &depth, gpu.depthState.ReleaseAndGetAddressOf());
            }
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = TRUE;
            raster.ScissorEnable = FALSE;
            if (SUCCEEDED(result)) {
                result = gpu.device->CreateRasterizerState(
                    &raster, gpu.rasterState.ReleaseAndGetAddressOf());
            }
            gpu.staticReady = SUCCEEDED(result) && gpu.fullscreenVertex &&
                gpu.generateCompute && gpu.integrateCompute &&
                gpu.resolvePixel && gpu.temporalPixel &&
                gpu.blurHorizontalPixel && gpu.blurVerticalPixel &&
                gpu.compositePixel && gpu.frameConstants &&
                gpu.temporalConstants && gpu.linearSampler &&
                gpu.depthState && gpu.rasterState;
            if (!gpu.staticReady) {
                logging::critical(
                    "Volumetric Lighting GPU resource creation failed (HRESULT=0x{:08X}); the engine host gate is disabled.",
                    static_cast<std::uint32_t>(result));
            }
            return gpu.staticReady;
        }

        [[nodiscard]] bool ensureSizeResources(
            GpuState& gpu,
            UINT fullWidth,
            UINT fullHeight,
            std::uint32_t quality) noexcept
        {
            struct Dimensions final
            {
                UINT width;
                UINT height;
                UINT depth;
            };
            constexpr std::array<Dimensions, 3> dimensions{
                Dimensions{ 160, 96, 64 },
                Dimensions{ 240, 144, 80 },
                Dimensions{ 320, 192, 96 }
            };
            const auto selected = dimensions[std::min<std::uint32_t>(quality, 2)];
            const auto eyeWidth = fullWidth / 2u;
            const auto halfWidth = 2u * ((eyeWidth + 1u) / 2u);
            const auto halfHeight = (fullHeight + 1u) / 2u;
            if (gpu.sizeReady && gpu.fullWidth == fullWidth &&
                gpu.fullHeight == fullHeight && gpu.halfWidth == halfWidth &&
                gpu.halfHeight == halfHeight &&
                gpu.resourceQuality == quality) {
                return true;
            }
            releaseSizeResources(gpu);
            if (!gpu.device || fullWidth < 2 || (fullWidth & 1u) != 0 ||
                fullHeight == 0 || halfWidth < 2 || (halfWidth & 1u) != 0) {
                return false;
            }
            auto result = createVolume(
                gpu.device.Get(),
                selected.width,
                selected.height,
                selected.depth,
                gpu.rawVolume);
            if (SUCCEEDED(result)) {
                result = createVolume(
                    gpu.device.Get(),
                    selected.width,
                    selected.height,
                    selected.depth,
                    gpu.integratedVolume);
            }
            if (SUCCEEDED(result)) {
                result = createTarget(
                    gpu.device.Get(),
                    halfWidth,
                    halfHeight,
                    DXGI_FORMAT_R16G16_FLOAT,
                    gpu.currentVolume);
            }
            if (SUCCEEDED(result)) {
                result = createTarget(
                    gpu.device.Get(),
                    halfWidth,
                    halfHeight,
                    DXGI_FORMAT_R16G16_FLOAT,
                    gpu.blurVolume);
            }
            if (SUCCEEDED(result)) {
                result = createTarget(
                    gpu.device.Get(),
                    halfWidth,
                    halfHeight,
                    DXGI_FORMAT_R32_FLOAT,
                    gpu.receiverDepth);
            }
            for (std::size_t index = 0;
                 index < gpu.historyVolume.size() && SUCCEEDED(result);
                 ++index) {
                result = createTarget(
                    gpu.device.Get(),
                    halfWidth,
                    halfHeight,
                    DXGI_FORMAT_R16G16_FLOAT,
                    gpu.historyVolume[index]);
                if (SUCCEEDED(result)) {
                    result = createTarget(
                        gpu.device.Get(),
                        halfWidth,
                        halfHeight,
                        DXGI_FORMAT_R32_FLOAT,
                        gpu.historyDepth[index]);
                }
            }
            gpu.sizeReady = SUCCEEDED(result);
            if (!gpu.sizeReady) {
                logging::error(
                    "Volumetric Lighting size-dependent resource creation failed (HRESULT=0x{:08X}, output={}x{}, quality={}).",
                    static_cast<std::uint32_t>(result),
                    fullWidth,
                    fullHeight,
                    quality);
                releaseSizeResources(gpu);
                return false;
            }
            gpu.fullWidth = fullWidth;
            gpu.fullHeight = fullHeight;
            gpu.halfWidth = halfWidth;
            gpu.halfHeight = halfHeight;
            gpu.volumeWidth = selected.width;
            gpu.volumeHeight = selected.height;
            gpu.volumeDepth = selected.depth;
            gpu.resourceQuality = quality;
            logging::info(
                "Volumetric Lighting resources created: output={}x{}, resolve={}x{}, froxel={}x{}x{}, quality={}.",
                fullWidth,
                fullHeight,
                halfWidth,
                halfHeight,
                selected.width,
                selected.height,
                selected.depth,
                quality);
            return true;
        }

        template <class Data>
        [[nodiscard]] bool upload(
            ID3D11DeviceContext* context,
            ID3D11Buffer* buffer,
            const Data& data) noexcept
        {
            if (!context || !buffer) {
                return false;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result = context->Map(
                buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result) || !mapped.pData) {
                return false;
            }
            std::memcpy(mapped.pData, std::addressof(data), sizeof(data));
            context->Unmap(buffer, 0);
            return true;
        }

        [[nodiscard]] bool cameraContinuous(
            const StereoFrame& current,
            const StereoFrame& previous) noexcept
        {
            if (!current.valid || !previous.valid) {
                return false;
            }
            constexpr auto maximumDistanceSquared =
                kTeleportDistance * kTeleportDistance;
            for (std::size_t eye = 0; eye < 2; ++eye) {
                float distanceSquared{};
                for (std::size_t component = 0; component < 3; ++component) {
                    const auto difference = current.eyeOrigin[eye][component] -
                        previous.eyeOrigin[eye][component];
                    if (!std::isfinite(difference)) {
                        return false;
                    }
                    distanceSquared += difference * difference;
                }
                if (!std::isfinite(distanceSquared) ||
                    distanceSquared > maximumDistanceSquared) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] float luminance(const std::array<float, 3>& value) noexcept
        {
            return value[0] * 0.2126f + value[1] * 0.7152f +
                value[2] * 0.0722f;
        }

        [[nodiscard]] FrameConstants makeFrameConstants(
            const Settings& settings,
            const StereoFrame& camera,
            const WeatherFrame& weather,
            bool cloudActive,
            float cloudOpacity,
            std::uint64_t frameIndex) noexcept
        {
            FrameConstants output{};
            for (std::size_t eye = 0; eye < 2; ++eye) {
                std::copy(
                    camera.eyeOrigin[eye].begin(),
                    camera.eyeOrigin[eye].end(),
                    output.eyeOrigin[eye]);
            }
            std::array<float, 3> air{};
            std::array<float, 3> forward{};
            std::array<float, 3> backward{};
            std::array<float, 3> extinction{};
            std::array<float, 3> color{};
            for (std::size_t component = 0; component < 3; ++component) {
                air[component] = std::max(
                    weather.medium[component] * kWeatherUnitScale, 0.0f);
                forward[component] = std::max(
                    weather.medium[3 + component] * kWeatherUnitScale, 0.0f);
                backward[component] = std::max(
                    weather.medium[6 + component] * kWeatherUnitScale, 0.0f);
                extinction[component] =
                    air[component] + forward[component] + backward[component];
                color[component] = air[component] + forward[component] * 0.35f;
            }
            const auto colorLuminance = std::max(luminance(color), 1.0e-7f);
            for (std::size_t component = 0; component < 3; ++component) {
                output.mediumColor[component] = std::clamp(
                    color[component] / colorLuminance, 0.25f, 4.0f);
            }
            output.mediumColor[3] = std::clamp(weather.intensity, 0.0f, 4.0f);
            const auto referenceExtinction = std::max(
                luminance(extinction), 1.0e-7f);
            const auto authoredExtinctionDistance = 1.0f / referenceExtinction;
            const auto distributionDistance = std::clamp(
                std::max(
                    authoredExtinctionDistance,
                    settings.maxDistance * 0.35f),
                512.0f,
                settings.maxDistance * 2.0f);
            output.volumeParams[0] = settings.maxDistance;
            output.volumeParams[1] = distributionDistance;
            output.volumeParams[2] = 0.0125f / settings.densityScale;
            output.volumeParams[3] = settings.densityContribution;
            output.applyParams[0] = settings.intensity;
            output.applyParams[1] = settings.baseScattering;
            output.applyParams[2] = settings.shaftIntensity;
            output.applyParams[3] = settings.phaseContribution;
            output.phaseParams[0] = std::clamp(weather.medium[9], -0.92f, 0.92f);
            output.phaseParams[1] = std::clamp(weather.medium[10], -0.92f, 0.92f);
            output.phaseParams[2] = std::max(luminance(forward), 1.0e-6f);
            output.phaseParams[3] = std::max(luminance(backward), 1.0e-6f);
            constexpr std::array<float, 8> phases{
                0.0f, 0.5f, 0.25f, 0.75f,
                0.125f, 0.625f, 0.375f, 0.875f
            };
            output.frameParams[0] = phases[frameIndex & 7u];
            output.frameParams[1] = static_cast<float>(frameIndex & 7u);
            output.frameParams[2] = linear_lighting::Runtime::get()
                .linearLightingEnabled() ? 1.0f : 0.0f;
            const auto seconds = static_cast<float>(
                static_cast<double>(GetTickCount64()) * 0.001);
            const auto wind = std::fmod(
                seconds * settings.windSpeed, 8192.0f);
            output.windParams[0] = wind * 0.73f;
            output.windParams[1] = wind * 0.37f;
            output.windParams[2] = 0.0f;
            output.windParams[3] = cloudActive ? 1.0f : 0.0f;
            output.cloudParams[0] = cloudOpacity;
            output.cloudParams[1] = kCloudHeight;
            output.cloudParams[2] = kPlanetRadius;
            return output;
        }

        void bindFullscreen(
            GpuState& gpu,
            ID3D11DeviceContext* context,
            UINT width,
            UINT height) noexcept
        {
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(gpu.fullscreenVertex.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            context->RSSetState(gpu.rasterState.Get());
            context->OMSetDepthStencilState(gpu.depthState.Get(), 0);
            constexpr float blendFactor[4]{};
            context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFu);
            const D3D11_VIEWPORT viewport{
                0.0f,
                0.0f,
                static_cast<float>(width),
                static_cast<float>(height),
                0.0f,
                1.0f
            };
            context->RSSetViewports(1, &viewport);
        }

        void clearPixelResources(ID3D11DeviceContext* context) noexcept
        {
            constexpr std::array<ID3D11ShaderResourceView*, 4> nullViews{};
            context->PSSetShaderResources(
                0, static_cast<UINT>(nullViews.size()), nullViews.data());
        }

        void clearComputeResources(ID3D11DeviceContext* context) noexcept
        {
            constexpr std::array<ID3D11ShaderResourceView*, 2> nullViews{};
            ID3D11UnorderedAccessView* nullOutput{};
            context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
            context->CSSetShaderResources(
                0, static_cast<UINT>(nullViews.size()), nullViews.data());
        }

        [[nodiscard]] bool acquireHostSurfaces(
            ID3D11DeviceContext* context,
            ID3D11Device* expectedDevice,
            ComPtr<ID3D11RenderTargetView>& outputTarget,
            ComPtr<ID3D11ShaderResourceView>& sceneSource,
            D3D11_TEXTURE2D_DESC& outputDescription) noexcept
        {
            ID3D11RenderTargetView* rawOutput{};
            context->OMGetRenderTargets(1, &rawOutput, nullptr);
            outputTarget.Attach(rawOutput);
            ID3D11ShaderResourceView* rawScene{};
            context->PSGetShaderResources(0, 1, &rawScene);
            sceneSource.Attach(rawScene);
            if (!outputTarget || !sceneSource || !expectedDevice) {
                return false;
            }
            ComPtr<ID3D11Resource> outputResource;
            ComPtr<ID3D11Resource> sceneResource;
            ComPtr<ID3D11Texture2D> outputTexture;
            ComPtr<ID3D11Texture2D> sceneTexture;
            outputTarget->GetResource(outputResource.GetAddressOf());
            sceneSource->GetResource(sceneResource.GetAddressOf());
            if (!outputResource || !sceneResource ||
                outputResource.Get() == sceneResource.Get() ||
                FAILED(outputResource.As(&outputTexture)) ||
                FAILED(sceneResource.As(&sceneTexture)) ||
                !outputTexture || !sceneTexture) {
                return false;
            }
            ComPtr<ID3D11Device> outputDevice;
            ComPtr<ID3D11Device> sceneDevice;
            outputTexture->GetDevice(outputDevice.GetAddressOf());
            sceneTexture->GetDevice(sceneDevice.GetAddressOf());
            D3D11_TEXTURE2D_DESC sceneDescription{};
            outputTexture->GetDesc(&outputDescription);
            sceneTexture->GetDesc(&sceneDescription);
            D3D11_SHADER_RESOURCE_VIEW_DESC sceneView{};
            sceneSource->GetDesc(&sceneView);
            return outputDevice.Get() == expectedDevice &&
                sceneDevice.Get() == expectedDevice &&
                sceneView.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
                outputDescription.Width == sceneDescription.Width &&
                outputDescription.Height == sceneDescription.Height &&
                outputDescription.ArraySize == 1 &&
                sceneDescription.ArraySize == 1 &&
                outputDescription.SampleDesc.Count == 1 &&
                sceneDescription.SampleDesc.Count == 1 &&
                outputDescription.Width >= 2 &&
                (outputDescription.Width & 1u) == 0 &&
                outputDescription.Height != 0;
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    bool Runtime::initializeNativeGate() noexcept
    {
        auto& value = state();
        value.moduleBase = REL::Module::get().base();
        auto* const record = reinterpret_cast<std::uint8_t*>(
            value.moduleBase + kSunbeamsSettingRecordRva);
        if (!readableRange(record, 0x18)) {
            logging::critical(
                "Volumetric Lighting rejected the bUseSunbeams host setting record.");
            return false;
        }
        std::uintptr_t vtable{};
        std::uintptr_t name{};
        std::memcpy(std::addressof(vtable), record, sizeof(vtable));
        std::memcpy(std::addressof(name), record + 0x10, sizeof(name));
        if (vtable != value.moduleBase + kIniSettingVtableRva ||
            name != value.moduleBase + kSunbeamsSettingNameRva ||
            !readableRange(reinterpret_cast<const void*>(name),
                sizeof(kSunbeamsSettingName)) ||
            std::memcmp(
                reinterpret_cast<const void*>(name),
                kSunbeamsSettingName,
                sizeof(kSunbeamsSettingName)) != 0 ||
            record[8] > 1) {
            logging::critical(
                "Volumetric Lighting rejected the bUseSunbeams host setting identity.");
            return false;
        }
        const auto* consumer = reinterpret_cast<const std::uint8_t*>(
            value.moduleBase + kSunbeamsConsumerRva);
        constexpr std::array<std::uint8_t, 2> compareRip{ 0x80, 0x3D };
        if (!readableRange(consumer, 7) ||
            std::memcmp(consumer, compareRip.data(), compareRip.size()) != 0) {
            logging::critical(
                "Volumetric Lighting rejected the native host availability consumer.");
            return false;
        }
        std::int32_t displacement{};
        std::memcpy(
            std::addressof(displacement), consumer + 2, sizeof(displacement));
        auto* const availability = reinterpret_cast<std::uint8_t*>(
            value.moduleBase + kSunbeamsAvailabilityRva);
        const auto target = value.moduleBase + kSunbeamsConsumerRva + 7 +
            displacement;
        if (target != reinterpret_cast<std::uintptr_t>(availability) ||
            !writableRange(record + 8, 1) ||
            !writableRange(availability, 1) || *availability > 1) {
            logging::critical(
                "Volumetric Lighting rejected the native host availability target.");
            return false;
        }
        value.settingValue = record + 8;
        value.availabilityValue = availability;
        value.nativeContractValid.store(true, std::memory_order_release);
        logging::info(
            "Volumetric Lighting owns the exact FO4VR ImageSpace host gate; bUseSunbeams remains an internal startup capability and is no longer user policy.");
        return true;
    }

    void Runtime::applyNativeGate() noexcept
    {
        auto& value = state();
        if (!value.nativeContractValid.load(std::memory_order_acquire)) {
            return;
        }
        writeBoolean(value.settingValue, true);
        const auto available = value.started.load(std::memory_order_acquire) &&
            value.engineDataValid.load(std::memory_order_acquire) &&
            value.enabled.load(std::memory_order_acquire) &&
            !value.diagnosticSuppressed.load(std::memory_order_acquire) &&
            !value.gpuFailed.load(std::memory_order_acquire);
        writeBoolean(value.availabilityValue, available);
    }

    bool Runtime::start(
        const Settings& settings,
        bool diagnosticSuppressed) noexcept
    {
        auto& value = state();
        applySettings(settings);
        value.diagnosticSuppressed.store(
            diagnosticSuppressed, std::memory_order_release);
        if (value.started.load(std::memory_order_acquire)) {
            applyNativeGate();
            return value.nativeContractValid.load(std::memory_order_acquire) &&
                value.engineDataValid.load(std::memory_order_acquire);
        }
        const auto engineData = initializeEngineData();
        value.engineDataValid.store(engineData, std::memory_order_release);
        if (!engineData || !initializeNativeGate()) {
            return false;
        }
        value.started.store(true, std::memory_order_release);
        applyNativeGate();
        return true;
    }

    void Runtime::onGameDataReady() noexcept
    {
        applyNativeGate();
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        auto& value = state();
        value.enabled.store(safe.enabled, std::memory_order_release);
        value.quality.store(safe.quality, std::memory_order_relaxed);
        value.intensity.store(safe.intensity, std::memory_order_relaxed);
        value.baseScattering.store(
            safe.baseScattering, std::memory_order_relaxed);
        value.shaftIntensity.store(
            safe.shaftIntensity, std::memory_order_relaxed);
        value.densityContribution.store(
            safe.densityContribution, std::memory_order_relaxed);
        value.densityScale.store(
            safe.densityScale, std::memory_order_relaxed);
        value.windSpeed.store(safe.windSpeed, std::memory_order_relaxed);
        value.phaseContribution.store(
            safe.phaseContribution, std::memory_order_relaxed);
        value.maxDistance.store(safe.maxDistance, std::memory_order_relaxed);
        value.temporalWeight.store(
            safe.temporalWeight, std::memory_order_relaxed);
        value.settingsRevision.fetch_add(1, std::memory_order_release);
        applyNativeGate();
    }

    void Runtime::setDiagnosticSuppressed(bool suppressed) noexcept
    {
        auto& value = state();
        const auto changed = value.diagnosticSuppressed.exchange(
            suppressed, std::memory_order_acq_rel) != suppressed;
        if (changed) {
            value.settingsRevision.fetch_add(1, std::memory_order_release);
            applyNativeGate();
        }
    }

    bool Runtime::requested() const noexcept
    {
        const auto& value = state();
        return value.started.load(std::memory_order_acquire) &&
            value.nativeContractValid.load(std::memory_order_acquire) &&
            value.engineDataValid.load(std::memory_order_acquire) &&
            value.enabled.load(std::memory_order_acquire) &&
            !value.diagnosticSuppressed.load(std::memory_order_acquire) &&
            !value.gpuFailed.load(std::memory_order_acquire);
    }

    HostShaderSelection Runtime::selectHostPixelShader(
        const void* bytecode,
        std::size_t bytecodeLength) noexcept
    {
        auto& value = state();
        if (!value.nativeContractValid.load(std::memory_order_acquire) ||
            !matchesHost(bytecode, bytecodeLength)) {
            return { bytecode, bytecodeLength, false };
        }
        value.hostMatches.fetch_add(1, std::memory_order_relaxed);
        return {
            fo4vr_cs_volumetric_host_ps,
            sizeof(fo4vr_cs_volumetric_host_ps),
            true
        };
    }

    void Runtime::recordHostPixelShaderCreation(
        const HostShaderSelection& selection,
        HRESULT result,
        ID3D11PixelShader* shader) noexcept
    {
        if (!selection.replaced()) {
            return;
        }
        auto& value = state();
        if (FAILED(result) || !shader) {
            value.gpuFailed.store(true, std::memory_order_release);
            applyNativeGate();
            logging::critical(
                "Volumetric Lighting host shader creation failed (HRESULT=0x{:08X}); the native host gate was disabled before a broken stock path could render.",
                static_cast<std::uint32_t>(result));
            return;
        }
        for (auto& slot : value.hostShaders) {
            auto* expected = static_cast<ID3D11PixelShader*>(nullptr);
            if (slot.compare_exchange_strong(
                    expected, shader, std::memory_order_release,
                    std::memory_order_relaxed) || expected == shader) {
                value.hostCreations.fetch_add(1, std::memory_order_relaxed);
                if (!value.firstHostLogged.exchange(
                        true, std::memory_order_relaxed)) {
                    logging::info(
                        "Volumetric Lighting replaced exact ImageSpace[126] with its scene-preserving host shader.");
                }
                return;
            }
        }
        logging::critical(
            "Volumetric Lighting host shader registry is full; the unmatched host remains fail-closed.");
    }

    bool Runtime::isHostPixelShader(ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return false;
        }
        const auto& value = state();
        for (const auto& slot : value.hostShaders) {
            if (slot.load(std::memory_order_acquire) == shader) {
                return true;
            }
        }
        return false;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        auto& value = state();
        value.gpu = {};
        for (auto& slot : value.hostShaders) {
            slot.store(nullptr, std::memory_order_release);
        }
        if (!device || !context ||
            context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            value.gpuFailed.store(true, std::memory_order_release);
            applyNativeGate();
            return;
        }
        value.gpu.device = device;
        value.gpu.context = context;
        const auto ready = createStaticResources(value.gpu);
        value.gpuFailed.store(!ready, std::memory_order_release);
        applyNativeGate();
        if (ready) {
            logging::info(
                "Volumetric Lighting GPU suite is ready: two-channel froxels, world density, weather phase, cloud modulation, temporal rejection, bilateral filtering, and linear HDR composition.");
        }
    }

    void Runtime::captureDirectionalFrame(
        ID3D11DeviceContext* context,
        bool directionalShaderActive) noexcept
    {
        auto& value = state();
        auto& gpu = value.gpu;
        if (!directionalShaderActive || !requested() || !context ||
            context != gpu.context.Get() || !gpu.staticReady) {
            return;
        }
        ID3D11ShaderResourceView* rawDepth{};
        ID3D11ShaderResourceView* rawShadow{};
        ID3D11SamplerState* rawSampler{};
        ID3D11Buffer* rawLight{};
        ID3D11Buffer* rawStereo{};
        ID3D11Buffer* rawCamera{};
        context->PSGetShaderResources(kNativeDepthSlot, 1, &rawDepth);
        context->PSGetShaderResources(kNativeShadowSlot, 1, &rawShadow);
        context->PSGetSamplers(kNativeShadowSamplerSlot, 1, &rawSampler);
        context->PSGetConstantBuffers(kNativeLightConstantSlot, 1, &rawLight);
        context->PSGetConstantBuffers(kNativeStereoConstantSlot, 1, &rawStereo);
        context->PSGetConstantBuffers(kNativeCameraConstantSlot, 1, &rawCamera);
        DirectionalFrame frame;
        frame.depth.Attach(rawDepth);
        frame.shadow.Attach(rawShadow);
        frame.comparisonSampler.Attach(rawSampler);
        frame.lightConstants.Attach(rawLight);
        frame.stereoConstants.Attach(rawStereo);
        frame.cameraConstants.Attach(rawCamera);
        UINT width{};
        UINT height{};
        if (!validDepth(frame.depth.Get(), gpu.device.Get(), width, height) ||
            !validShadow(frame.shadow.Get(), gpu.device.Get()) ||
            !validComparisonSampler(frame.comparisonSampler.Get()) ||
            !bufferAtLeast(frame.lightConstants.Get(), 46u * 16u) ||
            !bufferAtLeast(frame.stereoConstants.Get(), 16u) ||
            !bufferAtLeast(frame.cameraConstants.Get(), 51u * 16u)) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        frame.generation = gpu.directionalFrame.generation + 1;
        frame.settingsRevision = value.settingsRevision.load(
            std::memory_order_acquire);
        gpu.directionalFrame = std::move(frame);
        value.directionalCaptures.fetch_add(1, std::memory_order_relaxed);
        if (!value.firstCaptureLogged.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Volumetric Lighting captured its first exact DFLight frame (depth={}x{}, shadow=t5, comparison=s5, constants=b2/b8/b12).",
                width,
                height);
        }
    }

    void Runtime::renderAfterHostDraw(
        ID3D11DeviceContext* context,
        bool hostShaderActive) noexcept
    {
        auto& value = state();
        auto& gpu = value.gpu;
        if (!hostShaderActive || !requested()) {
            return;
        }
        if (!context || context != gpu.context.Get() || !gpu.staticReady ||
            !gpu.directionalFrame ||
            gpu.directionalFrame.settingsRevision !=
                value.settingsRevision.load(std::memory_order_acquire)) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            if (!value.firstRejectLogged.exchange(
                    true, std::memory_order_relaxed)) {
                logging::warn(
                    "Volumetric Lighting rejected its first host frame because no fresh exact DFLight capture was available.");
            }
            return;
        }

        DirectionalFrame frame = std::move(gpu.directionalFrame);
        gpu.directionalFrame = {};
        ComPtr<ID3D11RenderTargetView> outputTarget;
        ComPtr<ID3D11ShaderResourceView> sceneSource;
        D3D11_TEXTURE2D_DESC outputDescription{};
        if (!acquireHostSurfaces(
                context,
                gpu.device.Get(),
                outputTarget,
                sceneSource,
                outputDescription)) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        UINT depthWidth{};
        UINT depthHeight{};
        if (!validDepth(
                frame.depth.Get(),
                gpu.device.Get(),
                depthWidth,
                depthHeight) ||
            depthWidth != outputDescription.Width ||
            depthHeight != outputDescription.Height) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto camera = captureStereoFrame();
        if (!camera.valid) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            gpu.historyValid = false;
            gpu.previousCamera = {};
            return;
        }
        const auto now = GetTickCount64();
        if (!gpu.weather.valid ||
            now - gpu.weatherRefreshTick >= kWeatherRefreshMilliseconds) {
            const auto weather = captureWeatherFrame();
            gpu.weatherRefreshTick = now;
            if (weather.valid) {
                gpu.weather = weather;
            }
        }
        if (!gpu.weather.valid) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto settings = readSettings(value);
        if (!ensureSizeResources(
                gpu,
                outputDescription.Width,
                outputDescription.Height,
                settings.quality)) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto settingsRevision = value.settingsRevision.load(
            std::memory_order_acquire);
        if (gpu.appliedSettingsRevision != settingsRevision) {
            gpu.appliedSettingsRevision = settingsRevision;
            gpu.historyValid = false;
            gpu.previousCamera = {};
            gpu.historyWriteIndex = 0;
        }

        ID3D11ShaderResourceView* cloudCube{};
        ID3D11SamplerState* cloudSampler{};
        float cloudOpacity{};
        const auto cloudActive = cloud_shadows::Runtime::get().prepareLighting(
            context, cloudCube, cloudSampler, cloudOpacity);
        const auto frameConstants = makeFrameConstants(
            settings,
            camera,
            gpu.weather,
            cloudActive,
            cloudOpacity,
            gpu.frameIndex);
        const auto historyUsable = gpu.historyValid &&
            cameraContinuous(camera, gpu.previousCamera);
        const auto& previous = historyUsable ? gpu.previousCamera : camera;
        TemporalConstants temporal{};
        for (std::size_t eye = 0; eye < 2; ++eye) {
            std::copy(
                previous.eyeOrigin[eye].begin(),
                previous.eyeOrigin[eye].end(),
                temporal.previousEyeOrigin[eye]);
            std::copy(
                previous.viewProjection[eye].begin(),
                previous.viewProjection[eye].end(),
                temporal.previousViewProjection[eye]);
        }
        temporal.parameters[0] = historyUsable ? 1.0f : 0.0f;
        temporal.parameters[1] = settings.temporalWeight;
        temporal.parameters[2] = kDepthRejectStart;
        temporal.parameters[3] = kDepthRejectEnd;
        if (!upload(context, gpu.frameConstants.Get(), frameConstants) ||
            !upload(context, gpu.temporalConstants.Get(), temporal)) {
            value.rejectedFrames.fetch_add(1, std::memory_order_relaxed);
            gpu.historyValid = false;
            return;
        }

        InternalRenderScope internal;
        StateScope restore(context);
        const std::array<ID3D11Buffer*, 4> nativeConstants{
            frame.lightConstants.Get(),
            frame.stereoConstants.Get(),
            frame.cameraConstants.Get(),
            gpu.frameConstants.Get()
        };

        context->CSSetShader(gpu.generateCompute.Get(), nullptr, 0);
        const std::array<ID3D11ShaderResourceView*, 2> generationSources{
            frame.shadow.Get(), cloudActive ? cloudCube : nullptr
        };
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(generationSources.size()),
            generationSources.data());
        const std::array<ID3D11SamplerState*, 2> generationSamplers{
            frame.comparisonSampler.Get(), cloudActive ? cloudSampler : nullptr
        };
        context->CSSetSamplers(
            0,
            static_cast<UINT>(generationSamplers.size()),
            generationSamplers.data());
        context->CSSetConstantBuffers(
            0,
            static_cast<UINT>(nativeConstants.size()),
            nativeConstants.data());
        auto* volumeOutput = gpu.rawVolume.output.Get();
        context->CSSetUnorderedAccessViews(0, 1, &volumeOutput, nullptr);
        context->Dispatch(
            (gpu.volumeWidth + 7u) / 8u,
            (gpu.volumeHeight + 7u) / 8u,
            (gpu.volumeDepth + 3u) / 4u);
        clearComputeResources(context);

        context->CSSetShader(gpu.integrateCompute.Get(), nullptr, 0);
        auto* rawVolume = gpu.rawVolume.source.Get();
        context->CSSetShaderResources(0, 1, &rawVolume);
        volumeOutput = gpu.integratedVolume.output.Get();
        context->CSSetUnorderedAccessViews(0, 1, &volumeOutput, nullptr);
        context->Dispatch(
            (gpu.volumeWidth + 7u) / 8u,
            (gpu.volumeHeight + 7u) / 8u,
            1);
        clearComputeResources(context);

        bindFullscreen(gpu, context, gpu.halfWidth, gpu.halfHeight);
        std::array<ID3D11RenderTargetView*, 2> resolveTargets{
            gpu.currentVolume.target.Get(), gpu.receiverDepth.target.Get()
        };
        context->OMSetRenderTargets(
            static_cast<UINT>(resolveTargets.size()),
            resolveTargets.data(),
            nullptr);
        context->PSSetShader(gpu.resolvePixel.Get(), nullptr, 0);
        const std::array<ID3D11ShaderResourceView*, 2> resolveSources{
            frame.depth.Get(), gpu.integratedVolume.source.Get()
        };
        context->PSSetShaderResources(
            0,
            static_cast<UINT>(resolveSources.size()),
            resolveSources.data());
        auto* linearSampler = gpu.linearSampler.Get();
        context->PSSetSamplers(0, 1, &linearSampler);
        context->PSSetConstantBuffers(
            0,
            static_cast<UINT>(nativeConstants.size()),
            nativeConstants.data());
        context->Draw(3, 0);
        clearPixelResources(context);

        const auto writeIndex = gpu.historyWriteIndex;
        const auto readIndex = writeIndex ^ 1u;
        std::array<ID3D11RenderTargetView*, 2> temporalTargets{
            gpu.historyVolume[writeIndex].target.Get(),
            gpu.historyDepth[writeIndex].target.Get()
        };
        context->OMSetRenderTargets(
            static_cast<UINT>(temporalTargets.size()),
            temporalTargets.data(),
            nullptr);
        context->PSSetShader(gpu.temporalPixel.Get(), nullptr, 0);
        const std::array<ID3D11ShaderResourceView*, 4> temporalSources{
            gpu.currentVolume.source.Get(),
            gpu.receiverDepth.source.Get(),
            gpu.historyVolume[readIndex].source.Get(),
            gpu.historyDepth[readIndex].source.Get()
        };
        context->PSSetShaderResources(
            0,
            static_cast<UINT>(temporalSources.size()),
            temporalSources.data());
        context->PSSetSamplers(0, 1, &linearSampler);
        const std::array<ID3D11Buffer*, 5> temporalBuffers{
            frame.lightConstants.Get(),
            frame.stereoConstants.Get(),
            frame.cameraConstants.Get(),
            gpu.frameConstants.Get(),
            gpu.temporalConstants.Get()
        };
        context->PSSetConstantBuffers(
            0,
            static_cast<UINT>(temporalBuffers.size()),
            temporalBuffers.data());
        context->Draw(3, 0);
        clearPixelResources(context);

        auto* singleTarget = gpu.blurVolume.target.Get();
        context->OMSetRenderTargets(1, &singleTarget, nullptr);
        context->PSSetShader(gpu.blurHorizontalPixel.Get(), nullptr, 0);
        std::array<ID3D11ShaderResourceView*, 2> blurSources{
            gpu.historyVolume[writeIndex].source.Get(),
            gpu.receiverDepth.source.Get()
        };
        context->PSSetShaderResources(
            0,
            static_cast<UINT>(blurSources.size()),
            blurSources.data());
        context->Draw(3, 0);
        clearPixelResources(context);

        singleTarget = gpu.currentVolume.target.Get();
        context->OMSetRenderTargets(1, &singleTarget, nullptr);
        context->PSSetShader(gpu.blurVerticalPixel.Get(), nullptr, 0);
        blurSources[0] = gpu.blurVolume.source.Get();
        context->PSSetShaderResources(
            0,
            static_cast<UINT>(blurSources.size()),
            blurSources.data());
        context->Draw(3, 0);
        clearPixelResources(context);

        bindFullscreen(
            gpu, context, outputDescription.Width, outputDescription.Height);
        auto* output = outputTarget.Get();
        context->OMSetRenderTargets(1, &output, nullptr);
        context->PSSetShader(gpu.compositePixel.Get(), nullptr, 0);
        const std::array<ID3D11ShaderResourceView*, 4> compositeSources{
            gpu.currentVolume.source.Get(),
            gpu.receiverDepth.source.Get(),
            frame.depth.Get(),
            sceneSource.Get()
        };
        context->PSSetShaderResources(
            0,
            static_cast<UINT>(compositeSources.size()),
            compositeSources.data());
        context->PSSetConstantBuffers(
            0,
            static_cast<UINT>(nativeConstants.size()),
            nativeConstants.data());
        context->Draw(3, 0);
        clearPixelResources(context);

        gpu.previousCamera = camera;
        gpu.historyValid = true;
        gpu.historyWriteIndex = readIndex;
        ++gpu.frameIndex;
        const auto rendered = value.renderedFrames.fetch_add(
                                  1, std::memory_order_relaxed) +
            1;
        if (!value.firstRenderLogged.exchange(
                true, std::memory_order_relaxed)) {
            logging::info(
                "Volumetric Lighting rendered its first complete frame: froxel={}x{}x{}, two-channel lit/total integration, temporal={}, cloud={}, linearHDR={}, frame={}.",
                gpu.volumeWidth,
                gpu.volumeHeight,
                gpu.volumeDepth,
                historyUsable,
                cloudActive,
                linear_lighting::Runtime::get().linearLightingEnabled(),
                rendered);
        }
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        const auto& value = state();
        return {
            .settings = readSettings(value),
            .nativeContractValid = value.nativeContractValid.load(
                std::memory_order_acquire),
            .engineDataValid = value.engineDataValid.load(
                std::memory_order_acquire),
            .gpuReady = value.gpu.staticReady &&
                !value.gpuFailed.load(std::memory_order_acquire),
            .diagnosticSuppressed = value.diagnosticSuppressed.load(
                std::memory_order_acquire),
            .hostShaderObserved = value.hostCreations.load(
                std::memory_order_relaxed) != 0,
            .temporalHistoryValid = value.gpu.historyValid,
            .directionalCaptures = value.directionalCaptures.load(
                std::memory_order_relaxed),
            .renderedFrames = value.renderedFrames.load(
                std::memory_order_relaxed),
            .rejectedFrames = value.rejectedFrames.load(
                std::memory_order_relaxed),
        };
    }

    bool Runtime::internalRenderActive() noexcept
    {
        return gInternalRender;
    }
}
