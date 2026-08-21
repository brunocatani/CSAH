#pragma once

#include <cstdint>

namespace community_shaders::skylighting
{
    struct OcclusionPassProducerSnapshot
    {
        bool owned{};
        std::uint64_t calls{};
        std::uint64_t pass14ResolverCalls{};
        std::uint64_t accumulatorGeometryVisits{};
        std::uint64_t accumulatorGeometryVisitModeMask{};
        std::uint64_t forcedPrivateCpuCulling{};
        std::uint64_t emittedPasses{};
        std::uint64_t collectedPasses{};
        std::uint64_t rejectedInvalid{};
        std::uint64_t rejectedSkinned{};
        std::uint64_t rejectedSmall{};
        std::uint64_t rejectedBsx{};
        std::uint64_t rejectedFlags{};
        std::uint64_t rejectedAllocation{};
    };

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
    [[nodiscard]] OcclusionPassProducerSnapshot
        occlusionPassProducerSnapshot() noexcept;
}
