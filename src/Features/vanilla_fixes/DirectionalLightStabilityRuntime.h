#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace community_shaders::vanilla_fixes
{
    struct DirectionalLightStabilitySnapshot final
    {
        bool enabled{};
        bool gpuReady{};
        bool sourceValid{};
        std::array<float, 3> worldDirection{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t failures{};
    };

    class ScopedDirectionalLightStabilityBinding final
    {
    public:
        ScopedDirectionalLightStabilityBinding() noexcept = default;
        ~ScopedDirectionalLightStabilityBinding() noexcept;
        ScopedDirectionalLightStabilityBinding(
            const ScopedDirectionalLightStabilityBinding&) = delete;
        ScopedDirectionalLightStabilityBinding(
            ScopedDirectionalLightStabilityBinding&&) = delete;
        ScopedDirectionalLightStabilityBinding& operator=(
            const ScopedDirectionalLightStabilityBinding&) = delete;
        ScopedDirectionalLightStabilityBinding& operator=(
            ScopedDirectionalLightStabilityBinding&&) = delete;

        [[nodiscard]] bool active() const noexcept
        {
            return context_ != nullptr;
        }

    private:
        friend class DirectionalLightStabilityRuntime;
        ScopedDirectionalLightStabilityBinding(
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        std::atomic_uint64_t* restoreCounter_{};
    };

    class DirectionalLightStabilityRuntime final
    {
    public:
        [[nodiscard]] static DirectionalLightStabilityRuntime& get() noexcept;

        void onDeviceCreated(ID3D11Device* device) noexcept;
        void setEnabled(bool enabled) noexcept;
        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] ScopedDirectionalLightStabilityBinding scopeDraw(
            ID3D11DeviceContext* context,
            bool active) noexcept;
        [[nodiscard]] DirectionalLightStabilitySnapshot snapshot()
            const noexcept;

    private:
        DirectionalLightStabilityRuntime() = default;

        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        std::array<float, 4> uploadedConstants_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool gpuReady_{};
        std::atomic_bool sourceValid_{};
        std::array<std::atomic_uint32_t, 3> worldDirectionBits_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstBindLogged_{};
        bool uploaded_{};
    };
}
