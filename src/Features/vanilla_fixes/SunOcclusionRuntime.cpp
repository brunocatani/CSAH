#include "PCH.h"

#include "Features/vanilla_fixes/SunOcclusionRuntime.h"

#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::uintptr_t kAggregateSunOcclusionRva = 0x0281DD70;
        constexpr std::uint32_t kMinimumAcceptedCoveragePixels = 10;
        constexpr std::size_t kSunPixelCountOffset = 0xF610;
        constexpr std::size_t kWaitingForSunQueryOffset = 0xF614;
        constexpr std::size_t kStoredSunOcclusionOffset = 0xF618;
        constexpr std::size_t kSunTestsOffset = 0xF620;
        constexpr std::size_t kSunTestCount = 3;

        constexpr std::array<std::uint8_t, 26> kAggregateEntry{
            0x4C, 0x8D, 0x89, 0x20, 0xF6, 0x00, 0x00,
            0x0F, 0x57, 0xD2,
            0x41, 0xB8, 0xFF, 0xFF, 0xFF, 0x7F,
            0x4D, 0x8D, 0x59, 0x48,
            0x41, 0xB2, 0x01,
            0x0F, 0x28, 0xCA,
        };

        struct SunOcclusionTest final
        {
            std::uintptr_t query{};
            float percentOccluded{};
            std::uint32_t pixelCount{};
            std::uint32_t frameCount{};
            bool waiting{};
            std::array<std::byte, 3> padding{};
        };
        static_assert(sizeof(SunOcclusionTest) == 0x18);

        using AggregateFunction = void(__fastcall*)(std::uintptr_t);

        AggregateFunction originalAggregate{};
        std::atomic_bool hookInstalled{};
        std::atomic_bool fixEnabled{};
        std::atomic_bool firstCorrectionLogged{};
        std::atomic_uint64_t correctedAggregates{};

        [[nodiscard]] bool executableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    address,
                    &information,
                    sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto protection = information.Protect & 0xFFu;
            const auto executable = protection == PAGE_EXECUTE ||
                protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            const auto regionEnd = regionBegin + information.RegionSize;
            return executable && begin >= regionBegin && begin <= regionEnd &&
                size <= regionEnd - begin;
        }

        template <std::size_t Size>
        [[nodiscard]] bool exactEntry(
            const void* address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            return executableRange(address, expected.size()) &&
                std::memcmp(address, expected.data(), expected.size()) == 0;
        }

        template <class T>
        [[nodiscard]] T& field(
            const std::uintptr_t accumulator,
            const std::size_t offset) noexcept
        {
            return *reinterpret_cast<T*>(accumulator + offset);
        }

        void aggregateStereoSunOcclusion(
            const std::uintptr_t accumulator) noexcept
        {
            auto* tests = reinterpret_cast<SunOcclusionTest*>(
                accumulator + kSunTestsOffset);
            auto allQueriesReady = true;
            auto anyObservedQuery = false;
            auto anyAcceptedQuery = false;
            auto rejectedLowCoverageQuery = false;
            auto smallestObservedPixelCount =
                (std::numeric_limits<std::uint32_t>::max)();
            auto smallestAcceptedPixelCount =
                (std::numeric_limits<std::uint32_t>::max)();
            float maximumAcceptedOcclusion{};

            for (std::size_t index = 0; index < kSunTestCount; ++index) {
                auto& test = tests[index];
                if (test.waiting) {
                    ++test.frameCount;
                    field<bool>(accumulator, kWaitingForSunQueryOffset) = true;
                    allQueriesReady = false;
                }
                if (!(test.percentOccluded > 0.0f) && test.pixelCount == 0) {
                    continue;
                }

                anyObservedQuery = true;
                if (test.pixelCount < smallestObservedPixelCount) {
                    smallestObservedPixelCount = test.pixelCount;
                }
                if (test.pixelCount < kMinimumAcceptedCoveragePixels) {
                    rejectedLowCoverageQuery = true;
                    continue;
                }

                anyAcceptedQuery = true;
                if (test.pixelCount < smallestAcceptedPixelCount) {
                    smallestAcceptedPixelCount = test.pixelCount;
                }
                if (test.percentOccluded > maximumAcceptedOcclusion) {
                    maximumAcceptedOcclusion = test.percentOccluded;
                }
            }

            if (!allQueriesReady) {
                return;
            }

            auto storedOcclusion = 0.0f;
            auto representativePixelCount =
                (std::numeric_limits<std::uint32_t>::max)();
            if (anyAcceptedQuery) {
                storedOcclusion = maximumAcceptedOcclusion;
                representativePixelCount = smallestAcceptedPixelCount;
            } else if (anyObservedQuery) {
                storedOcclusion = 1.0f;
                representativePixelCount = smallestObservedPixelCount;
            }

            field<float>(accumulator, kStoredSunOcclusionOffset) =
                storedOcclusion;
            field<bool>(accumulator, kWaitingForSunQueryOffset) = false;
            field<std::uint32_t>(accumulator, kSunPixelCountOffset) =
                representativePixelCount;

            for (std::size_t index = 0; index < kSunTestCount; ++index) {
                tests[index].percentOccluded = 0.0f;
                tests[index].pixelCount = 0;
            }

            if (anyAcceptedQuery && rejectedLowCoverageQuery) {
                correctedAggregates.fetch_add(1, std::memory_order_relaxed);
                if (!firstCorrectionLogged.exchange(
                        true,
                        std::memory_order_acq_rel)) {
                    logging::info(
                        "Stereo Sun occlusion ignored a clipped query below {} pixels because another completed query retained valid coverage; native glare smoothing remains active.",
                        kMinimumAcceptedCoveragePixels);
                }
            }
        }

        void __fastcall hookAggregateSunOcclusion(
            const std::uintptr_t accumulator) noexcept
        {
            if (!accumulator) {
                return;
            }
            if (!fixEnabled.load(std::memory_order_acquire)) {
                if (originalAggregate) {
                    originalAggregate(accumulator);
                }
                return;
            }
            aggregateStereoSunOcclusion(accumulator);
        }
    }

    bool installSunOcclusionNativeHook() noexcept
    {
        if (hookInstalled.load(std::memory_order_acquire)) {
            return true;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            logging::error(
                "Stereo Sun occlusion requires Fallout4VR 1.2.72; native Sun queries remain unchanged.");
            return false;
        }

        const auto module = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (!module) {
            return false;
        }
        auto* target = reinterpret_cast<void*>(
            module + kAggregateSunOcclusionRva);
        if (!exactEntry(target, kAggregateEntry)) {
            logging::critical(
                "Stereo Sun occlusion rejected the FO4VR aggregation contract; native Sun queries remain unchanged.");
            return false;
        }

        void* trampoline{};
        const auto createResult = MH_CreateHook(
            target,
            reinterpret_cast<void*>(&hookAggregateSunOcclusion),
            &trampoline);
        if (createResult != MH_OK || !executableRange(trampoline, 1)) {
            if (createResult == MH_OK) {
                (void)MH_RemoveHook(target);
            }
            logging::critical(
                "Stereo Sun occlusion could not create its verified native hook (MinHook={}).",
                static_cast<int>(createResult));
            return false;
        }
        originalAggregate = reinterpret_cast<AggregateFunction>(trampoline);
        const auto enableResult = MH_EnableHook(target);
        if (enableResult != MH_OK ||
            std::memcmp(
                target,
                kAggregateEntry.data(),
                kAggregateEntry.size()) == 0) {
            (void)MH_DisableHook(target);
            (void)MH_RemoveHook(target);
            originalAggregate = nullptr;
            logging::critical(
                "Stereo Sun occlusion could not prove ownership of its native hook (MinHook={}).",
                static_cast<int>(enableResult));
            return false;
        }

        hookInstalled.store(true, std::memory_order_release);
        logging::info(
            "Stereo Sun occlusion owns the verified FO4VR three-query aggregation boundary; low-coverage peripheral queries can no longer invalidate a simultaneously visible Sun query.");
        return true;
    }

    void setSunOcclusionFixEnabled(const bool enabled) noexcept
    {
        const auto active = enabled &&
            hookInstalled.load(std::memory_order_acquire);
        const auto previous = fixEnabled.exchange(
            active,
            std::memory_order_acq_rel);
        if (previous != active) {
            logging::info(
                "Stereo Sun occlusion correction {}.",
                active ? "enabled" : "disabled");
        }
    }
}
