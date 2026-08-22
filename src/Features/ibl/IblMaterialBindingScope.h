#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace community_shaders::ibl
{
    enum class MaterialBindingRejection : std::uint8_t
    {
        none,
        nullContext,
        missingConstants,
        deviceMismatch,
        applyFailed,
    };

    // Render-thread-only transaction for the six bindings added to exact
    // DFComposite replacement shaders. Construction captures and validates
    // the complete t29..t33/b5 state; destruction restores that state exactly.
    // Null views are intentional for the reflection-free duplicate, whose
    // explicit zero-weight b5 keeps both material paths disabled.
    class ScopedMaterialBindings final
    {
    public:
        static constexpr UINT kAlbedoSlot = 29;
        static constexpr UINT kRadianceSlot = 30;
        static constexpr UINT kValiditySlot = 31;
        static constexpr UINT kPreviousRadianceSlot = 32;
        static constexpr UINT kPreviousValiditySlot = 33;
        static constexpr UINT kConstantSlot = 5;

        ScopedMaterialBindings() noexcept = default;
        ScopedMaterialBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* albedo,
            ID3D11ShaderResourceView* radiance,
            ID3D11ShaderResourceView* validity,
            ID3D11ShaderResourceView* previousRadiance,
            ID3D11ShaderResourceView* previousValidity,
            ID3D11Buffer* constants) noexcept;
        ~ScopedMaterialBindings();

        ScopedMaterialBindings(const ScopedMaterialBindings&) = delete;
        ScopedMaterialBindings& operator=(
            const ScopedMaterialBindings&) = delete;
        ScopedMaterialBindings(ScopedMaterialBindings&& other) noexcept;
        ScopedMaterialBindings& operator=(
            ScopedMaterialBindings&& other) noexcept;

        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool restore() noexcept;
        [[nodiscard]] MaterialBindingRejection rejection() const noexcept;

    private:
        [[nodiscard]] bool restoreCaptured() noexcept;
        void moveFrom(ScopedMaterialBindings&& other) noexcept;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,
            5>
            previousResources_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        MaterialBindingRejection rejection_{
            MaterialBindingRejection::nullContext
        };
        bool captured_{};
        bool active_{};
        bool restored_{};
    };
}
