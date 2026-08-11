#pragma once

#include "Features/ibl/IblCaptureProbeModel.h"
#include "Features/ibl/IblProjectionModel.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <mutex>

namespace community_shaders::ibl
{
    struct RuntimeSnapshot
    {
        bool resourcesReady{};
        bool nativeCubemapReady{};
        bool diffuseSHUsable{};
        std::uint64_t cadenceTicks{};
        std::uint64_t projectionDispatches{};
        std::uint64_t completedReadbacks{};
        std::uint64_t blackReadbacks{};
        std::uint64_t usableReadbacks{};
        std::uint64_t invalidReadbacks{};
        std::uint64_t matchingCaptureShaders{};
        std::uint64_t completedCaptureProbes{};
        std::uint64_t latestSampleGeneration{};
        std::uint64_t latestSampleTickMilliseconds{};
        DiffuseSH latestDiffuseSH{};
    };

    class Runtime
    {
    public:
        static Runtime& get() noexcept;

        // Render-thread only. The device/context are retained for the process
        // lifetime; no engine pointer is retained by this subsystem.
        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* immediateContext) noexcept;

        // Creation-time identity registration is bounded and retains only
        // exact local-FXP matches. Draw-time lookup is allocation-free.
        void onPixelShaderCreated(
            const void* bytecode,
            std::size_t bytecodeSize,
            ID3D11PixelShader* shader) noexcept;

        [[nodiscard]] std::uint16_t captureProbeForShader(
            ID3D11PixelShader* shader) const noexcept;

        // Called only inside the existing PostLoadGame/NewGame qualification
        // window. It reads current D3D11 bindings once per exact contract,
        // changes no state, and distinguishes world evidence from menu draws.
        void onCaptureProbeDraw(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne) noexcept;

        // Called only at an exact DFLight ambient shader bind. The initial
        // observe-only stage performs at most one tiny projection every
        // 250 ms, never waits for the GPU, and does not alter lighting.
        void onDFLightAmbientBind(ID3D11DeviceContext* context) noexcept;

        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        struct ReadbackSlot
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            bool pending{};
            std::uint64_t generation{};
        };

        static constexpr std::size_t kCaptureShaderSlotCount = 128;

        struct CaptureShaderSlot
        {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> owner;
            std::atomic<ID3D11PixelShader*> shader{};
            std::atomic<std::uint16_t> contractPlusOne{};
        };

        [[nodiscard]] bool createResources() noexcept;
        [[nodiscard]] bool refreshNativeCubemap() noexcept;
        void consumeCompletedReadbacks() noexcept;
        void dispatchProjection() noexcept;
        void publishUsable(
            const DiffuseSH& coefficients,
            std::uint64_t generation,
            std::uint64_t tickMilliseconds) noexcept;
        void publishUnavailable(
            std::uint64_t generation,
            std::uint64_t tickMilliseconds) noexcept;
        void resetCaptureProbes() noexcept;
        void resetResources() noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> projectionShader_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> projectionTexture_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> projectionUav_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nativeCubemapSrv_;
        std::array<ReadbackSlot, 3> readbackRing_{};
        mutable std::mutex captureShaderMutex_;
        std::array<CaptureShaderSlot, kCaptureShaderSlotCount>
            captureShaderSlots_{};
        std::array<std::atomic_bool, kCaptureProbeContracts.size()>
            captureProbeLogged_{};
        std::uint64_t nextGeneration_{ 1 };
        std::uint64_t lastProcessedGeneration_{};
        std::uint64_t nextCadenceTickMilliseconds_{};
        std::uint32_t consecutiveBlackReadbacks_{};
        bool loggedSourceReady_{};
        bool loggedFirstReadback_{};
        bool loggedFirstUsableReadback_{};
        bool loggedBlackStreak_{};
        bool loggedSourceUnavailable_{};
        bool loggedReadbackFailure_{};

        std::atomic_bool resourcesReady_{};
        std::atomic_bool nativeCubemapReady_{};
        std::atomic_uint64_t cadenceTicks_{};
        std::atomic_uint64_t projectionDispatches_{};
        std::atomic_uint64_t completedReadbacks_{};
        std::atomic_uint64_t blackReadbacks_{};
        std::atomic_uint64_t usableReadbacks_{};
        std::atomic_uint64_t invalidReadbacks_{};
        std::atomic_uint64_t matchingCaptureShaders_{};
        std::atomic_uint64_t completedCaptureProbes_{};
        std::atomic_bool captureRegistryOverflowLogged_{};
        std::atomic_uint64_t publishedSequence_{};
        std::atomic_bool publishedUsable_{};
        std::atomic_uint64_t publishedGeneration_{};
        std::atomic_uint64_t publishedTickMilliseconds_{};
        std::array<std::atomic_uint32_t, 12> publishedCoefficientBits_{};
    };
}
