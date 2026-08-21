#pragma once

#include "Features/skylighting/SkylightingSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace community_shaders::skylighting
{
    using NativePrecipitationRender =
        void(__fastcall*)(void* precipitation, void* rainEmitter);
    using NativeProjectionSetup =
        void(__fastcall*)(void* precipitation, void** cameraHolder);

    struct RuntimeSnapshot
    {
        bool requested{};
        bool gpuResourcesReady{};
        bool nativeHookOwned{};
        bool exteriorActive{};
        bool privateDepthReady{};
        bool probeDataValid{};
        Quality requestedQuality{ Quality::high };
        Quality activeQuality{ Quality::high };
        std::uint32_t probeWidth{};
        std::uint32_t probeHeight{};
        std::uint32_t probeDepth{};
        std::uint64_t captureCalls{};
        std::uint64_t privateDepthBinds{};
        std::uint64_t probeDispatches{};
        std::uint64_t rejectedCaptures{};
        std::uint64_t ambientBinds{};
    };

    class ScopedAmbientBindings final
    {
    public:
        ScopedAmbientBindings() = default;
        ScopedAmbientBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* probe,
            ID3D11Buffer* constants) noexcept;
        ~ScopedAmbientBindings();

        ScopedAmbientBindings(const ScopedAmbientBindings&) = delete;
        ScopedAmbientBindings& operator=(
            const ScopedAmbientBindings&) = delete;
        ScopedAmbientBindings(ScopedAmbientBindings&& other) noexcept;
        ScopedAmbientBindings& operator=(
            ScopedAmbientBindings&& other) noexcept;

        [[nodiscard]] bool restore() noexcept;

    private:
        ID3D11DeviceContext* context_{};
        ID3D11ShaderResourceView* previousProbe_{};
        ID3D11Buffer* previousConstants_{};
        bool captured_{};
    };

    class Runtime final
    {
    public:
        [[nodiscard]] static Runtime& get() noexcept;

        void applySettings(const Settings& settings) noexcept;
        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* immediateContext) noexcept;
        void setNativeHookOwned(bool owned) noexcept;
        void beginWorldSession() noexcept;

        // Called by the verified native precipitation wrapper detour after
        // the engine's original weather pass has completed. The manager is
        // resolved from FO4VR's persistent renderer state, not active weather.
        void onNativePrecipitationFrame(
            void* precipitation,
            NativePrecipitationRender render,
            NativeProjectionSetup restoreProjection) noexcept;

        // Called only from the immediate-context output-merger detours. The
        // returned DSV is borrowed for that one native call.
        [[nodiscard]] ID3D11DepthStencilView* substituteDepthStencil(
            ID3D11DeviceContext* context,
            ID3D11DepthStencilView* requested) noexcept;
        [[nodiscard]] bool privateCaptureActive() const noexcept;

        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] ScopedAmbientBindings scopeAmbientDraw(
            ID3D11DeviceContext* context,
            bool ambientReplacementActive) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;

        struct UInt4
        {
            std::uint32_t x{};
            std::uint32_t y{};
            std::uint32_t z{};
            std::uint32_t w{};
        };

        struct Int4
        {
            std::int32_t x{};
            std::int32_t y{};
            std::int32_t z{};
            std::int32_t w{};
        };

        struct Float4
        {
            float x{};
            float y{};
            float z{};
            float w{};
        };

        struct alignas(16) Constants
        {
            std::array<float, 16> occlusionViewProjection{};
            Float4 occlusionDirection{};
            Float4 arraySize{};
            Float4 cellSize{};
            Float4 positionOffset{};
            UInt4 arrayDimensions{};
            UInt4 arrayOrigin{};
            Int4 validMargin{};
            Float4 response{};
        };
        static_assert(sizeof(Constants) == 192);

        struct Dimensions
        {
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t depth{};
        };

        [[nodiscard]] static Dimensions dimensionsFor(Quality quality) noexcept;
        [[nodiscard]] bool createProbeResources() noexcept;
        [[nodiscard]] bool ensurePrivateDepth(
            ID3D11DepthStencilView* source) noexcept;
        void clearProbeResources() noexcept;
        void publishConstants(bool featureActive) noexcept;
        void dispatchProbeUpdate() noexcept;
        void updateRollingVolume(float x, float y, float z) noexcept;

        Settings startupSettings_{};
        Quality activeQuality_{ Quality::high };
        Dimensions dimensions_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> privateDepthTexture_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> privateDepthView_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> privateDepthResource_;
        Microsoft::WRL::ComPtr<ID3D11Texture3D> probeTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> probeResource_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> probeOutput_;
        Microsoft::WRL::ComPtr<ID3D11Texture3D> accumulationTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            accumulationResource_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            accumulationOutput_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateShader_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> comparisonSampler_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantsBuffer_;
        D3D11_TEXTURE2D_DESC privateDepthDescription_{};
        D3D11_DEPTH_STENCIL_VIEW_DESC privateDepthViewDescription_{};
        Constants constants_{};
        alignas(16) std::array<std::byte, 0xF10> nativeOutput_{};
        std::array<std::int64_t, 3> previousCell_{};
        bool previousCellValid_{};
        std::uint64_t publishedSettingsRevision_{};
        std::uint64_t publishedCaptureRevision_{};
        bool publishedFeatureActive_{};
        std::uint64_t captureRevision_{};

        std::atomic_bool enabled_{ true };
        std::atomic_uint32_t requestedQuality_{
            static_cast<std::uint32_t>(Quality::high) };
        std::atomic_uint32_t minimumDiffuseBits_{};
        std::atomic_uint32_t minimumSpecularBits_{};
        std::atomic_uint32_t maximumZenithBits_{};
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool gpuResourcesReady_{};
        std::atomic_bool nativeHookOwned_{};
        std::atomic_bool exteriorActive_{};
        std::atomic_bool privateDepthReady_{};
        std::atomic_bool probeDataValid_{};
        std::atomic_bool resetRequested_{ true };
        std::atomic_uint64_t frameIndex_{};
        std::atomic_uint64_t captureCalls_{};
        std::atomic_uint64_t privateDepthBinds_{};
        std::atomic_uint64_t probeDispatches_{};
        std::atomic_uint64_t rejectedCaptures_{};
        std::atomic_uint64_t ambientBinds_{};
        std::atomic_bool firstActiveAmbientBindLogged_{};
        std::atomic_bool qualityRestartWarningLogged_{};
    };
}
