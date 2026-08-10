#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace community_shaders::linear_lighting
{
    inline constexpr std::size_t kContractMaskWordBits = 64;
    inline constexpr std::size_t kContractMaskWordCount = 5;
    inline constexpr std::size_t kContractMaskCapacity =
        kContractMaskWordBits * kContractMaskWordCount;

    template <std::size_t WordCount>
    using FixedContractMask = std::array<std::uint64_t, WordCount>;

    using ContractMask = FixedContractMask<kContractMaskWordCount>;
    static_assert(std::atomic_uint64_t::is_always_lock_free);

    struct ContractBit
    {
        std::size_t word{};
        std::uint64_t value{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return value != 0;
        }
    };

    [[nodiscard]] constexpr ContractBit contractBit(
        std::size_t contractIndex) noexcept
    {
        if (contractIndex >= kContractMaskCapacity) {
            return {};
        }
        return {
            contractIndex / kContractMaskWordBits,
            1ull << (contractIndex % kContractMaskWordBits),
        };
    }

    template <std::size_t WordCount>
    [[nodiscard]] constexpr ContractBit fixedContractBit(
        std::size_t contractIndex) noexcept
    {
        if (contractIndex >= kContractMaskWordBits * WordCount) {
            return {};
        }
        return {
            contractIndex / kContractMaskWordBits,
            1ull << (contractIndex % kContractMaskWordBits),
        };
    }

    constexpr void setContractBit(
        ContractMask& mask,
        std::size_t contractIndex) noexcept
    {
        const auto bit = contractBit(contractIndex);
        if (bit) {
            mask[bit.word] |= bit.value;
        }
    }

    constexpr void clearContractBit(
        ContractMask& mask,
        std::size_t contractIndex) noexcept
    {
        const auto bit = contractBit(contractIndex);
        if (bit) {
            mask[bit.word] &= ~bit.value;
        }
    }

    [[nodiscard]] constexpr bool contractBitSet(
        const ContractMask& mask,
        std::size_t contractIndex) noexcept
    {
        const auto bit = contractBit(contractIndex);
        return bit && (mask[bit.word] & bit.value) != 0;
    }

    [[nodiscard]] constexpr bool anyContractBit(
        const ContractMask& mask) noexcept
    {
        for (const auto word : mask) {
            if (word != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool containsContractMask(
        const ContractMask& observed,
        const ContractMask& expected) noexcept
    {
        for (std::size_t index = 0; index < observed.size(); ++index) {
            if ((observed[index] & expected[index]) != expected[index]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr ContractMask intersectContractMasks(
        const ContractMask& first,
        const ContractMask& second,
        const ContractMask& third) noexcept
    {
        ContractMask result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = first[index] & second[index] & third[index];
        }
        return result;
    }

    [[nodiscard]] constexpr ContractMask expectedContractMask(
        std::size_t contractCount) noexcept
    {
        ContractMask result{};
        auto remaining = contractCount < kContractMaskCapacity ?
            contractCount : kContractMaskCapacity;
        for (auto& word : result) {
            if (remaining >= kContractMaskWordBits) {
                word = (std::numeric_limits<std::uint64_t>::max)();
                remaining -= kContractMaskWordBits;
            } else if (remaining != 0) {
                word = (1ull << remaining) - 1ull;
                remaining = 0;
            }
        }
        return result;
    }

    template <std::size_t WordCount>
    class AtomicFixedContractMask final
    {
    public:
        void clear(
            std::memory_order order = std::memory_order_relaxed) noexcept
        {
            for (auto& word : words_) {
                word.store(0, order);
            }
        }

        void set(
            std::size_t contractIndex,
            std::memory_order order = std::memory_order_relaxed) noexcept
        {
            const auto bit = fixedContractBit<WordCount>(contractIndex);
            if (bit) {
                words_[bit.word].fetch_or(bit.value, order);
            }
        }

        [[nodiscard]] bool test(
            std::size_t contractIndex,
            std::memory_order order = std::memory_order_relaxed) const noexcept
        {
            const auto bit = fixedContractBit<WordCount>(contractIndex);
            return bit &&
                (words_[bit.word].load(order) & bit.value) != 0;
        }

        [[nodiscard]] FixedContractMask<WordCount> load(
            std::memory_order order = std::memory_order_relaxed) const noexcept
        {
            FixedContractMask<WordCount> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = words_[index].load(order);
            }
            return result;
        }

    private:
        std::array<std::atomic_uint64_t, WordCount> words_{};
    };

    using AtomicContractMask =
        AtomicFixedContractMask<kContractMaskWordCount>;
}
