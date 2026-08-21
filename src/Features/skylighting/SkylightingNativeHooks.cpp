#include "Features/skylighting/SkylightingNativeHooks.h"

#include "Features/skylighting/SkylightingRuntime.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace community_shaders::skylighting
{
    namespace
    {
        constexpr std::uintptr_t kWrapperRva = 0x00634300;
        constexpr std::uintptr_t kRenderRva = 0x006350C0;
        constexpr std::uintptr_t kProjectionRva = 0x00635530;
        constexpr std::uintptr_t kWrapperFirstCallTargetRva = 0x0012FB50;
        constexpr std::uintptr_t kCubeSizeRva = 0x05A3CFA4;
        constexpr std::uintptr_t kDirectionRva = 0x05A3CFC8;
        constexpr std::ptrdiff_t kPrecipitationManagerOffset = 0xA0;
        constexpr std::size_t kPrecipitationManagerReadableSize = 0x98;
        constexpr std::array<std::byte, 6> kWrapperSignature{
            std::byte{ 0x40 }, std::byte{ 0x53 }, std::byte{ 0x48 },
            std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x30 },
        };
        constexpr std::array<std::byte, 20> kRenderSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x55 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0xA8 },
            std::byte{ 0x68 }, std::byte{ 0xFB }, std::byte{ 0xFF },
            std::byte{ 0xFF }, std::byte{ 0x48 }, std::byte{ 0x81 },
            std::byte{ 0xEC }, std::byte{ 0x88 }, std::byte{ 0x05 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 30> kProjectionSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x58 },
            std::byte{ 0x08 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x70 }, std::byte{ 0x18 }, std::byte{ 0x48 },
            std::byte{ 0x89 }, std::byte{ 0x78 }, std::byte{ 0x20 },
            std::byte{ 0x55 }, std::byte{ 0x48 }, std::byte{ 0x8D },
            std::byte{ 0xA8 }, std::byte{ 0x18 }, std::byte{ 0xFF },
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x48 },
            std::byte{ 0x81 }, std::byte{ 0xEC }, std::byte{ 0xE0 },
            std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 },
        };

        using WrapperFunction = void(__fastcall*)();
        using NativeRendererSingleton = void*(__fastcall*)();

        struct DetourIdentity
        {
            const std::byte* patch{};
            const void* destination{};
        };

        WrapperFunction originalWrapper{};
        NativeRendererSingleton nativeRendererSingleton{};
        NativePrecipitationRender nativeRender{};
        NativeProjectionSetup nativeProjection{};
        std::byte* wrapperTarget{};
        DetourIdentity installedIdentity{};
        std::atomic_bool installed{};
        std::atomic_bool firstCallbackLogged{};
        std::atomic_bool missingManagerLogged{};

        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
                return false;
            }
            const auto end = begin + size;
            auto cursor = begin;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto region = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (information.RegionSize >
                    std::numeric_limits<std::uintptr_t>::max() - region) {
                    return false;
                }
                const auto regionEnd = region + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(end, regionEnd);
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
            constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & executable) != 0;
        }

        [[nodiscard]] const std::byte* relativeTarget(
            const std::byte* instruction) noexcept
        {
            if (!isReadableRange(instruction, 5) ||
                instruction[0] != std::byte{ 0xE8 }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, instruction + 1, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) + 5;
            const auto target = static_cast<std::int64_t>(next) +
                static_cast<std::int64_t>(displacement);
            if (target <= 0 ||
                static_cast<std::uint64_t>(target) >
                    std::numeric_limits<std::uintptr_t>::max()) {
                return nullptr;
            }
            return reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(target));
        }

        [[nodiscard]] void* resolveNativePrecipitationManager() noexcept
        {
            if (!nativeRendererSingleton) {
                return nullptr;
            }
            auto* rendererState = nativeRendererSingleton();
            if (!rendererState) {
                return nullptr;
            }
            auto* slot = static_cast<std::byte*>(rendererState) +
                kPrecipitationManagerOffset;
            if (!isReadableRange(slot, sizeof(void*))) {
                return nullptr;
            }
            void* precipitation{};
            std::memcpy(&precipitation, slot, sizeof(precipitation));
            if (!isReadableRange(
                    precipitation,
                    kPrecipitationManagerReadableSize)) {
                return nullptr;
            }
            return precipitation;
        }

        [[nodiscard]] bool captureDetourIdentity(
            const void* target,
            DetourIdentity& identity) noexcept
        {
            identity = {};
            if (!isReadableRange(target, 5)) {
                return false;
            }
            auto* entry = static_cast<const std::byte*>(target);
            const auto* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t displacement{};
                std::memcpy(&displacement, entry + 1, sizeof(displacement));
                if (displacement != -7 ||
                    reinterpret_cast<std::uintptr_t>(entry) < 5) {
                    return false;
                }
                patch = entry - 5;
            }
            if (!isReadableRange(patch, 5) ||
                patch[0] != std::byte{ 0xE9 }) {
                return false;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, patch + 1, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(patch) + 5;
            const auto destinationAddress = static_cast<std::int64_t>(next) +
                static_cast<std::int64_t>(displacement);
            if (destinationAddress <= 0) {
                return false;
            }
            const auto* destination = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(destinationAddress));
            if (!isExecutableRange(destination, 1)) {
                return false;
            }
            identity = { patch, destination };
            return true;
        }

        void __fastcall hookWrapper() noexcept
        {
            if (originalWrapper) {
                originalWrapper();
            }
            if (!firstCallbackLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Skylighting observed its first native exterior-render callback.");
            }
            auto* precipitation = resolveNativePrecipitationManager();
            if (!precipitation && !missingManagerLogged.exchange(
                                      true,
                                      std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting could not resolve the persistent FO4VR precipitation manager at renderer offset +0xA0; captures remain fail-closed.");
            }
            Runtime::get().onNativePrecipitationFrame(
                precipitation,
                nativeRender,
                nativeProjection);
        }
    }

    bool installNativeHooks() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image || !isReadableRange(image, sizeof(IMAGE_DOS_HEADER))) {
            logging::error(
                "Skylighting native hook rejected a missing FO4VR image.");
            return false;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error(
                "Skylighting native hook rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            logging::error(
                "Skylighting native hook rejected invalid PE metadata.");
            return false;
        }
        const auto imageSize =
            static_cast<std::uintptr_t>(nt->OptionalHeader.SizeOfImage);
        const auto inImage = [imageSize](
                                 std::uintptr_t rva,
                                 std::size_t size) noexcept {
            return rva <= imageSize && size <= imageSize - rva;
        };
        if (!inImage(kWrapperRva, kWrapperSignature.size() + 5)) {
            logging::error(
                "Skylighting native wrapper contract at RVA 0x00634300 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kRenderRva, kRenderSignature.size())) {
            logging::error(
                "Skylighting native render contract at RVA 0x006350C0 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kProjectionRva, kProjectionSignature.size())) {
            logging::error(
                "Skylighting native projection contract at RVA 0x00635530 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kCubeSizeRva, sizeof(float))) {
            logging::error(
                "Skylighting native cube-size contract at RVA 0x05A3CFA4 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kDirectionRva, sizeof(float) * 3)) {
            logging::error(
                "Skylighting native direction contract at RVA 0x05A3CFC8 is outside the FO4VR image.");
            return false;
        }

        auto* wrapper = image + kWrapperRva;
        auto* render = image + kRenderRva;
        auto* projection = image + kProjectionRva;
        if (!isExecutableRange(wrapper, kWrapperSignature.size() + 5)) {
            logging::error(
                "Skylighting native wrapper contract at RVA 0x00634300 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                wrapper,
                kWrapperSignature.data(),
                kWrapperSignature.size()) != 0) {
            logging::error(
                "Skylighting native wrapper signature mismatch at RVA 0x00634300.");
            return false;
        }
        auto* wrapperFirstCall = wrapper + kWrapperSignature.size();
        if (wrapperFirstCall[0] != std::byte{ 0xE8 }) {
            logging::error(
                "Skylighting native wrapper first-call opcode mismatch at RVA 0x00634306.");
            return false;
        }
        if (relativeTarget(wrapperFirstCall) !=
            image + kWrapperFirstCallTargetRva) {
            logging::error(
                "Skylighting native wrapper first-call target mismatch; expected RVA 0x0012FB50.");
            return false;
        }
        if (!isExecutableRange(render, kRenderSignature.size())) {
            logging::error(
                "Skylighting native render contract at RVA 0x006350C0 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                render,
                kRenderSignature.data(),
                kRenderSignature.size()) != 0) {
            logging::error(
                "Skylighting native render signature mismatch at RVA 0x006350C0.");
            return false;
        }
        if (!isExecutableRange(projection, kProjectionSignature.size())) {
            logging::error(
                "Skylighting native projection contract at RVA 0x00635530 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                projection,
                kProjectionSignature.data(),
                kProjectionSignature.size()) != 0) {
            logging::error(
                "Skylighting native projection signature mismatch at RVA 0x00635530.");
            return false;
        }
        if (!isReadableRange(image + kCubeSizeRva, sizeof(float))) {
            logging::error(
                "Skylighting native cube-size global at RVA 0x05A3CFA4 is not readable.");
            return false;
        }
        if (!isReadableRange(image + kDirectionRva, sizeof(float) * 3)) {
            logging::error(
                "Skylighting native direction global at RVA 0x05A3CFC8 is not readable.");
            return false;
        }

        void* trampoline{};
        auto status = MH_CreateHook(
            wrapper,
            reinterpret_cast<void*>(&hookWrapper),
            &trampoline);
        if (status != MH_OK || !isExecutableRange(trampoline, 1)) {
            if (status == MH_OK) {
                (void)MH_RemoveHook(wrapper);
            }
            logging::error(
                "Skylighting native wrapper detour creation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            return false;
        }
        originalWrapper = reinterpret_cast<WrapperFunction>(trampoline);
        nativeRendererSingleton =
            reinterpret_cast<NativeRendererSingleton>(
                image + kWrapperFirstCallTargetRva);
        nativeRender = reinterpret_cast<NativePrecipitationRender>(render);
        nativeProjection = reinterpret_cast<NativeProjectionSetup>(projection);
        status = MH_EnableHook(wrapper);
        DetourIdentity identity{};
        if (status != MH_OK || !captureDetourIdentity(wrapper, identity)) {
            (void)MH_DisableHook(wrapper);
            (void)MH_RemoveHook(wrapper);
            originalWrapper = nullptr;
            nativeRendererSingleton = nullptr;
            nativeRender = nullptr;
            nativeProjection = nullptr;
            logging::error(
                "Skylighting native wrapper detour activation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            return false;
        }

        wrapperTarget = wrapper;
        installedIdentity = identity;
        installed.store(true, std::memory_order_release);
        Runtime::get().setNativeHookOwned(true);
        logging::info(
            "Installed verified FO4VR Skylighting precipitation capture hook (wrapper RVA 0x00634300, render RVA 0x006350C0, projection RVA 0x00635530)." );
        return true;
    }

    bool validateNativeHooks(const char* trigger) noexcept
    {
        DetourIdentity current{};
        const auto owned = installed.load(std::memory_order_acquire) &&
            wrapperTarget && installedIdentity.patch &&
            installedIdentity.destination &&
            captureDetourIdentity(wrapperTarget, current) &&
            current.patch == installedIdentity.patch &&
            current.destination == installedIdentity.destination;
        Runtime::get().setNativeHookOwned(owned);
        if (!owned && installed.load(std::memory_order_acquire)) {
            logging::error(
                "Skylighting native hook ownership validation failed at '{}'; ambient consumption and private capture are disabled.",
                trigger ? trigger : "unknown");
        }
        return owned;
    }
}
