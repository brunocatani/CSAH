#pragma once

#include "Features/ibl/IblProjectionModel.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

namespace community_shaders::ibl
{
    struct RuntimeSnapshot
    {
        bool resourcesReady{};
        bool nativeCubemapReady{};
        std::uint64_t cadenceTicks{};
        std::uint64_t projectionDispatches{};
        std::uint64_t completedReadbacks{};
        std::uint64_t invalidReadbacks{};
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

        [[nodiscard]] bool createResources() noexcept;
        [[nodiscard]] bool refreshNativeCubemap() noexcept;
        void consumeCompletedReadbacks() noexcept;
        void dispatchProjection() noexcept;
        void publish(const DiffuseSH& coefficients) noexcept;
        void resetResources() noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> projectionShader_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> projectionTexture_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> projectionUav_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nativeCubemapSrv_;
        std::array<ReadbackSlot, 3> readbackRing_{};
        std::uint64_t nextGeneration_{ 1 };
        std::uint64_t lastPublishedGeneration_{};
        std::uint64_t nextCadenceTickMilliseconds_{};
        bool loggedSourceReady_{};
        bool loggedFirstReadback_{};
        bool loggedSourceUnavailable_{};
        bool loggedReadbackFailure_{};

        std::atomic_bool resourcesReady_{};
        std::atomic_bool nativeCubemapReady_{};
        std::atomic_uint64_t cadenceTicks_{};
        std::atomic_uint64_t projectionDispatches_{};
        std::atomic_uint64_t completedReadbacks_{};
        std::atomic_uint64_t invalidReadbacks_{};
        std::atomic_uint64_t publishedSequence_{};
        std::array<std::atomic_uint32_t, 12> publishedCoefficientBits_{};
    };
}
