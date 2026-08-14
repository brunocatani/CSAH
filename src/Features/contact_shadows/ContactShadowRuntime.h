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
        std::uint64_t constantScopes{};
        std::uint64_t constantRestores{};
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
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previous_;
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
        [[nodiscard]] ScopedConstants scopeConstants(
            ID3D11DeviceContext* context,
            ShaderBinding binding) noexcept;
        [[nodiscard]] bool featureEnabled() const noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
        void uploadSettings(ID3D11DeviceContext* context) noexcept;

        static constexpr std::size_t kMaximumTrackedShaders = 8;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
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
        std::atomic_uint64_t constantScopes_{};
        std::atomic_uint64_t constantRestores_{};
        std::atomic_uint64_t failures_{};
    };
}
