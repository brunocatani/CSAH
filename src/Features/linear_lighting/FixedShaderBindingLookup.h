#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace csah::linear_lighting
{
    // Fixed, append-only lookup for original shader pointers. Insertions are
    // serialized by Runtime::shaderRegistryMutex_; render-thread reads are
    // lock-free and never allocate.
    template <std::size_t Capacity>
    class FixedShaderBindingLookup final
    {
        static_assert(Capacity > 0);
        static_assert(std::has_single_bit(Capacity));

    public:
        [[nodiscard]] bool insert(
            const void* shader,
            std::uint64_t encodedBinding) noexcept
        {
            if (!shader || encodedBinding == 0) {
                return false;
            }

            auto index = bucket(shader);
            for (std::size_t probe = 0; probe < Capacity; ++probe) {
                auto& entry = entries_[index];
                const auto* existing =
                    entry.shader.load(std::memory_order_acquire);
                if (existing == shader) {
                    return entry.encodedBinding.load(
                               std::memory_order_relaxed) == encodedBinding;
                }
                if (!existing) {
                    entry.encodedBinding.store(
                        encodedBinding,
                        std::memory_order_relaxed);
                    entry.shader.store(shader, std::memory_order_release);
                    return true;
                }
                index = (index + 1) & (Capacity - 1);
            }
            return false;
        }

        [[nodiscard]] std::uint64_t find(
            const void* shader) const noexcept
        {
            if (!shader) {
                return 0;
            }

            auto index = bucket(shader);
            for (std::size_t probe = 0; probe < Capacity; ++probe) {
                const auto& entry = entries_[index];
                const auto* existing =
                    entry.shader.load(std::memory_order_acquire);
                if (!existing) {
                    return 0;
                }
                if (existing == shader) {
                    return entry.encodedBinding.load(
                        std::memory_order_relaxed);
                }
                index = (index + 1) & (Capacity - 1);
            }
            return 0;
        }

    private:
        struct Entry
        {
            std::atomic<const void*> shader{};
            std::atomic_uint64_t encodedBinding{};
        };

        [[nodiscard]] static std::size_t bucket(
            const void* shader) noexcept
        {
            auto value = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(shader));
            value ^= value >> 33;
            value *= 0xff51afd7ed558ccdULL;
            value ^= value >> 33;
            value *= 0xc4ceb9fe1a85ec53ULL;
            value ^= value >> 33;
            return static_cast<std::size_t>(value) & (Capacity - 1);
        }

        std::array<Entry, Capacity> entries_{};
    };
}
