#include "render/BSLightingGeometryHook.h"

#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "support/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace community_shaders::render
{
    namespace
    {
        // Independently derived from raw Fallout4VR.exe 1.2.72 disassembly.
        // The flat executable uses a different virtual slot for this phase.
        constexpr std::uintptr_t kBSLightingShaderVtableRva = 0x030BBDB8;
        constexpr std::size_t kGeometrySetupSlot = 9;
        constexpr std::uintptr_t kGeometrySetupFunctionRva = 0x028B6B70;
        constexpr std::size_t kPassGeometryOffset = 0x18;
        constexpr std::size_t kGeometryPropertyOffset = 0x178;
        constexpr std::size_t kPropertyEmissiveMultiplierOffset = 0x1B0;
        constexpr float kMaximumPlausibleEmissiveMultiplier = 1.0e6f;

        constexpr std::array<std::byte, 39> kGeometrySetupSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x58 }, std::byte{ 0x08 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x68 }, std::byte{ 0x18 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x70 }, std::byte{ 0x20 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x50 }, std::byte{ 0x10 },
            std::byte{ 0x57 },
            std::byte{ 0x41 }, std::byte{ 0x54 },
            std::byte{ 0x41 }, std::byte{ 0x55 },
            std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x41 }, std::byte{ 0x57 },
            std::byte{ 0x48 }, std::byte{ 0x81 }, std::byte{ 0xEC },
            std::byte{ 0x30 }, std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x45 }, std::byte{ 0x8B }, std::byte{ 0x60 }, std::byte{ 0x40 },
        };

        using GeometrySetupFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* compiledProgram);

        GeometrySetupFunction originalGeometrySetup{};
        std::atomic_bool installed{};
        std::atomic_uint64_t calls{};
        std::atomic_uint64_t acceptedUpdates{};
        std::atomic_uint64_t rejectedWalks{};
        std::atomic_uint32_t deepestStage{};

        void recordStage(GeometryWalkStage stage) noexcept
        {
            auto observed = deepestStage.load(std::memory_order_relaxed);
            const auto value = static_cast<std::uint32_t>(stage);
            while (observed < value &&
                   !deepestStage.compare_exchange_weak(
                       observed,
                       value,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }
        }

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

        [[nodiscard]] bool patchPointer(
            void** target,
            void* expected,
            void* replacement) noexcept
        {
            if (!target || !expected || !replacement || *target != expected) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    target,
                    sizeof(*target),
                    PAGE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            auto* observed = InterlockedCompareExchangePointer(
                target,
                replacement,
                expected);
            DWORD discardedProtection{};
            const auto restored = VirtualProtect(
                target,
                sizeof(*target),
                oldProtection,
                &discardedProtection);
            FlushInstructionCache(GetCurrentProcess(), target, sizeof(*target));
            return observed == expected && restored != FALSE;
        }

        [[nodiscard]] bool readEmissiveMultiplier(
            const void* pass,
            float& value) noexcept
        {
            if (!isReadableRange(
                    pass,
                    kPassGeometryOffset + sizeof(void*))) {
                return false;
            }
            recordStage(GeometryWalkStage::pass);

            const auto* geometry = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(pass) + kPassGeometryOffset);
            if (!isReadableRange(
                    geometry,
                    kGeometryPropertyOffset + sizeof(void*))) {
                return false;
            }
            recordStage(GeometryWalkStage::geometry);

            const auto* property = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(geometry) +
                kGeometryPropertyOffset);
            if (!isReadableRange(
                    property,
                    kPropertyEmissiveMultiplierOffset + sizeof(float))) {
                return false;
            }
            recordStage(GeometryWalkStage::property);

            std::memcpy(
                &value,
                static_cast<const std::byte*>(property) +
                    kPropertyEmissiveMultiplierOffset,
                sizeof(value));
            if (!std::isfinite(value) || value < 0.0f ||
                value > kMaximumPlausibleEmissiveMultiplier) {
                return false;
            }
            recordStage(GeometryWalkStage::emissiveMultiplier);
            return true;
        }

        void __fastcall hookGeometrySetup(
            void* receiver,
            void* pass,
            void* compiledProgram) noexcept
        {
            calls.fetch_add(1, std::memory_order_relaxed);

            linear_lighting::Runtime::get()
                .applyQueuedSettingsForGeometryDraw();

            float emissiveMultiplier{};
            const auto validSource =
                readEmissiveMultiplier(pass, emissiveMultiplier);

            // Preserve the engine's complete selector-2 map/write/unmap/bind
            // transaction exactly once. Our independent b8 is published after
            // it returns so the engine cannot overwrite that slot afterward.
            originalGeometrySetup(receiver, pass, compiledProgram);

            if (!validSource) {
                rejectedWalks.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (linear_lighting::Runtime::get().updateGeometryEmissive(
                    emissiveMultiplier)) {
                acceptedUpdates.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    bool installBSLightingGeometryHook() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }

        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image) {
            logging::error(
                "BSLighting geometry hook rejected a missing executable image.");
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (!isReadableRange(dos, sizeof(*dos)) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error(
                "BSLighting geometry hook rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            kBSLightingShaderVtableRva +
                    (kGeometrySetupSlot + 1) * sizeof(void*) >
                nt->OptionalHeader.SizeOfImage ||
            kGeometrySetupFunctionRva + kGeometrySetupSignature.size() >
                nt->OptionalHeader.SizeOfImage) {
            logging::error(
                "BSLighting geometry hook rejected invalid PE image bounds.");
            return false;
        }

        auto** cell = reinterpret_cast<void**>(
            image + kBSLightingShaderVtableRva +
            kGeometrySetupSlot * sizeof(void*));
        auto* expected = image + kGeometrySetupFunctionRva;
        if (!isReadableRange(cell, sizeof(*cell)) || *cell != expected ||
            !isExecutableRange(expected, kGeometrySetupSignature.size()) ||
            std::memcmp(
                expected,
                kGeometrySetupSignature.data(),
                kGeometrySetupSignature.size()) != 0) {
            logging::error(
                "BSLighting geometry hook live identity/signature gate failed; Linear Lighting remains vanilla.");
            return false;
        }

        originalGeometrySetup =
            reinterpret_cast<GeometrySetupFunction>(expected);
        if (!patchPointer(
                cell,
                expected,
                reinterpret_cast<void*>(&hookGeometrySetup))) {
            originalGeometrySetup = nullptr;
            logging::error(
                "BSLighting geometry vtable patch failed; Linear Lighting remains vanilla.");
            return false;
        }

        installed.store(true, std::memory_order_release);
        linear_lighting::Runtime::get().setGeometryProviderReady(true);
        logging::info(
            "Installed verified Fallout4VR BSLighting geometry hook (vtable slot 9, emissive property +0x1B0).");
        return true;
    }

    GeometryHookSnapshot geometryHookSnapshot() noexcept
    {
        return {
            .installed = installed.load(std::memory_order_acquire),
            .calls = calls.load(std::memory_order_relaxed),
            .acceptedUpdates = acceptedUpdates.load(std::memory_order_relaxed),
            .rejectedWalks = rejectedWalks.load(std::memory_order_relaxed),
            .deepestStage = static_cast<GeometryWalkStage>(
                deepestStage.load(std::memory_order_relaxed)),
        };
    }
}
