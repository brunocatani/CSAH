#include "diagnostics/HdrOutputProbe.h"

#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace community_shaders::diagnostics::hdr_output_probe
{
    namespace
    {
        // Independently verified in Fallout4VR.exe 1.2.72. The
        // ImageSpaceEffectHDR constructor at RVA 0x02882C30 publishes vtable
        // RVA 0x030B8EA8. Slot 1 is the complete HDR render transaction at
        // RVA 0x02883360. It owns all downsample/adaptation work and the final
        // HDR-tonemap image-space draw. This one-shot probe observes that
        // transaction without changing any engine or D3D11 render state.
        constexpr std::uintptr_t kVtableRva = 0x030B8EA8;
        constexpr std::size_t kRenderSlot = 1;
        constexpr std::uintptr_t kRenderFunctionRva = 0x02883360;
        constexpr std::array<std::byte, 31> kRenderSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x58 },
            std::byte{ 0x18 }, std::byte{ 0x4C }, std::byte{ 0x89 },
            std::byte{ 0x48 }, std::byte{ 0x20 }, std::byte{ 0x48 },
            std::byte{ 0x89 }, std::byte{ 0x50 }, std::byte{ 0x10 },
            std::byte{ 0x55 }, std::byte{ 0x56 }, std::byte{ 0x57 },
            std::byte{ 0x41 }, std::byte{ 0x54 }, std::byte{ 0x41 },
            std::byte{ 0x55 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x41 }, std::byte{ 0x57 }, std::byte{ 0x48 },
            std::byte{ 0x8D }, std::byte{ 0x68 }, std::byte{ 0xA0 },
            std::byte{ 0x48 },
        };

        constexpr std::size_t kMaximumTrackedShaders = 4096;
        constexpr std::size_t kMaximumCapturedBinds = 64;

        struct ShaderIdentity final
        {
            ID3D11PixelShader* shader{};
            std::size_t bytecodeLength{};
            std::array<std::uint8_t, 16> checksum{};
            bool dxbc{};
        };

        struct CapturedBind final
        {
            ID3D11PixelShader* shader{};
            std::size_t bytecodeLength{};
            std::array<std::uint8_t, 16> checksum{};
            bool dxbc{};
            bool immediateContext{};
        };

        using RenderFunction = void(__fastcall*)(
            void* receiver,
            void* input,
            void* output,
            void* parameters);

        std::array<ShaderIdentity, kMaximumTrackedShaders> shaderIdentities{};
        std::size_t shaderIdentityCount{};
        std::mutex shaderIdentityMutex{};
        RenderFunction originalRender{};
        std::atomic_bool installed{};
        std::atomic_bool captureComplete{};
        std::atomic_uint64_t renderCalls{};
        std::atomic_uint64_t validationFailures{};
        thread_local bool captureActive{};
        thread_local std::array<CapturedBind, kMaximumCapturedBinds>
            capturedBinds{};
        thread_local std::size_t capturedBindCount{};

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

        void formatChecksum(
            const std::array<std::uint8_t, 16>& checksum,
            char (&text)[33]) noexcept
        {
            constexpr char digits[] = "0123456789abcdef";
            for (std::size_t index = 0; index < checksum.size(); ++index) {
                text[index * 2] = digits[checksum[index] >> 4];
                text[index * 2 + 1] = digits[checksum[index] & 0x0F];
            }
            text[32] = '\0';
        }

        void publishCapture() noexcept
        {
            logging::info(
                "FO4VR HDR output ownership probe captured {} pixel-shader binds inside ImageSpaceEffectHDR::Render.",
                capturedBindCount);
            for (std::size_t index = 0; index < capturedBindCount; ++index) {
                const auto& binding = capturedBinds[index];
                char checksum[33]{};
                formatChecksum(binding.checksum, checksum);
                logging::info(
                    "FO4VR HDR output bind #{}: shader={}, bytecodeBytes={}, dxbc={}, checksum={}, immediateContext={}.",
                    index + 1,
                    static_cast<const void*>(binding.shader),
                    binding.bytecodeLength,
                    binding.dxbc,
                    binding.dxbc ? checksum : "unknown",
                    binding.immediateContext);
            }
        }

        void __fastcall hookRender(
            void* receiver,
            void* input,
            void* output,
            void* parameters) noexcept
        {
            const auto original = originalRender;
            if (!original) {
                return;
            }
            renderCalls.fetch_add(1, std::memory_order_relaxed);
            const auto capture =
                !captureComplete.load(std::memory_order_acquire) &&
                !captureActive;
            if (capture) {
                capturedBindCount = 0;
                captureActive = true;
            }
            original(receiver, input, output, parameters);
            if (!capture) {
                return;
            }
            captureActive = false;
            if (capturedBindCount == 0 ||
                captureComplete.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            publishCapture();
        }
    }

    bool install() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image) {
            logging::error(
                "HDR output ownership probe could not resolve Fallout4VR.exe.");
            return false;
        }
        auto** renderCell = reinterpret_cast<void**>(
            image + kVtableRva + kRenderSlot * sizeof(void*));
        auto* expectedRender = image + kRenderFunctionRva;
        const auto cellMatches = readable(renderCell, sizeof(*renderCell)) &&
            *renderCell == expectedRender;
        const auto codeExecutable =
            executable(expectedRender, kRenderSignature.size());
        const auto codeMatches = codeExecutable &&
            std::memcmp(
                expectedRender,
                kRenderSignature.data(),
                kRenderSignature.size()) == 0;
        if (!cellMatches || !codeExecutable) {
            validationFailures.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "HDR output ownership probe identity gate failed (vtableCell={}, executableTarget={}); no engine state was modified.",
                cellMatches,
                codeExecutable);
            return false;
        }
        if (!codeMatches) {
            logging::warn(
                "HDR output ownership probe found the exact FO4VR vtable cell and executable render target, but its verified native prologue is already detoured. The probe will own only the vtable cell and chain through the existing render target.");
        }
        originalRender = reinterpret_cast<RenderFunction>(expectedRender);
        if (!patchPointer(
                renderCell,
                expectedRender,
                reinterpret_cast<void*>(&hookRender))) {
            originalRender = nullptr;
            validationFailures.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "HDR output ownership probe could not own the verified vtable cell; no engine state was modified.");
            return false;
        }
        installed.store(true, std::memory_order_release);
        logging::info(
            "Installed one-shot FO4VR HDR output ownership probe (ImageSpaceEffectHDR vtable RVA 0x030B8EA8 slot 1, render RVA 0x02883360). The probe records identity only and does not change the image.");
        return true;
    }

    void onPixelShaderCreated(
        const void* bytecode,
        std::size_t bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!bytecode || !shader) {
            return;
        }
        ShaderIdentity identity{
            .shader = shader,
            .bytecodeLength = bytecodeLength,
        };
        if (bytecodeLength >= 20 &&
            std::memcmp(bytecode, "DXBC", 4) == 0) {
            identity.dxbc = true;
            std::memcpy(
                identity.checksum.data(),
                static_cast<const std::byte*>(bytecode) + 4,
                identity.checksum.size());
        }
        std::scoped_lock lock(shaderIdentityMutex);
        if (shaderIdentityCount < shaderIdentities.size()) {
            shaderIdentities[shaderIdentityCount++] = identity;
        }
    }

    void onPixelShaderBound(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* shader) noexcept
    {
        if (!captureActive || !shader ||
            capturedBindCount >= capturedBinds.size()) {
            return;
        }
        CapturedBind binding{ .shader = shader };
        {
            std::scoped_lock lock(shaderIdentityMutex);
            for (std::size_t index = 0; index < shaderIdentityCount; ++index) {
                const auto& identity = shaderIdentities[index];
                if (identity.shader == shader) {
                    binding.bytecodeLength = identity.bytecodeLength;
                    binding.checksum = identity.checksum;
                    binding.dxbc = identity.dxbc;
                    break;
                }
            }
        }
        ID3D11Device* device{};
        if (context) {
            context->GetDevice(&device);
        }
        ID3D11DeviceContext* immediate{};
        if (device) {
            device->GetImmediateContext(&immediate);
        }
        binding.immediateContext = context && immediate == context;
        if (immediate) {
            immediate->Release();
        }
        if (device) {
            device->Release();
        }
        capturedBinds[capturedBindCount++] = binding;
    }
}
