#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace csah::vanilla_fixes
{
    struct SslrEnvironmentSnapshot final
    {
        bool requested{};
        bool resourcesReady{};
        bool failed{};
        std::uint64_t bindings{};
        std::uint64_t historyBindings{};
        std::uint64_t bindingFailures{};
        std::uint64_t restoreFailures{};
    };

    // Creates the small private b11/s4 binding resources used by the exact
    // SSLR raytrace draw. CSAH' IBL runtime retains ownership of
    // all environment textures.
    [[nodiscard]] bool initializeSslrEnvironmentBinding(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept;
    void setSslrSuiteRequested(bool requested) noexcept;
    [[nodiscard]] bool sslrSuiteReady() noexcept;
    [[nodiscard]] SslrEnvironmentSnapshot sslrEnvironmentSnapshot() noexcept;

    // Render-thread-only transaction for t4..t9, s4, and b11. The scope
    // binds either the atomically published shared environment or explicit
    // null history with a zero-availability constant. It always restores the
    // exact previous state.
    class ScopedSslrEnvironmentBinding final
    {
    public:
        ScopedSslrEnvironmentBinding(
            ID3D11DeviceContext* context,
            bool exactCorrectedRaytrace) noexcept;
        ~ScopedSslrEnvironmentBinding();

        ScopedSslrEnvironmentBinding(
            const ScopedSslrEnvironmentBinding&) = delete;
        ScopedSslrEnvironmentBinding& operator=(
            const ScopedSslrEnvironmentBinding&) = delete;

        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool restore() noexcept;

    private:
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,
            6>
            previousResources_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> previousSampler_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        bool captured_{};
        bool active_{};
        bool restored_{};
    };
}
