#pragma once

#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace community_shaders::filmic_tonemapping
{
    using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
        ID3D11Device*,
        const void*,
        SIZE_T,
        ID3D11ClassLinkage*,
        ID3D11PixelShader**);

    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool gpuReady{};
        std::uint32_t matchingShaders{};
        std::uint32_t trackedShaders{};
        std::uint64_t replacementBinds{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t failures{};
    };

    struct ShaderBinding final
    {
        ID3D11PixelShader* original{};
        ID3D11PixelShader* replacement{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return original && replacement;
        }
    };

    struct PixelShaderSelection final
    {
        ID3D11PixelShader* shader{};
        ShaderBinding binding{};
    };

    class Runtime;

    class ScopedConstants final
    {
    public:
        ScopedConstants() noexcept = default;
        ~ScopedConstants() noexcept;
        ScopedConstants(const ScopedConstants&) = delete;
        ScopedConstants(ScopedConstants&&) = delete;
        ScopedConstants& operator=(const ScopedConstants&) = delete;
        ScopedConstants& operator=(ScopedConstants&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedConstants(
            Runtime* owner,
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants) noexcept;

        Runtime* owner_{};
        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previous_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            CreatePixelShaderFunction createPixelShader) noexcept;
        void onPixelShaderCreated(
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11PixelShader* shader) noexcept;
        [[nodiscard]] PixelShaderSelection selectPixelShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* requested) noexcept;
        [[nodiscard]] ScopedConstants scopeDraw(
            ID3D11DeviceContext* context,
            ShaderBinding binding) noexcept;
        [[nodiscard]] bool featureEnabled() const noexcept;
        void setOutputFeatureRequested(bool requested) noexcept;
        [[nodiscard]] bool bindingActive(ShaderBinding binding) const noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        friend class ScopedConstants;
        Runtime() = default;

        struct alignas(16) GpuConstants final
        {
            float exposureMultiplier{ 1.0f };
            float nativeAdaptationWeight{ 1.0f };
            float filmicStrength{ 1.0f };
            float whitePointScale{ 1.0f };
        };

        static constexpr std::size_t kMaximumTrackedShaders = 4;
        [[nodiscard]] GpuConstants gpuConstants() const noexcept;
        void uploadConstants(ID3D11DeviceContext* context) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_{};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kMaximumTrackedShaders> originals_{};
        std::atomic_uint32_t trackedShaders_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool outputFeatureRequested_{};
        std::atomic_bool nativeAutoExposure_{ true };
        std::atomic<float> exposureCompensationEV_{};
        std::atomic<float> filmicStrength_{ 1.0f };
        std::atomic<float> whitePointScale_{ 1.0f };
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::uint64_t uploadedRevision_{};
        std::atomic_bool resourcesReady_{};
        std::atomic_uint32_t matchingShaders_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstMatchLogged_{};
        std::atomic_bool firstBindLogged_{};
    };
}
