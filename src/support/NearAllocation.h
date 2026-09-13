#pragma once

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace csah::support::near_allocation
{
    [[nodiscard]] inline std::uintptr_t alignUp(
        std::uintptr_t value,
        std::uintptr_t alignment) noexcept
    {
        if (alignment == 0) {
            return value;
        }
        const auto remainder = value % alignment;
        if (remainder == 0) {
            return value;
        }
        const auto increment = alignment - remainder;
        return value <=
                std::numeric_limits<std::uintptr_t>::max() - increment ?
            value + increment :
            0;
    }

    [[nodiscard]] inline void* allocateInRange(
        std::uintptr_t first,
        std::uintptr_t last,
        std::size_t bytes,
        std::uintptr_t allocationGranularity) noexcept
    {
        auto cursor = alignUp(first, allocationGranularity);
        while (cursor != 0 && cursor <= last) {
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &information,
                    sizeof(information)) != sizeof(information)) {
                return nullptr;
            }
            const auto regionStart =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            if (regionStart >
                std::numeric_limits<std::uintptr_t>::max() -
                    information.RegionSize) {
                return nullptr;
            }
            const auto regionEnd = regionStart + information.RegionSize;
            if (information.State == MEM_FREE) {
                const auto candidate = alignUp(
                    (std::max)(cursor, regionStart),
                    allocationGranularity);
                if (candidate != 0 && candidate <= last &&
                    candidate <= regionEnd && bytes <= regionEnd - candidate) {
                    auto* result = VirtualAlloc(
                        reinterpret_cast<void*>(candidate),
                        bytes,
                        MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE);
                    if (result == reinterpret_cast<void*>(candidate)) {
                        return result;
                    }
                    if (result) {
                        (void)VirtualFree(result, 0, MEM_RELEASE);
                    }
                }
            }
            const auto next = (std::max)(
                regionEnd,
                cursor <=
                        std::numeric_limits<std::uintptr_t>::max() -
                            allocationGranularity ?
                    cursor + allocationGranularity :
                    std::numeric_limits<std::uintptr_t>::max());
            cursor = alignUp(next, allocationGranularity);
        }
        return nullptr;
    }

    [[nodiscard]] inline void* allocateReachablePage(
        std::span<const std::uintptr_t> nextInstructions,
        std::uintptr_t preferredAddress,
        std::size_t minimumBytes) noexcept
    {
        SYSTEM_INFO systemInformation{};
        GetSystemInfo(&systemInformation);
        const auto granularity = static_cast<std::uintptr_t>(
            systemInformation.dwAllocationGranularity);
        const auto pageSize = static_cast<std::size_t>(
            systemInformation.dwPageSize);
        if (nextInstructions.empty() || granularity == 0 ||
            pageSize < minimumBytes) {
            return nullptr;
        }

        auto lower = reinterpret_cast<std::uintptr_t>(
            systemInformation.lpMinimumApplicationAddress);
        auto upper = reinterpret_cast<std::uintptr_t>(
            systemInformation.lpMaximumApplicationAddress);
        for (const auto next : nextInstructions) {
            const auto instructionLower = next >= 0x80000000ull ?
                next - 0x80000000ull :
                0;
            const auto instructionUpper = next <=
                    std::numeric_limits<std::uintptr_t>::max() -
                        static_cast<std::uintptr_t>(
                            (std::numeric_limits<std::int32_t>::max)()) ?
                next + static_cast<std::uintptr_t>(
                           (std::numeric_limits<std::int32_t>::max)()) :
                std::numeric_limits<std::uintptr_t>::max();
            lower = (std::max)(lower, instructionLower);
            upper = (std::min)(upper, instructionUpper);
        }
        if (lower > upper) {
            return nullptr;
        }

        auto preferred = alignUp(preferredAddress, granularity);
        if (preferred == 0) {
            return nullptr;
        }
        preferred = std::clamp(preferred, lower, upper);
        if (auto* result = allocateInRange(
                preferred,
                upper,
                pageSize,
                granularity)) {
            return result;
        }
        if (preferred > lower) {
            const auto secondUpper = preferred >= granularity ?
                preferred - granularity :
                0;
            if (secondUpper >= lower) {
                return allocateInRange(
                    lower,
                    secondUpper,
                    pageSize,
                    granularity);
            }
        }
        return nullptr;
    }
}
