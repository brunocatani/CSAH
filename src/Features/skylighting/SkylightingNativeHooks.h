#pragma once

namespace community_shaders::skylighting
{
    class ScopedOcclusionPassProduction final
    {
    public:
        ScopedOcclusionPassProduction() noexcept;
        ~ScopedOcclusionPassProduction() noexcept;

        ScopedOcclusionPassProduction(
            const ScopedOcclusionPassProduction&) = delete;
        ScopedOcclusionPassProduction& operator=(
            const ScopedOcclusionPassProduction&) = delete;

        [[nodiscard]] bool active() const noexcept;

    private:
        bool active_{};
    };

    [[nodiscard]] bool installNativeHooks() noexcept;
    [[nodiscard]] bool validateNativeHooks(const char* trigger) noexcept;
}
