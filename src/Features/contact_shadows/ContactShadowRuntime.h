#pragma once

#include "Features/contact_shadows/ContactShadowSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace community_shaders::contact_shadows
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool gpuReady{};
        std::uint32_t matchingShaders{};
        std::uint32_t trackedShaders{};
        std::uint64_t replacementBinds{};
        std::uint64_t maskDispatches{};
        std::uint64_t maskRebuilds{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t drawFallbacks{};
        std::uint64_t failures{};
    };

    struct ShaderBinding
    {
        ID3D11PixelShader* original{};
        ID3D11PixelShader* replacement{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return original && replacement;
        }
    };

    struct PixelShaderSelection
    {
        ID3D11PixelShader* shader{};
        ShaderBinding binding{};
    };

    class ScopedDrawBindings final
    {
    public:
        ScopedDrawBindings() noexcept = default;
        ~ScopedDrawBindings() noexcept;
        ScopedDrawBindings(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings(ScopedDrawBindings&&) = delete;
        ScopedDrawBindings& operator=(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings& operator=(ScopedDrawBindings&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedDrawBindings(
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            ID3D11ShaderResourceView* mask,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousMask_;
        std::atomic_uint64_t* restoreCounter_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            HRESULT(STDMETHODCALLTYPE* createPixelShader)(
                ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                ID3D11PixelShader**)) noexcept;
        void onPixelShaderCreated(
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11PixelShader* shader) noexcept;
        [[nodiscard]] PixelShaderSelection selectPixelShader(
            ID3D11PixelShader* requested) noexcept;
        [[nodiscard]] ScopedDrawBindings scopeDraw(
            ID3D11DeviceContext* context,
            ShaderBinding binding) noexcept;
        void recordDrawFallback() noexcept;
        [[nodiscard]] bool featureEnabled() const noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
        void uploadSettings(ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] bool ensureMaskResources(
            ID3D11ShaderResourceView* depth) noexcept;
        [[nodiscard]] bool dispatchMask(ID3D11DeviceContext* context) noexcept;

        static constexpr std::size_t kMaximumTrackedShaders = 8;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> maskCompute_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> maskTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> maskOutput_;
        UINT maskWidth_{};
        UINT maskHeight_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kMaximumTrackedShaders> originals_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool foveated_{ true };
        std::atomic<float> strength_{ 0.85f };
        std::atomic<float> maxDistance_{ 96.0f };
        std::atomic<float> fadeDistance_{ 2048.0f };
        std::atomic<float> thickness_{ 0.012f };
        std::atomic_uint32_t sampleCount_{ 8 };
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool resourcesReady_{};
        std::uint64_t uploadedRevision_{};
        std::atomic_uint32_t matchingShaders_{};
        std::atomic_uint32_t trackedShaders_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t maskDispatches_{};
        std::atomic_uint64_t maskRebuilds_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t drawFallbacks_{};
        std::atomic_uint64_t failures_{};
    };
}
