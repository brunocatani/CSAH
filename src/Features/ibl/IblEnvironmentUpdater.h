#pragma once

#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblProviderModel.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace community_shaders::ibl
{
    enum class EnvironmentDiagnosticConsumeResult : std::uint8_t
    {
        idle,
        pending,
        completed,
        failed,
    };

    struct EnvironmentDiagnosticSummary
    {
        bool initialized{};
        bool pending{};
        std::uint64_t dispatches{};
        std::uint64_t completedReadbacks{};
        std::uint64_t failedUpdates{};
        std::uint64_t generation{};
        Float3 average{};
        std::array<float, kEnvironmentCubeFaceCount> faceAverageLuminance{};
        float peak{};
        std::uint32_t nonBlackSamples{};
        std::uint32_t sampleCount{};
    };

    // Render-thread-only diagnostic updater. One dispatch fills every face
    // and mip of the provider's private back chain from the exact packed
    // stereo reflection-free source, stages mip zero for nonblocking
    // readback, and aborts the provider generation without publication.
    class EnvironmentUpdater final
    {
    public:
        [[nodiscard]] bool initialize(
            ID3D11Device* device,
            const void* shaderBytecode,
            std::size_t shaderBytecodeSize,
            std::uint32_t extent) noexcept;
        void reset() noexcept;

        [[nodiscard]] bool dispatchDiagnostic(
            ID3D11DeviceContext* context,
            EnvironmentProvider& provider,
            ID3D11ShaderResourceView* reflectionFreeRadiance,
            ID3D11ShaderResourceView* sceneDepth,
            ID3D11Buffer* sceneConstants) noexcept;

        [[nodiscard]] EnvironmentDiagnosticConsumeResult consumeDiagnostic(
            ID3D11DeviceContext* context) noexcept;

        [[nodiscard]] EnvironmentDiagnosticSummary snapshot() const noexcept
        {
            return summary_;
        }

    private:
        struct Resources
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
            Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
            Microsoft::WRL::ComPtr<ID3D11Buffer> updateConstants;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingMipZero;
            Microsoft::WRL::ComPtr<ID3D11Query> completionEvent;
            std::uint32_t extent{};
        };

        struct UpdateConstants
        {
            std::uint32_t sourceWidth{};
            std::uint32_t sourceHeight{};
            std::uint32_t targetExtent{};
            std::uint32_t reserved{};
        };

        static_assert(sizeof(UpdateConstants) == 16);

        [[nodiscard]] bool validateInputs(
            ID3D11DeviceContext* context,
            const EnvironmentProvider& provider,
            ID3D11ShaderResourceView* reflectionFreeRadiance,
            ID3D11ShaderResourceView* sceneDepth,
            ID3D11Buffer* sceneConstants,
            D3D11_TEXTURE2D_DESC& radianceDescription) const noexcept;
        void recordFailure() noexcept;

        Resources resources_{};
        EnvironmentDiagnosticSummary summary_{};
        std::uint64_t nextGeneration_{ 1 };
    };
}
