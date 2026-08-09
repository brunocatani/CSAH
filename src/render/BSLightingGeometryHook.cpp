#include "render/BSLightingGeometryHook.h"

#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "support/Logger.h"

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

namespace community_shaders::render
{
    namespace
    {
        // Independently derived from raw Fallout4VR.exe 1.2.72 disassembly.
        // The active VR renderer constructs the BSDF lighting object at
        // 0x14291D6B0 and publishes this vtable. The legacy BSLightingShader
        // vtable at RVA 0x030BBDB8 is constructed too, but its slot 9 receives
        // no world-rendering calls in the VR-extended pipeline.
        constexpr std::uintptr_t kBSDFLightShaderVtableRva = 0x030BF3C8;
        constexpr std::size_t kGeometrySetupSlot = 9;
        constexpr std::uintptr_t kGeometrySetupFunctionRva = 0x0291DCA0;
        constexpr std::uintptr_t kLightingStateAccessorRva = 0x027AEEB0;
        constexpr std::uintptr_t kLightingStateRva = 0x068787F0;
        constexpr std::size_t kLightingStateEmissiveMultiplierOffset = 0x1BC;
        constexpr std::size_t kLightingStateLeaInstructionBytes = 7;
        constexpr float kMaximumPlausibleEmissiveMultiplier = 1.0e6f;

        constexpr std::array<std::byte, 41> kGeometrySetupSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x4C }, std::byte{ 0x89 }, std::byte{ 0x40 }, std::byte{ 0x18 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x50 }, std::byte{ 0x10 },
            std::byte{ 0x55 }, std::byte{ 0x53 },
            std::byte{ 0x41 }, std::byte{ 0x54 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0xA8 },
            std::byte{ 0x98 }, std::byte{ 0xFD }, std::byte{ 0xFF }, std::byte{ 0xFF },
            std::byte{ 0x48 }, std::byte{ 0x81 }, std::byte{ 0xEC },
            std::byte{ 0x50 }, std::byte{ 0x03 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x49 }, std::byte{ 0x8B }, std::byte{ 0x58 }, std::byte{ 0x08 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x70 }, std::byte{ 0x08 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x78 }, std::byte{ 0xE0 },
        };
        constexpr std::array<std::byte, 8> kLightingStateAccessorSignature{
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x05 },
            std::byte{ 0x39 }, std::byte{ 0x99 }, std::byte{ 0x0C }, std::byte{ 0x04 },
            std::byte{ 0xC3 },
        };

        using GeometrySetupFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* compiledProgram);

        GeometrySetupFunction originalGeometrySetup{};
        void** geometrySetupCell{};
        const std::byte* lightingState{};
        std::atomic_bool installed{};
        std::atomic_uint64_t calls{};
        std::atomic_uint64_t acceptedUpdates{};
        std::atomic_uint64_t rejectedSources{};
        std::atomic_uint32_t deepestStage{};
        std::atomic_uint32_t lastSourceEmissiveMultiplierBits{};

        void recordStage(GeometrySourceStage stage) noexcept
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

        [[nodiscard]] void* readPointerCell(void** cell) noexcept
        {
            return cell ? ReadPointerAcquire(
                              reinterpret_cast<void* const volatile*>(cell)) :
                          nullptr;
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

        [[nodiscard]] bool readEmissiveMultiplier(float& value) noexcept
        {
            // Fallout4VR's active geometry routine obtains this exact
            // process-lifetime renderer state through the signature-gated
            // accessor at RVA 0x027AEEB0, then uploads state +0x1BC. The range
            // is validated once before the hook is installed; the render hot
            // path performs only the scalar copy and plausibility check.
            if (!lightingState) {
                return false;
            }
            recordStage(GeometrySourceStage::lightingState);

            std::memcpy(
                &value,
                lightingState + kLightingStateEmissiveMultiplierOffset,
                sizeof(value));
            if (!std::isfinite(value) || value < 0.0f ||
                value > kMaximumPlausibleEmissiveMultiplier) {
                return false;
            }
            recordStage(GeometrySourceStage::emissiveMultiplier);
            return true;
        }

        void __fastcall hookGeometrySetup(
            void* receiver,
            void* pass,
            void* compiledProgram) noexcept
        {
            // Preserve the engine's complete selector-2 map/write/unmap/bind
            // transaction exactly once. Our independent b8 is published after
            // it returns so the engine cannot overwrite that slot afterward.
            originalGeometrySetup(receiver, pass, compiledProgram);

            float emissiveMultiplier{};
            const auto validSource = readEmissiveMultiplier(emissiveMultiplier);
            if (!validSource) {
                rejectedSources.fetch_add(1, std::memory_order_relaxed);
                calls.fetch_add(1, std::memory_order_release);
                return;
            }
            lastSourceEmissiveMultiplierBits.store(
                std::bit_cast<std::uint32_t>(emissiveMultiplier),
                std::memory_order_relaxed);
            if (linear_lighting::Runtime::get().updateGeometryEmissive(
                    emissiveMultiplier)) {
                acceptedUpdates.fetch_add(1, std::memory_order_relaxed);
            }
            // Publish a completed call only after every classification counter
            // and sample is final, allowing the telemetry acquire to report a
            // coherent first-call outcome from another thread.
            calls.fetch_add(1, std::memory_order_release);
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
                "BSDF lighting geometry hook rejected a missing executable image.");
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (!isReadableRange(dos, sizeof(*dos)) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error(
                "BSDF lighting geometry hook rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            kBSDFLightShaderVtableRva +
                    (kGeometrySetupSlot + 1) * sizeof(void*) >
                nt->OptionalHeader.SizeOfImage ||
            kGeometrySetupFunctionRva + kGeometrySetupSignature.size() >
                nt->OptionalHeader.SizeOfImage ||
            kLightingStateAccessorRva +
                    kLightingStateAccessorSignature.size() >
                nt->OptionalHeader.SizeOfImage ||
            kLightingStateRva + kLightingStateEmissiveMultiplierOffset +
                    sizeof(float) >
                nt->OptionalHeader.SizeOfImage) {
            logging::error(
                "BSDF lighting geometry hook rejected invalid PE image bounds.");
            return false;
        }

        auto** cell = reinterpret_cast<void**>(
            image + kBSDFLightShaderVtableRva +
            kGeometrySetupSlot * sizeof(void*));
        auto* expected = image + kGeometrySetupFunctionRva;
        const auto* accessor = image + kLightingStateAccessorRva;
        const auto* state = image + kLightingStateRva;
        if (!isReadableRange(cell, sizeof(*cell)) || *cell != expected ||
            !isExecutableRange(expected, kGeometrySetupSignature.size()) ||
            std::memcmp(
                expected,
                kGeometrySetupSignature.data(),
                kGeometrySetupSignature.size()) != 0 ||
            !isExecutableRange(
                accessor,
                kLightingStateAccessorSignature.size()) ||
            std::memcmp(
                accessor,
                kLightingStateAccessorSignature.data(),
                kLightingStateAccessorSignature.size()) != 0 ||
            !isReadableRange(
                state + kLightingStateEmissiveMultiplierOffset,
                sizeof(float))) {
            logging::error(
                "BSDF lighting geometry hook live identity/signature gate failed; Linear Lighting remains vanilla.");
            return false;
        }
        std::int32_t stateDisplacement{};
        std::memcpy(
            &stateDisplacement,
            accessor + 3,
            sizeof(stateDisplacement));
        const auto* resolvedLightingState =
            accessor + kLightingStateLeaInstructionBytes + stateDisplacement;
        if (resolvedLightingState != state) {
            logging::error(
                "BSDF lighting geometry hook accessor target gate failed; Linear Lighting remains vanilla.");
            return false;
        }

        originalGeometrySetup =
            reinterpret_cast<GeometrySetupFunction>(expected);
        lightingState = state;
        if (!patchPointer(
                cell,
                expected,
                reinterpret_cast<void*>(&hookGeometrySetup))) {
            originalGeometrySetup = nullptr;
            lightingState = nullptr;
            logging::error(
                "BSDF lighting geometry vtable patch failed; Linear Lighting remains vanilla.");
            return false;
        }

        geometrySetupCell = cell;
        installed.store(true, std::memory_order_release);
        linear_lighting::Runtime::get().setGeometryProviderReady(true);
        logging::info(
            "Installed verified Fallout4VR BSDF lighting geometry hook (vtable slot 9, renderer state RVA 0x068787F0, emissive multiplier +0x1BC).");
        return true;
    }

    GeometryHookSnapshot geometryHookSnapshot() noexcept
    {
        const auto hookInstalled = installed.load(std::memory_order_acquire);
        return {
            .installed = hookInstalled,
            .vtableCellOwned = hookInstalled &&
                readPointerCell(geometrySetupCell) ==
                reinterpret_cast<void*>(&hookGeometrySetup),
            .calls = calls.load(std::memory_order_acquire),
            .acceptedUpdates = acceptedUpdates.load(std::memory_order_relaxed),
            .rejectedSources = rejectedSources.load(std::memory_order_relaxed),
            .deepestStage = static_cast<GeometrySourceStage>(
                deepestStage.load(std::memory_order_relaxed)),
            .lastSourceEmissiveMultiplier = std::bit_cast<float>(
                lastSourceEmissiveMultiplierBits.load(
                    std::memory_order_relaxed)),
        };
    }
}
