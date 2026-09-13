#pragma once

#include <cstdint>

struct ID3D11DepthStencilView;
struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11ShaderResourceView;

namespace csah::vanilla_fixes
{
    struct FocusShadowSnapshot final
    {
        bool nativeHooksInstalled{};
        std::uint64_t exactMapTargetsObserved{};
        std::uint64_t fullArrayViewsCreated{};
        std::uint64_t bindingsApplied{};
        std::uint64_t bindingFailures{};
    };

    [[nodiscard]] bool installFocusShadowNativeHooks() noexcept;
    void registerFocusShadowPixelShader(
        ID3D11PixelShader* shader) noexcept;
    [[nodiscard]] bool isFocusShadowPixelShader(
        ID3D11PixelShader* shader) noexcept;
    void observeFocusShadowRenderTargets(
        ID3D11DepthStencilView* depthTarget) noexcept;

    class ScopedFocusShadowBinding final
    {
    public:
        ScopedFocusShadowBinding(
            ID3D11DeviceContext* context,
            bool enabled) noexcept;
        ~ScopedFocusShadowBinding() noexcept;

        ScopedFocusShadowBinding(const ScopedFocusShadowBinding&) = delete;
        ScopedFocusShadowBinding& operator=(
            const ScopedFocusShadowBinding&) = delete;

    private:
        ID3D11DeviceContext* context_{};
        ID3D11ShaderResourceView* original_{};
        bool applied_{};
    };

    [[nodiscard]] FocusShadowSnapshot focusShadowSnapshot() noexcept;
}
