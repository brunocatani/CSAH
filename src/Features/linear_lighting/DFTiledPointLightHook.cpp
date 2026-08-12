#include "Features/linear_lighting/DFTiledPointLightHook.h"

#include "Features/linear_lighting/DFTiledPointLightModel.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

namespace community_shaders::linear_lighting
{
    namespace
    {
        // Independently derived from raw Fallout4VR.exe 1.2.72 disassembly.
        // 0x1427EE6C0 converts source light RGB with three powf calls and then
        // calls the sole 64-byte DFTiled record constructor at 0x142889940.
        constexpr std::uintptr_t kPointLightProducerCallsiteRva = 0x027EE9AB;
        constexpr std::uintptr_t kPointLightRecordConstructorRva = 0x02889940;
        constexpr std::uintptr_t kVanillaGammaRva = 0x02C96CE4;
        constexpr std::array<std::uintptr_t, 3> kPointLightGammaLoadRvas{
            0x027EE8B6,
            0x027EE8ED,
            0x027EE907,
        };
        constexpr std::size_t kGammaLoadInstructionBytes = 8;
        constexpr std::size_t kGammaLoadDisplacementOffset = 4;
        constexpr std::array<std::byte, 4> kGammaLoadOpcode{
            std::byte{ 0xF3 }, std::byte{ 0x0F },
            std::byte{ 0x10 }, std::byte{ 0x0D },
        };
        constexpr std::array<std::byte, 5> kProducerCallsiteSignature{
            std::byte{ 0xE8 }, std::byte{ 0x90 }, std::byte{ 0xAF },
            std::byte{ 0x09 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 32> kPointLightRecordSignature{
            std::byte{ 0x44 }, std::byte{ 0x8B }, std::byte{ 0x1D },
            std::byte{ 0x19 }, std::byte{ 0xD6 }, std::byte{ 0xFF },
            std::byte{ 0x03 }, std::byte{ 0x48 }, std::byte{ 0x8D },
            std::byte{ 0x05 }, std::byte{ 0x02 }, std::byte{ 0xD6 },
            std::byte{ 0xFF }, std::byte{ 0x03 }, std::byte{ 0x0F },
            std::byte{ 0x28 }, std::byte{ 0xE3 }, std::byte{ 0x46 },
            std::byte{ 0x8B }, std::byte{ 0x0C }, std::byte{ 0x98 },
            std::byte{ 0x4E }, std::byte{ 0x8D }, std::byte{ 0x14 },
            std::byte{ 0x98 }, std::byte{ 0x48 }, std::byte{ 0x8D },
            std::byte{ 0x05 }, std::byte{ 0x10 }, std::byte{ 0xD6 },
            std::byte{ 0xFF }, std::byte{ 0x03 },
        };

        struct Float3
        {
            float x{};
            float y{};
            float z{};
        };

        struct Float4
        {
            float x{};
            float y{};
            float z{};
            float w{};
        };

        using PointLightRecordFunction = void(__fastcall*)(
            std::int32_t recordKind,
            const Float4* positionAndRadius,
            const Float4* directionAndShape,
            float range,
            const Float3* color,
            const Float3* secondary,
            std::uint8_t flag1,
            std::uint8_t flag2,
            std::uint8_t flag3,
            std::uint8_t flag4);

        struct DetourPatchIdentity
        {
            const std::byte* patchAddress{};
            const void* destination{};
        };

        PointLightRecordFunction originalPointLightRecord{};
        std::byte* pointLightRecordTarget{};
        std::atomic<std::uint32_t*> exponentStorage{};
        std::array<std::int32_t, kPointLightGammaLoadRvas.size()>
            originalGammaDisplacements{};
        DetourPatchIdentity pointLightRecordPatch{};
        std::atomic_bool installed{};
        std::atomic_bool hookOwnershipReady{};
        std::atomic_bool desiredEnabled{};
        std::atomic_uint32_t desiredGammaBits{
            std::bit_cast<std::uint32_t>(kVanillaPointLightGamma) };
        std::atomic_uint32_t desiredColorMultiplierBits{
            std::bit_cast<std::uint32_t>(1.0f) };
        std::atomic_uint64_t completedCalls{};
        std::atomic_uint64_t modifiedCalls{};
        std::atomic_uint64_t passThroughCalls{};
        std::atomic_uint64_t invalidColorSources{};
        std::atomic_uint64_t validationFailures{};
        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
                return false;
            }
            const auto finish = start + size;
            auto cursor = start;
            while (cursor < finish) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto regionStart =
                    reinterpret_cast<std::uintptr_t>(information.BaseAddress);
                if (regionStart >
                    std::numeric_limits<std::uintptr_t>::max() -
                        information.RegionSize) {
                    return false;
                }
                const auto regionEnd = regionStart + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(finish, regionEnd);
            }
            return true;
        }

        [[nodiscard]] bool isExecutableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!isReadableRange(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            constexpr DWORD executableProtection =
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & executableProtection) != 0;
        }

        [[nodiscard]] bool addRelativeDisplacement(
            std::uintptr_t nextInstruction,
            std::int32_t displacement,
            std::uintptr_t& destination) noexcept
        {
            if (displacement >= 0) {
                const auto positive = static_cast<std::uintptr_t>(displacement);
                if (positive >
                    std::numeric_limits<std::uintptr_t>::max() -
                        nextInstruction) {
                    return false;
                }
                destination = nextInstruction + positive;
                return true;
            }
            const auto magnitude = static_cast<std::uintptr_t>(
                -static_cast<std::int64_t>(displacement));
            if (magnitude > nextInstruction) {
                return false;
            }
            destination = nextInstruction - magnitude;
            return true;
        }

        [[nodiscard]] const std::byte* resolveRelativeTarget(
            const std::byte* instruction,
            std::size_t displacementOffset,
            std::size_t instructionBytes) noexcept
        {
            if (!instruction ||
                !isReadableRange(instruction, instructionBytes)) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(
                &displacement,
                instruction + displacementOffset,
                sizeof(displacement));
            const auto address = reinterpret_cast<std::uintptr_t>(instruction);
            if (address >
                std::numeric_limits<std::uintptr_t>::max() -
                    instructionBytes) {
                return nullptr;
            }
            std::uintptr_t destination{};
            if (!addRelativeDisplacement(
                    address + instructionBytes,
                    displacement,
                    destination)) {
                return nullptr;
            }
            return reinterpret_cast<const std::byte*>(destination);
        }

        [[nodiscard]] bool captureMinHookPatchIdentity(
            const void* target,
            DetourPatchIdentity& identity) noexcept
        {
            identity = {};
            if (!isReadableRange(target, 5)) {
                return false;
            }
            auto* entry = static_cast<const std::byte*>(target);
            const std::byte* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t shortDisplacement{};
                std::memcpy(
                    &shortDisplacement,
                    entry + 1,
                    sizeof(shortDisplacement));
                if (shortDisplacement != -7) {
                    return false;
                }
                const auto entryAddress =
                    reinterpret_cast<std::uintptr_t>(entry);
                if (entryAddress < 5) {
                    return false;
                }
                patch = reinterpret_cast<const std::byte*>(entryAddress - 5);
                if (!isReadableRange(patch, 5)) {
                    return false;
                }
            }
            if (patch[0] != std::byte{ 0xE9 }) {
                return false;
            }
            const auto* destination = resolveRelativeTarget(patch, 1, 5);
            if (!destination || !isExecutableRange(destination, 1)) {
                return false;
            }
            identity = {
                .patchAddress = patch,
                .destination = destination,
            };
            return true;
        }

        [[nodiscard]] bool detourPatchOwned() noexcept
        {
            DetourPatchIdentity current{};
            return pointLightRecordPatch.patchAddress &&
                pointLightRecordPatch.destination &&
                captureMinHookPatchIdentity(pointLightRecordTarget, current) &&
                current.patchAddress == pointLightRecordPatch.patchAddress &&
                current.destination == pointLightRecordPatch.destination;
        }

        [[nodiscard]] std::uint32_t* allocateExponentStorage(
            const std::byte* image,
            std::size_t imageSize) noexcept
        {
            if (!image || imageSize == 0) {
                return nullptr;
            }
            const auto imageAddress = reinterpret_cast<std::uintptr_t>(image);
            if (imageAddress >
                std::numeric_limits<std::uintptr_t>::max() - imageSize) {
                return nullptr;
            }
            std::array<std::uintptr_t, kPointLightGammaLoadRvas.size()>
                nextInstructions{};
            for (std::size_t index = 0;
                 index < kPointLightGammaLoadRvas.size();
                 ++index) {
                nextInstructions[index] = imageAddress +
                    kPointLightGammaLoadRvas[index] +
                    kGammaLoadInstructionBytes;
            }
            auto* result = support::near_allocation::allocateReachablePage(
                nextInstructions,
                imageAddress + imageSize,
                sizeof(std::uint32_t));
            return result ?
                std::construct_at(
                    static_cast<std::uint32_t*>(result),
                    std::bit_cast<std::uint32_t>(
                        kVanillaPointLightGamma)) :
                nullptr;
        }

        [[nodiscard]] bool writeDisplacement(
            std::byte* instruction,
            std::int32_t displacement) noexcept
        {
            auto* target = instruction + kGammaLoadDisplacementOffset;
            DWORD oldProtection{};
            if (!VirtualProtect(
                    target,
                    sizeof(displacement),
                    PAGE_EXECUTE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            std::memcpy(target, &displacement, sizeof(displacement));
            DWORD discardedProtection{};
            const auto restored = VirtualProtect(
                target,
                sizeof(displacement),
                oldProtection,
                &discardedProtection);
            FlushInstructionCache(
                GetCurrentProcess(),
                instruction,
                kGammaLoadInstructionBytes);
            return restored != FALSE;
        }

        [[nodiscard]] bool displacementForTarget(
            const std::byte* instruction,
            const void* target,
            std::int32_t& displacement) noexcept
        {
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) +
                kGammaLoadInstructionBytes;
            const auto destination = reinterpret_cast<std::uintptr_t>(target);
            const auto delta = static_cast<std::int64_t>(destination) -
                static_cast<std::int64_t>(next);
            if (delta < INT32_MIN || delta > INT32_MAX) {
                return false;
            }
            displacement = static_cast<std::int32_t>(delta);
            return true;
        }

        [[nodiscard]] bool gammaLoadsOwned() noexcept
        {
            auto* storage = exponentStorage.load(std::memory_order_acquire);
            if (!storage) {
                return false;
            }
            for (const auto rva : kPointLightGammaLoadRvas) {
                auto* instruction = reinterpret_cast<const std::byte*>(
                    GetModuleHandleW(nullptr)) + rva;
                if (!isExecutableRange(
                        instruction,
                        kGammaLoadInstructionBytes) ||
                    std::memcmp(
                        instruction,
                        kGammaLoadOpcode.data(),
                        kGammaLoadOpcode.size()) != 0 ||
                    resolveRelativeTarget(
                        instruction,
                        kGammaLoadDisplacementOffset,
                        kGammaLoadInstructionBytes) !=
                        reinterpret_cast<const std::byte*>(storage)) {
                    return false;
                }
            }
            return true;
        }

        void synchronizeExponentStorage() noexcept
        {
            auto* storage = exponentStorage.load(std::memory_order_acquire);
            if (!storage) {
                return;
            }
            const auto active = hookOwnershipReady.load(
                                    std::memory_order_acquire) &&
                desiredEnabled.load(std::memory_order_acquire);
            const auto bits = active ?
                desiredGammaBits.load(std::memory_order_relaxed) :
                std::bit_cast<std::uint32_t>(kVanillaPointLightGamma);
            std::atomic_ref<std::uint32_t>(*storage).store(
                bits,
                std::memory_order_release);
        }

        void restoreGammaLoads(std::size_t count) noexcept
        {
            auto* image = reinterpret_cast<std::byte*>(
                GetModuleHandleW(nullptr));
            for (std::size_t index = 0;
                 image && index < count &&
                     index < kPointLightGammaLoadRvas.size();
                 ++index) {
                (void)writeDisplacement(
                    image + kPointLightGammaLoadRvas[index],
                    originalGammaDisplacements[index]);
            }
        }

        [[nodiscard]] bool finiteColor(const Float3& color) noexcept
        {
            return std::isfinite(color.x) && std::isfinite(color.y) &&
                std::isfinite(color.z);
        }

        void __fastcall hookPointLightRecord(
            std::int32_t recordKind,
            const Float4* positionAndRadius,
            const Float4* directionAndShape,
            float range,
            const Float3* color,
            const Float3* secondary,
            std::uint8_t flag1,
            std::uint8_t flag2,
            std::uint8_t flag3,
            std::uint8_t flag4) noexcept
        {
            const auto active = hookOwnershipReady.load(
                                    std::memory_order_acquire) &&
                desiredEnabled.load(std::memory_order_relaxed);
            if (active && color) {
                const auto multiplier = std::bit_cast<float>(
                    desiredColorMultiplierBits.load(
                        std::memory_order_relaxed));
                Float3 scaledColor{
                    color->x * multiplier,
                    color->y * multiplier,
                    color->z * multiplier,
                };
                if (std::isfinite(multiplier) && finiteColor(*color) &&
                    finiteColor(scaledColor)) {
                    originalPointLightRecord(
                        recordKind,
                        positionAndRadius,
                        directionAndShape,
                        range,
                        &scaledColor,
                        secondary,
                        flag1,
                        flag2,
                        flag3,
                        flag4);
                    modifiedCalls.fetch_add(1, std::memory_order_relaxed);
                    completedCalls.fetch_add(1, std::memory_order_release);
                    return;
                }
                invalidColorSources.fetch_add(1, std::memory_order_relaxed);
            }

            originalPointLightRecord(
                recordKind,
                positionAndRadius,
                directionAndShape,
                range,
                color,
                secondary,
                flag1,
                flag2,
                flag3,
                flag4);
            passThroughCalls.fetch_add(1, std::memory_order_relaxed);
            completedCalls.fetch_add(1, std::memory_order_release);
        }
    }

    bool installDFTiledPointLightHook() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }

        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image) {
            logging::error(
                "DFTiled point-light hook rejected a missing executable image.");
            return false;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (!isReadableRange(dos, sizeof(*dos)) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error(
                "DFTiled point-light hook rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            kPointLightRecordConstructorRva +
                    kPointLightRecordSignature.size() >
                nt->OptionalHeader.SizeOfImage ||
            kPointLightProducerCallsiteRva +
                    kProducerCallsiteSignature.size() >
                nt->OptionalHeader.SizeOfImage ||
            kVanillaGammaRva + sizeof(float) >
                nt->OptionalHeader.SizeOfImage) {
            logging::error(
                "DFTiled point-light hook rejected invalid PE image bounds.");
            return false;
        }
        for (const auto rva : kPointLightGammaLoadRvas) {
            if (rva + kGammaLoadInstructionBytes >
                nt->OptionalHeader.SizeOfImage) {
                logging::error(
                    "DFTiled point-light hook rejected a gamma-load image bound.");
                return false;
            }
        }

        auto* recordTarget = image + kPointLightRecordConstructorRva;
        const auto* callsite = image + kPointLightProducerCallsiteRva;
        const auto* vanillaGamma = image + kVanillaGammaRva;
        if (!isExecutableRange(
                recordTarget,
                kPointLightRecordSignature.size()) ||
            std::memcmp(
                recordTarget,
                kPointLightRecordSignature.data(),
                kPointLightRecordSignature.size()) != 0 ||
            !isExecutableRange(
                callsite,
                kProducerCallsiteSignature.size()) ||
            std::memcmp(
                callsite,
                kProducerCallsiteSignature.data(),
                kProducerCallsiteSignature.size()) != 0 ||
            resolveRelativeTarget(callsite, 1, 5) != recordTarget ||
            !isReadableRange(vanillaGamma, sizeof(float))) {
            logging::error(
                "DFTiled point-light constructor/caller identity gate failed; point lighting remains vanilla.");
            return false;
        }
        std::uint32_t vanillaGammaBits{};
        std::memcpy(
            &vanillaGammaBits,
            vanillaGamma,
            sizeof(vanillaGammaBits));
        if (vanillaGammaBits !=
            std::bit_cast<std::uint32_t>(kVanillaPointLightGamma)) {
            logging::error(
                "DFTiled point-light source gamma gate failed; point lighting remains vanilla.");
            return false;
        }
        for (std::size_t index = 0;
             index < kPointLightGammaLoadRvas.size();
             ++index) {
            auto* instruction = image + kPointLightGammaLoadRvas[index];
            if (!isExecutableRange(
                    instruction,
                    kGammaLoadInstructionBytes) ||
                std::memcmp(
                    instruction,
                    kGammaLoadOpcode.data(),
                    kGammaLoadOpcode.size()) != 0 ||
                resolveRelativeTarget(
                    instruction,
                    kGammaLoadDisplacementOffset,
                    kGammaLoadInstructionBytes) != vanillaGamma) {
                logging::error(
                    "DFTiled point-light gamma-load identity gate failed at index {}; point lighting remains vanilla.",
                    index);
                return false;
            }
            std::memcpy(
                &originalGammaDisplacements[index],
                instruction + kGammaLoadDisplacementOffset,
                sizeof(originalGammaDisplacements[index]));
        }

        auto* storage = allocateExponentStorage(
            image,
            nt->OptionalHeader.SizeOfImage);
        if (!storage) {
            logging::error(
                "DFTiled point-light hook could not allocate reachable exponent storage; point lighting remains vanilla.");
            return false;
        }

        std::size_t patchedLoads{};
        for (std::size_t index = 0;
             index < kPointLightGammaLoadRvas.size();
             ++index) {
            auto* instruction = image + kPointLightGammaLoadRvas[index];
            std::int32_t displacement{};
            if (!displacementForTarget(
                    instruction,
                    storage,
                    displacement)) {
                restoreGammaLoads(patchedLoads);
                (void)VirtualFree(storage, 0, MEM_RELEASE);
                logging::error(
                    "DFTiled point-light exponent storage was out of RIP-relative range; point lighting remains vanilla.");
                return false;
            }
            ++patchedLoads;
            if (!writeDisplacement(instruction, displacement)) {
                std::atomic_ref<std::uint32_t>(*storage).store(
                    std::bit_cast<std::uint32_t>(kVanillaPointLightGamma),
                    std::memory_order_release);
                restoreGammaLoads(patchedLoads);
                // Keep the page resident if protection restoration failed;
                // an incompletely restored instruction must never dangle.
                logging::error(
                    "DFTiled point-light gamma-load patch failed at index {}; point lighting remains vanilla.",
                    index);
                return false;
            }
        }

        void* trampoline{};
        auto status = MH_CreateHook(
            recordTarget,
            reinterpret_cast<void*>(&hookPointLightRecord),
            &trampoline);
        if (status != MH_OK || !isExecutableRange(trampoline, 1)) {
            std::atomic_ref<std::uint32_t>(*storage).store(
                std::bit_cast<std::uint32_t>(kVanillaPointLightGamma),
                std::memory_order_release);
            restoreGammaLoads(patchedLoads);
            if (status == MH_OK) {
                (void)MH_RemoveHook(recordTarget);
            }
            logging::error(
                "DFTiled point-light detour creation failed: {} ({}); point lighting remains vanilla.",
                MH_StatusToString(status),
                static_cast<int>(status));
            return false;
        }
        originalPointLightRecord =
            reinterpret_cast<PointLightRecordFunction>(trampoline);
        status = MH_EnableHook(recordTarget);
        DetourPatchIdentity patch{};
        if (status != MH_OK ||
            !captureMinHookPatchIdentity(recordTarget, patch)) {
            (void)MH_DisableHook(recordTarget);
            (void)MH_RemoveHook(recordTarget);
            originalPointLightRecord = nullptr;
            std::atomic_ref<std::uint32_t>(*storage).store(
                std::bit_cast<std::uint32_t>(kVanillaPointLightGamma),
                std::memory_order_release);
            restoreGammaLoads(patchedLoads);
            logging::error(
                "DFTiled point-light detour activation/ownership validation failed: {} ({}); point lighting remains vanilla.",
                MH_StatusToString(status),
                static_cast<int>(status));
            return false;
        }

        pointLightRecordTarget = recordTarget;
        exponentStorage.store(storage, std::memory_order_release);
        pointLightRecordPatch = patch;
        hookOwnershipReady.store(true, std::memory_order_release);
        synchronizeExponentStorage();
        installed.store(true, std::memory_order_release);
        logging::info(
            "Installed verified FO4VR DFTiled point-light producer hook (record RVA 0x02889940, gamma loads 3, vanilla exponent 2.2).");
        return true;
    }

    bool validateDFTiledPointLightHook(const char* trigger) noexcept
    {
        const auto hookInstalled = installed.load(std::memory_order_acquire);
        const auto detourOwned = hookInstalled && detourPatchOwned();
        const auto loadsOwned = hookInstalled && gammaLoadsOwned();
        const auto owned = detourOwned && loadsOwned;
        hookOwnershipReady.store(owned, std::memory_order_release);
        synchronizeExponentStorage();
        if (!owned && hookInstalled) {
            validationFailures.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "DFTiled point-light hook ownership validation failed at '{}' (detourOwned={}, gammaLoadsOwned={}); point-light producer is fail-closed to vanilla gamma.",
                trigger ? trigger : "unknown",
                detourOwned,
                loadsOwned);
        }
        return owned;
    }

    void publishDFTiledPointLightSettings(const Settings& settings) noexcept
    {
        const auto state = makeDFTiledPointLightProducerState(settings);
        desiredGammaBits.store(
            std::bit_cast<std::uint32_t>(state.gamma),
            std::memory_order_relaxed);
        desiredColorMultiplierBits.store(
            std::bit_cast<std::uint32_t>(state.colorMultiplier),
            std::memory_order_relaxed);
        desiredEnabled.store(state.enabled, std::memory_order_release);
        synchronizeExponentStorage();
    }

    DFTiledPointLightHookSnapshot dFTiledPointLightHookSnapshot() noexcept
    {
        const auto hookInstalled = installed.load(std::memory_order_acquire);
        const auto owned = hookOwnershipReady.load(std::memory_order_acquire);
        const auto active = owned &&
            desiredEnabled.load(std::memory_order_acquire);
        return {
            .installed = hookInstalled,
            .detourOwned = hookInstalled && detourPatchOwned(),
            .gammaLoadsOwned = hookInstalled && gammaLoadsOwned(),
            .enabled = active,
            .completedCalls = completedCalls.load(std::memory_order_acquire),
            .modifiedCalls = modifiedCalls.load(std::memory_order_relaxed),
            .passThroughCalls =
                passThroughCalls.load(std::memory_order_relaxed),
            .invalidColorSources =
                invalidColorSources.load(std::memory_order_relaxed),
            .validationFailures =
                validationFailures.load(std::memory_order_relaxed),
            .activeGamma = active ?
                std::bit_cast<float>(
                    desiredGammaBits.load(std::memory_order_relaxed)) :
                kVanillaPointLightGamma,
            .activeColorMultiplier = active ?
                std::bit_cast<float>(desiredColorMultiplierBits.load(
                    std::memory_order_relaxed)) :
                1.0f,
        };
    }
}
