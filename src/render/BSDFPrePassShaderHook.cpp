#include "render/BSDFPrePassShaderHook.h"

#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace community_shaders::render
{
    namespace
    {
        // Independently verified in Fallout4VR.exe 1.2.72. The constructor at
        // 0x142878930 publishes vtable 0x1430B8C68. Slot 3 is
        // 0x142878F80 and reads the uint32 descriptor at pass + 0x48 before
        // selecting the DFPrepass program and binding its pixel shader.
        constexpr std::uintptr_t kVtableRva = 0x030B8C68;
        constexpr std::size_t kSetupSlot = 3;
        constexpr std::uintptr_t kSetupFunctionRva = 0x02878F80;
        constexpr std::size_t kDescriptorOffset = 0x48;
        constexpr std::array<std::byte, 49> kSetupSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x58 },
            std::byte{ 0x08 }, std::byte{ 0x4C }, std::byte{ 0x89 },
            std::byte{ 0x40 }, std::byte{ 0x18 }, std::byte{ 0x55 },
            std::byte{ 0x56 }, std::byte{ 0x57 }, std::byte{ 0x41 },
            std::byte{ 0x54 }, std::byte{ 0x41 }, std::byte{ 0x55 },
            std::byte{ 0x41 }, std::byte{ 0x56 }, std::byte{ 0x41 },
            std::byte{ 0x57 }, std::byte{ 0x48 }, std::byte{ 0x8D },
            std::byte{ 0xA8 }, std::byte{ 0x38 }, std::byte{ 0xFE },
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x48 },
            std::byte{ 0x81 }, std::byte{ 0xEC }, std::byte{ 0x90 },
            std::byte{ 0x02 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x0F }, std::byte{ 0x29 }, std::byte{ 0x78 },
            std::byte{ 0xA8 }, std::byte{ 0x4D }, std::byte{ 0x8B },
            std::byte{ 0xF1 }, std::byte{ 0x49 }, std::byte{ 0x8B },
            std::byte{ 0xD8 }, std::byte{ 0x4C }, std::byte{ 0x8B },
            std::byte{ 0xE2 },
        };

        using SetupFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* compiledProgram,
            void* techniqueState);

        SetupFunction originalSetup{};
        void** setupCell{};
        std::atomic_bool installed{};
        std::atomic_uint64_t setupCalls{};
        std::atomic_uint64_t validationFailures{};
        std::atomic_uint32_t lastDescriptor{};

        constexpr std::uint32_t kLinearLightingConsumer = 1u << 0;
        constexpr std::uint32_t kComplexEnvironmentConsumer = 1u << 1;
        constexpr std::uint32_t kIblConsumer = 1u << 2;
        constexpr std::uint32_t kAllDescriptorConsumers =
            kLinearLightingConsumer | kComplexEnvironmentConsumer |
            kIblConsumer;
        std::atomic_uint32_t descriptorConsumerMask{};

        thread_local DFPrePassDescriptorScope activeScope{};

        class DescriptorScope final
        {
        public:
            explicit DescriptorScope(std::uint32_t descriptor) noexcept :
                previous_(activeScope)
            {
                activeScope = { descriptor, true };
            }

            ~DescriptorScope() noexcept
            {
                activeScope = previous_;
            }

            DescriptorScope(const DescriptorScope&) = delete;
            DescriptorScope& operator=(const DescriptorScope&) = delete;

        private:
            DFPrePassDescriptorScope previous_{};
        };

        [[nodiscard]] bool readable(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            if (start > (std::numeric_limits<std::uintptr_t>::max)() - size) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                    sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto regionStart =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            return start >= regionStart &&
                start + size <= regionStart + information.RegionSize;
        }

        [[nodiscard]] bool executable(
            const void* address,
            std::size_t size) noexcept
        {
            if (!readable(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            constexpr DWORD mask = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & mask) != 0;
        }

        [[nodiscard]] bool patchPointer(
            void** cell,
            void* expected,
            void* replacement) noexcept
        {
            if (!readable(cell, sizeof(*cell)) || *cell != expected) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    cell,
                    sizeof(*cell),
                    PAGE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            *cell = replacement;
            DWORD discarded{};
            if (!VirtualProtect(
                cell,
                sizeof(*cell),
                oldProtection,
                &discarded)) {
                // The page is still writable, so restore the engine target
                // before reporting failure. Never leave hookSetup installed
                // unless originalSetup remains live and ownership is published.
                if (*cell == replacement) {
                    *cell = expected;
                }
                DWORD rollbackProtection{};
                (void)VirtualProtect(
                    cell,
                    sizeof(*cell),
                    oldProtection,
                    &rollbackProtection);
                FlushInstructionCache(
                    GetCurrentProcess(), cell, sizeof(*cell));
                return false;
            }
            FlushInstructionCache(GetCurrentProcess(), cell, sizeof(*cell));
            return *cell == replacement;
        }

        void setDescriptorConsumer(
            std::uint32_t consumer,
            bool enabled) noexcept
        {
            if (enabled) {
                descriptorConsumerMask.fetch_or(
                    consumer,
                    std::memory_order_release);
            } else {
                descriptorConsumerMask.fetch_and(
                    ~consumer,
                    std::memory_order_release);
            }
        }

        void __fastcall hookSetup(
            void* receiver,
            void* pass,
            void* compiledProgram,
            void* techniqueState) noexcept
        {
            const auto original = originalSetup;
            if (!original) {
                return;
            }

            if ((descriptorConsumerMask.load(std::memory_order_acquire) &
                    kAllDescriptorConsumers) != kAllDescriptorConsumers ||
                !pass) {
                original(
                    receiver,
                    pass,
                    compiledProgram,
                    techniqueState);
                return;
            }

            // Ghidra verification of Fallout4VR.exe 1.2.72 at 0x142878F80
            // confirms that the original immediately performs the same
            // unchecked 32-bit read from its mandatory second argument.
            // A per-draw VirtualQuery adds no safety and is prohibitively
            // expensive in this renderer hot path.
            std::uint32_t descriptor{};
            std::memcpy(
                &descriptor,
                static_cast<const std::byte*>(pass) + kDescriptorOffset,
                sizeof(descriptor));
            setupCalls.fetch_add(1, std::memory_order_relaxed);
            lastDescriptor.store(descriptor, std::memory_order_relaxed);
            const DescriptorScope scope(descriptor);
            original(
                receiver,
                pass,
                compiledProgram,
                techniqueState);
        }
    }

    bool installBSDFPrePassShaderHook() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        auto* image = reinterpret_cast<std::byte*>(
            GetModuleHandleW(nullptr));
        if (!image) {
            logging::error(
                "BSDFPrePass descriptor hook could not resolve Fallout4VR.exe.");
            return false;
        }
        auto** cell = reinterpret_cast<void**>(
            image + kVtableRva + kSetupSlot * sizeof(void*));
        auto* expected = image + kSetupFunctionRva;
        if (!readable(cell, sizeof(*cell)) || *cell != expected ||
            !executable(expected, kSetupSignature.size()) ||
            std::memcmp(
                expected,
                kSetupSignature.data(),
                kSetupSignature.size()) != 0) {
            logging::error(
                "BSDFPrePass descriptor hook live identity gate failed; complex environment materials remain fail-closed.");
            return false;
        }
        originalSetup = reinterpret_cast<SetupFunction>(expected);
        if (!patchPointer(
                cell,
                expected,
                reinterpret_cast<void*>(&hookSetup))) {
            originalSetup = nullptr;
            logging::error(
                "BSDFPrePass descriptor vtable patch failed; complex environment materials remain fail-closed.");
            return false;
        }
        setupCell = cell;
        installed.store(true, std::memory_order_release);
        logging::info(
            "Installed verified Fallout4VR BSDFPrePass descriptor hook (vtable 0x030B8C68 slot 3, descriptor +0x48).");
        return true;
    }

    bool validateBSDFPrePassShaderHook(const char* trigger) noexcept
    {
        const auto hookInstalled =
            installed.load(std::memory_order_acquire);
        const auto owned = hookInstalled && readable(
            setupCell, sizeof(*setupCell)) &&
            *setupCell == reinterpret_cast<void*>(&hookSetup);
        if (!owned && hookInstalled) {
            const auto failures = validationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if ((failures & (failures - 1)) == 0) {
                logging::error(
                    "BSDFPrePass descriptor hook ownership failed at '{}' (failures={}); complex environment materials remain fail-closed.",
                    trigger ? trigger : "unknown",
                    failures);
            }
        }
        return owned;
    }

    void setDFPrePassLinearLightingEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kLinearLightingConsumer, enabled);
    }

    void setDFPrePassComplexEnvironmentEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kComplexEnvironmentConsumer, enabled);
    }

    void setDFPrePassIblEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kIblConsumer, enabled);
    }

    DFPrePassDescriptorScope activeDFPrePassDescriptorScope() noexcept
    {
        return activeScope;
    }

    DFPrePassHookSnapshot dFPrePassHookSnapshot() noexcept
    {
        const auto hookInstalled =
            installed.load(std::memory_order_acquire);
        return {
            .installed = hookInstalled,
            .vtableCellOwned = hookInstalled && readable(
                setupCell, sizeof(*setupCell)) &&
                *setupCell == reinterpret_cast<void*>(&hookSetup),
            .setupCalls = setupCalls.load(std::memory_order_relaxed),
            .validationFailures =
                validationFailures.load(std::memory_order_relaxed),
            .lastDescriptor =
                lastDescriptor.load(std::memory_order_relaxed),
        };
    }
}
