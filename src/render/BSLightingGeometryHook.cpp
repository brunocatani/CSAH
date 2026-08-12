#include "render/BSLightingGeometryHook.h"

#include "Features/linear_lighting/DFLightAmbientShaderPatch.h"
#include "Features/linear_lighting/DFLightProducerModel.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

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
#include <span>

namespace community_shaders::render
{
    namespace
    {
        // Independently derived from raw Fallout4VR.exe 1.2.72 disassembly.
        // The active VR renderer constructs the BSDF lighting object at
        // 0x14291D6B0 and publishes this vtable. Raw FO4VR disassembly and the
        // DFLight macro emitter independently establish that slot 3 owns the
        // active technique-light transaction while slot 9 owns the geometry
        // transaction. Both phases must share the same descriptor scope: the
        // technique phase owns ambient-transform selection, while both phases
        // contain the directional scalar-pow producer family.
        constexpr std::uintptr_t kBSDFLightShaderVtableRva = 0x030BF3C8;
        constexpr std::size_t kTechniqueSetupSlot = 3;
        constexpr std::uintptr_t kTechniqueSetupFunctionRva = 0x02922810;
        constexpr std::size_t kGeometrySetupSlot = 9;
        constexpr std::uintptr_t kGeometrySetupFunctionRva = 0x0291DCA0;
        constexpr std::size_t kDFLightDescriptorOffset = 0x48;
        constexpr std::uintptr_t kLightingStateAccessorRva = 0x027AEEB0;
        constexpr std::uintptr_t kLightingStateRva = 0x068787F0;
        constexpr std::size_t kLightingStateEmissiveMultiplierOffset = 0x1BC;
        constexpr std::size_t kLightingStateLeaInstructionBytes = 7;
        constexpr std::uintptr_t kNativeScalarPowThunkRva = 0x029917A8;
        constexpr std::uintptr_t kLocalAmbientTransformAccessorRva = 0x027ADD90;
        constexpr std::uintptr_t kFallbackAmbientTransformAccessorRva = 0x027AE6F0;
        constexpr std::size_t kRelativeCallBytes = 5;
        constexpr std::size_t kJumpStubBytes = 12;
        constexpr std::size_t kDirectionalJumpStubOffset = 0;
        constexpr std::size_t kFallbackAmbientJumpStubOffset = 16;
        constexpr std::size_t kLocalAmbientJumpStubOffset = 32;
        constexpr std::size_t kJumpIslandRequiredBytes = 48;
        constexpr float kMaximumPlausibleEmissiveMultiplier = 1.0e6f;

        struct PowCallsite
        {
            std::uintptr_t rva{};
            std::array<std::byte, kRelativeCallBytes> signature{};

            constexpr PowCallsite(
                std::uintptr_t address,
                std::uint32_t displacement) noexcept :
                rva(address),
                signature{
                    std::byte{ 0xE8 },
                    static_cast<std::byte>(displacement & 0xFFu),
                    static_cast<std::byte>((displacement >> 8) & 0xFFu),
                    static_cast<std::byte>((displacement >> 16) & 0xFFu),
                    static_cast<std::byte>((displacement >> 24) & 0xFFu),
                }
            {}
        };

        constexpr std::array<PowCallsite, 6> kDirectionalPowCallsites{
            PowCallsite{ 0x029232E9, 0x0006E4BA },
            PowCallsite{ 0x029232FD, 0x0006E4A6 },
            PowCallsite{ 0x02923311, 0x0006E492 },
            PowCallsite{ 0x0291E9E7, 0x00072DBC },
            PowCallsite{ 0x0291E9FB, 0x00072DA8 },
            PowCallsite{ 0x0291EA0F, 0x00072D94 },
        };
        constexpr std::array<PowCallsite, 2> kAmbientTransformCallsites{
            PowCallsite{ 0x02922AB5, 0xFFE8BC36 },
            PowCallsite{ 0x02922ABF, 0xFFE8B2CC },
        };

        constexpr std::array<std::byte, 45> kTechniqueSetupSignature{
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x5C },
            std::byte{ 0x24 }, std::byte{ 0x08 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x54 },
            std::byte{ 0x24 }, std::byte{ 0x10 },
            std::byte{ 0x55 }, std::byte{ 0x56 }, std::byte{ 0x57 },
            std::byte{ 0x41 }, std::byte{ 0x54 },
            std::byte{ 0x41 }, std::byte{ 0x55 },
            std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x41 }, std::byte{ 0x57 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0xAC },
            std::byte{ 0x24 }, std::byte{ 0xD0 }, std::byte{ 0xFD },
            std::byte{ 0xFF }, std::byte{ 0xFF },
            std::byte{ 0x48 }, std::byte{ 0x81 }, std::byte{ 0xEC },
            std::byte{ 0x30 }, std::byte{ 0x03 }, std::byte{ 0x00 },
            std::byte{ 0x00 },
            std::byte{ 0x44 }, std::byte{ 0x0F }, std::byte{ 0x29 },
            std::byte{ 0xAC }, std::byte{ 0x24 }, std::byte{ 0xB0 },
            std::byte{ 0x02 }, std::byte{ 0x00 }, std::byte{ 0x00 },
        };

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
        constexpr std::array<std::byte, 6> kNativeScalarPowThunkSignature{
            std::byte{ 0xFF }, std::byte{ 0x25 }, std::byte{ 0x3A },
            std::byte{ 0xB2 }, std::byte{ 0x2B }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 8> kLocalAmbientAccessorSignature{
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x81 },
            std::byte{ 0x00 }, std::byte{ 0x01 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0xC3 },
        };
        constexpr std::array<std::byte, 8> kFallbackAmbientAccessorSignature{
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x05 },
            std::byte{ 0xD9 }, std::byte{ 0x9F }, std::byte{ 0x0C },
            std::byte{ 0x04 }, std::byte{ 0xC3 },
        };

        using TechniqueSetupFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* compiledProgram,
            void* techniqueState);
        using GeometrySetupFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* compiledProgram);
        using NativeScalarPowFunction = float(__fastcall*)(
            float value,
            float exponent);
        using LocalAmbientTransformAccessorFunction = const float*(__fastcall*)(
            const void* state);
        using FallbackAmbientTransformAccessorFunction = const float*(__fastcall*)();

        TechniqueSetupFunction originalTechniqueSetup{};
        GeometrySetupFunction originalGeometrySetup{};
        NativeScalarPowFunction originalNativeScalarPow{};
        LocalAmbientTransformAccessorFunction originalLocalAmbientTransformAccessor{};
        FallbackAmbientTransformAccessorFunction
            originalFallbackAmbientTransformAccessor{};
        void** techniqueSetupCell{};
        void** geometrySetupCell{};
        const std::byte* lightingState{};
        std::byte* executableImage{};
        std::byte* powJumpIsland{};
        std::byte* directionalPowJumpStub{};
        std::byte* fallbackAmbientJumpStub{};
        std::byte* localAmbientJumpStub{};
        std::atomic_bool installed{};
        std::atomic_bool producerOwnershipReady{};
        std::atomic_bool desiredProducerEnabled{};
        std::atomic_uint32_t desiredDirectionalGammaBits{
            std::bit_cast<std::uint32_t>(
                linear_lighting::kVanillaDFLightGamma) };
        std::atomic_uint32_t desiredDirectionalMultiplierBits{
            std::bit_cast<std::uint32_t>(1.0f) };
        std::atomic_uint32_t desiredAmbientGammaBits{
            std::bit_cast<std::uint32_t>(
                linear_lighting::kVanillaDFLightGamma) };
        std::atomic_uint32_t desiredAmbientMultiplierBits{
            std::bit_cast<std::uint32_t>(1.0f) };
        std::atomic_uint32_t desiredAmbientInputScaleBits{
            std::bit_cast<std::uint32_t>(1.0f) };
        std::atomic_uint64_t techniqueCalls{};
        std::atomic_uint64_t calls{};
        std::atomic_uint64_t acceptedUpdates{};
        std::atomic_uint64_t rejectedSources{};
        std::atomic_uint64_t ambientDescriptors{};
        std::atomic_uint64_t directionalDescriptors{};
        std::atomic_uint64_t otherDescriptors{};
        std::atomic_uint64_t ambientTransformCalls{};
        std::atomic_uint64_t ambientTransformPrepared{};
        std::atomic_uint64_t ambientTransformPassThrough{};
        std::atomic_uint64_t directionalPowCalls{};
        std::atomic_uint64_t directionalPowModified{};
        std::atomic_uint64_t directionalPowPassThrough{};
        std::atomic_uint64_t invalidPowResults{};
        std::atomic_uint64_t validationFailures{};
        std::atomic_uint32_t deepestStage{};
        std::atomic_uint32_t lastDescriptor{};
        std::atomic_uint32_t lastSourceEmissiveMultiplierBits{};
        thread_local std::uint32_t activeDFLightDescriptor{};
        alignas(16) thread_local linear_lighting::DirectionalAmbientTransform
            scaledAmbientTransform{};

        class DFLightDescriptorScope
        {
        public:
            explicit DFLightDescriptorScope(std::uint32_t descriptor) noexcept :
                previous_(activeDFLightDescriptor)
            {
                activeDFLightDescriptor = descriptor;
            }

            ~DFLightDescriptorScope() noexcept
            {
                activeDFLightDescriptor = previous_;
            }

            DFLightDescriptorScope(const DFLightDescriptorScope&) = delete;
            DFLightDescriptorScope& operator=(const DFLightDescriptorScope&) =
                delete;

        private:
            std::uint32_t previous_{};
        };

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

        [[nodiscard]] const std::byte* resolveRelativeCallTarget(
            const std::byte* instruction) noexcept
        {
            if (!instruction ||
                !isReadableRange(instruction, kRelativeCallBytes) ||
                instruction[0] != std::byte{ 0xE8 }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, instruction + 1, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) +
                kRelativeCallBytes;
            std::uintptr_t destination{};
            return addRelativeDisplacement(next, displacement, destination) ?
                reinterpret_cast<const std::byte*>(destination) :
                nullptr;
        }

        [[nodiscard]] bool displacementForTarget(
            const std::byte* instruction,
            const void* target,
            std::int32_t& displacement) noexcept
        {
            if (!instruction || !target) {
                return false;
            }
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) +
                kRelativeCallBytes;
            const auto destination = reinterpret_cast<std::uintptr_t>(target);
            const auto delta = static_cast<std::int64_t>(destination) -
                static_cast<std::int64_t>(next);
            if (delta < (std::numeric_limits<std::int32_t>::min)() ||
                delta > (std::numeric_limits<std::int32_t>::max)()) {
                return false;
            }
            displacement = static_cast<std::int32_t>(delta);
            return true;
        }

        [[nodiscard]] bool writeExecutableBytes(
            std::byte* target,
            const std::byte* bytes,
            std::size_t size) noexcept
        {
            if (!target || !bytes || size == 0) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    target,
                    size,
                    PAGE_EXECUTE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            std::memcpy(target, bytes, size);
            DWORD discardedProtection{};
            const auto restored = VirtualProtect(
                target,
                size,
                oldProtection,
                &discardedProtection);
            FlushInstructionCache(GetCurrentProcess(), target, size);
            return restored != FALSE;
        }

        [[nodiscard]] bool writeRelativeCall(
            std::byte* instruction,
            const void* target) noexcept
        {
            std::int32_t displacement{};
            if (!displacementForTarget(instruction, target, displacement)) {
                return false;
            }
            std::array<std::byte, kRelativeCallBytes> replacement{};
            replacement[0] = std::byte{ 0xE8 };
            std::memcpy(
                replacement.data() + 1,
                &displacement,
                sizeof(displacement));
            return writeExecutableBytes(
                instruction,
                replacement.data(),
                replacement.size());
        }

        void buildAbsoluteJumpStub(
            std::byte* stub,
            const void* destination) noexcept
        {
            stub[0] = std::byte{ 0x48 };
            stub[1] = std::byte{ 0xB8 };
            const auto address = reinterpret_cast<std::uintptr_t>(destination);
            std::memcpy(stub + 2, &address, sizeof(address));
            stub[10] = std::byte{ 0xFF };
            stub[11] = std::byte{ 0xE0 };
        }

        [[nodiscard]] bool absoluteJumpStubOwned(
            const std::byte* stub,
            const void* destination) noexcept
        {
            if (!isExecutableRange(stub, kJumpStubBytes) ||
                stub[0] != std::byte{ 0x48 } ||
                stub[1] != std::byte{ 0xB8 } ||
                stub[10] != std::byte{ 0xFF } ||
                stub[11] != std::byte{ 0xE0 }) {
                return false;
            }
            std::uintptr_t encoded{};
            std::memcpy(&encoded, stub + 2, sizeof(encoded));
            return encoded == reinterpret_cast<std::uintptr_t>(destination);
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

        [[nodiscard]] bool callsiteFamilyOwned(
            const auto& callsites,
            const std::byte* expectedTarget) noexcept
        {
            if (!executableImage || !expectedTarget) {
                return false;
            }
            for (const auto& callsite : callsites) {
                const auto* instruction = executableImage + callsite.rva;
                if (resolveRelativeCallTarget(instruction) != expectedTarget) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool dFLightProducerCallsitesOwned() noexcept;

        [[nodiscard]] float runDFLightPowProducer(
            linear_lighting::DFLightProducerKind expectedKind,
            float value,
            float vanillaExponent,
            const std::atomic_uint32_t& desiredGamma,
            const std::atomic_uint32_t& desiredMultiplier,
            std::atomic_uint64_t& completed,
            std::atomic_uint64_t& modified,
            std::atomic_uint64_t& passThrough) noexcept
        {
            const auto active = originalNativeScalarPow &&
                producerOwnershipReady.load(std::memory_order_acquire) &&
                desiredProducerEnabled.load(std::memory_order_acquire) &&
                linear_lighting::classifyDFLightProducer(
                    activeDFLightDescriptor) == expectedKind;
            if (!active) {
                const auto result = originalNativeScalarPow ?
                    originalNativeScalarPow(value, vanillaExponent) :
                    value;
                passThrough.fetch_add(1, std::memory_order_relaxed);
                completed.fetch_add(1, std::memory_order_release);
                return result;
            }

            const auto gamma = std::bit_cast<float>(
                desiredGamma.load(std::memory_order_relaxed));
            const auto multiplier = std::bit_cast<float>(
                desiredMultiplier.load(std::memory_order_relaxed));
            if (!std::isfinite(gamma) || gamma <= 0.0f ||
                !std::isfinite(multiplier)) {
                invalidPowResults.fetch_add(1, std::memory_order_relaxed);
                const auto result =
                    originalNativeScalarPow(value, vanillaExponent);
                passThrough.fetch_add(1, std::memory_order_relaxed);
                completed.fetch_add(1, std::memory_order_release);
                return result;
            }

            const auto converted = originalNativeScalarPow(value, gamma);
            const auto result = converted * multiplier;
            if (!std::isfinite(result)) {
                invalidPowResults.fetch_add(1, std::memory_order_relaxed);
                const auto fallback =
                    originalNativeScalarPow(value, vanillaExponent);
                passThrough.fetch_add(1, std::memory_order_relaxed);
                completed.fetch_add(1, std::memory_order_release);
                return fallback;
            }
            modified.fetch_add(1, std::memory_order_relaxed);
            completed.fetch_add(1, std::memory_order_release);
            return result;
        }

        float __fastcall hookDirectionalScalarPow(
            float value,
            float exponent) noexcept
        {
            return runDFLightPowProducer(
                linear_lighting::DFLightProducerKind::directional,
                value,
                exponent,
                desiredDirectionalGammaBits,
                desiredDirectionalMultiplierBits,
                directionalPowCalls,
                directionalPowModified,
                directionalPowPassThrough);
        }

        [[nodiscard]] const float* prepareAmbientTransform(
            const float* source) noexcept
        {
            ambientTransformCalls.fetch_add(1, std::memory_order_relaxed);
            const auto active = source &&
                producerOwnershipReady.load(std::memory_order_acquire) &&
                desiredProducerEnabled.load(std::memory_order_acquire) &&
                linear_lighting::classifyDFLightProducer(
                    activeDFLightDescriptor) ==
                    linear_lighting::DFLightProducerKind::ambient &&
                linear_lighting::Runtime::get().dFLightAmbientDescriptorReady(
                    activeDFLightDescriptor);
            if (!active) {
                ambientTransformPassThrough.fetch_add(
                    1, std::memory_order_relaxed);
                return source;
            }

            const auto scale = std::bit_cast<float>(
                desiredAmbientInputScaleBits.load(std::memory_order_relaxed));
            if (!std::isfinite(scale) || scale < 0.0f) {
                invalidPowResults.fetch_add(1, std::memory_order_relaxed);
                ambientTransformPassThrough.fetch_add(
                    1, std::memory_order_relaxed);
                return source;
            }

            std::memcpy(
                scaledAmbientTransform.data(),
                source,
                sizeof(scaledAmbientTransform));
            linear_lighting::scaleDirectionalAmbientTransform(
                scaledAmbientTransform,
                scale);
            ambientTransformPrepared.fetch_add(1, std::memory_order_release);
            return scaledAmbientTransform.data();
        }

        const float* __fastcall hookFallbackAmbientTransform() noexcept
        {
            const auto* source = originalFallbackAmbientTransformAccessor ?
                originalFallbackAmbientTransformAccessor() :
                nullptr;
            return prepareAmbientTransform(source);
        }

        const float* __fastcall hookLocalAmbientTransform(
            const void* state) noexcept
        {
            const auto* source = originalLocalAmbientTransformAccessor ?
                originalLocalAmbientTransformAccessor(state) :
                nullptr;
            return prepareAmbientTransform(source);
        }

        [[nodiscard]] bool dFLightProducerCallsitesOwned() noexcept
        {
            const auto* nativePow = executableImage ?
                executableImage + kNativeScalarPowThunkRva :
                nullptr;
            const auto* localAccessor = executableImage ?
                executableImage + kLocalAmbientTransformAccessorRva :
                nullptr;
            const auto* fallbackAccessor = executableImage ?
                executableImage + kFallbackAmbientTransformAccessorRva :
                nullptr;
            return nativePow && localAccessor && fallbackAccessor &&
                isExecutableRange(
                    nativePow,
                    kNativeScalarPowThunkSignature.size()) &&
                std::memcmp(
                    nativePow,
                    kNativeScalarPowThunkSignature.data(),
                    kNativeScalarPowThunkSignature.size()) == 0 &&
                isExecutableRange(
                    localAccessor,
                    kLocalAmbientAccessorSignature.size()) &&
                std::memcmp(
                    localAccessor,
                    kLocalAmbientAccessorSignature.data(),
                    kLocalAmbientAccessorSignature.size()) == 0 &&
                isExecutableRange(
                    fallbackAccessor,
                    kFallbackAmbientAccessorSignature.size()) &&
                std::memcmp(
                    fallbackAccessor,
                    kFallbackAmbientAccessorSignature.data(),
                    kFallbackAmbientAccessorSignature.size()) == 0 &&
                absoluteJumpStubOwned(
                    directionalPowJumpStub,
                    reinterpret_cast<const void*>(&hookDirectionalScalarPow)) &&
                absoluteJumpStubOwned(
                    fallbackAmbientJumpStub,
                    reinterpret_cast<const void*>(
                        &hookFallbackAmbientTransform)) &&
                absoluteJumpStubOwned(
                    localAmbientJumpStub,
                    reinterpret_cast<const void*>(&hookLocalAmbientTransform)) &&
                callsiteFamilyOwned(
                    kDirectionalPowCallsites,
                    directionalPowJumpStub) &&
                resolveRelativeCallTarget(
                    executableImage + kAmbientTransformCallsites[0].rva) ==
                    fallbackAmbientJumpStub &&
                resolveRelativeCallTarget(
                    executableImage + kAmbientTransformCallsites[1].rva) ==
                    localAmbientJumpStub;
        }

        [[nodiscard]] bool restoreProducerCallsites(
            std::size_t patchedCallsites) noexcept
        {
            bool restored = true;
            std::size_t index{};
            for (const auto& callsite : kDirectionalPowCallsites) {
                if (index++ >= patchedCallsites) {
                    return restored;
                }
                restored = writeExecutableBytes(
                               executableImage + callsite.rva,
                               callsite.signature.data(),
                               callsite.signature.size()) &&
                    restored;
            }
            for (const auto& callsite : kAmbientTransformCallsites) {
                if (index++ >= patchedCallsites) {
                    return restored;
                }
                restored = writeExecutableBytes(
                               executableImage + callsite.rva,
                               callsite.signature.data(),
                               callsite.signature.size()) &&
                    restored;
            }
            return restored;
        }

        void releaseProducerPatchStateIfRestored(bool restored) noexcept
        {
            if (!restored) {
                return;
            }
            if (powJumpIsland) {
                (void)VirtualFree(powJumpIsland, 0, MEM_RELEASE);
            }
            originalNativeScalarPow = nullptr;
            originalLocalAmbientTransformAccessor = nullptr;
            originalFallbackAmbientTransformAccessor = nullptr;
            executableImage = nullptr;
            powJumpIsland = nullptr;
            directionalPowJumpStub = nullptr;
            fallbackAmbientJumpStub = nullptr;
            localAmbientJumpStub = nullptr;
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

        [[nodiscard]] std::uint32_t readAndRecordDFLightDescriptor(
            const void* pass) noexcept
        {
            std::uint32_t descriptor{};
            if (pass) {
                std::memcpy(
                    &descriptor,
                    static_cast<const std::byte*>(pass) +
                        kDFLightDescriptorOffset,
                    sizeof(descriptor));
            }
            lastDescriptor.store(descriptor, std::memory_order_relaxed);
            switch (linear_lighting::classifyDFLightProducer(descriptor)) {
            case linear_lighting::DFLightProducerKind::ambient:
                ambientDescriptors.fetch_add(1, std::memory_order_relaxed);
                break;
            case linear_lighting::DFLightProducerKind::directional:
                directionalDescriptors.fetch_add(1, std::memory_order_relaxed);
                break;
            case linear_lighting::DFLightProducerKind::other:
                otherDescriptors.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            return descriptor;
        }

        void __fastcall hookTechniqueSetup(
            void* receiver,
            void* pass,
            void* compiledProgram,
            void* techniqueState) noexcept
        {
            const auto descriptor = readAndRecordDFLightDescriptor(pass);
            {
                DFLightDescriptorScope descriptorScope(descriptor);
                originalTechniqueSetup(
                    receiver,
                    pass,
                    compiledProgram,
                    techniqueState);
            }
            techniqueCalls.fetch_add(1, std::memory_order_release);
        }

        void __fastcall hookGeometrySetup(
            void* receiver,
            void* pass,
            void* compiledProgram) noexcept
        {
            const auto descriptor = readAndRecordDFLightDescriptor(pass);

            // Preserve the engine's complete selector-2 map/write/unmap/bind
            // transaction exactly once. The descriptor scope exists only for
            // the verified native pow calls reached by this transaction.
            {
                DFLightDescriptorScope descriptorScope(descriptor);
                originalGeometrySetup(receiver, pass, compiledProgram);
            }

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
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            logging::error(
                "BSDF lighting geometry hook rejected invalid PE metadata.");
            return false;
        }
        const auto imageSize =
            static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage);
        const auto callsiteInBounds = [imageSize](const auto& callsites) {
            return std::ranges::all_of(
                callsites,
                [imageSize](const PowCallsite& callsite) {
                    return callsite.rva <= imageSize &&
                        callsite.signature.size() <= imageSize - callsite.rva;
                });
        };
        if (kBSDFLightShaderVtableRva +
                    (kGeometrySetupSlot + 1) * sizeof(void*) >
                imageSize ||
            kTechniqueSetupFunctionRva + kTechniqueSetupSignature.size() >
                imageSize ||
            kGeometrySetupFunctionRva + kGeometrySetupSignature.size() >
                imageSize ||
            kLightingStateAccessorRva +
                    kLightingStateAccessorSignature.size() >
                imageSize ||
            kLightingStateRva + kLightingStateEmissiveMultiplierOffset +
                    sizeof(float) >
                imageSize ||
            kNativeScalarPowThunkRva +
                    kNativeScalarPowThunkSignature.size() >
                imageSize ||
            kLocalAmbientTransformAccessorRva +
                    kLocalAmbientAccessorSignature.size() >
                imageSize ||
            kFallbackAmbientTransformAccessorRva +
                    kFallbackAmbientAccessorSignature.size() >
                imageSize ||
            !callsiteInBounds(kAmbientTransformCallsites) ||
            !callsiteInBounds(kDirectionalPowCallsites)) {
            logging::error(
                "BSDF lighting geometry hook rejected invalid PE image bounds.");
            return false;
        }

        auto** techniqueCell = reinterpret_cast<void**>(
            image + kBSDFLightShaderVtableRva +
            kTechniqueSetupSlot * sizeof(void*));
        auto** geometryCell = reinterpret_cast<void**>(
            image + kBSDFLightShaderVtableRva +
            kGeometrySetupSlot * sizeof(void*));
        auto* expectedTechnique = image + kTechniqueSetupFunctionRva;
        auto* expectedGeometry = image + kGeometrySetupFunctionRva;
        const auto* accessor = image + kLightingStateAccessorRva;
        const auto* state = image + kLightingStateRva;
        const auto* nativePow = image + kNativeScalarPowThunkRva;
        const auto* localAmbientAccessor =
            image + kLocalAmbientTransformAccessorRva;
        const auto* fallbackAmbientAccessor =
            image + kFallbackAmbientTransformAccessorRva;
        if (!isReadableRange(techniqueCell, sizeof(*techniqueCell)) ||
            *techniqueCell != expectedTechnique ||
            !isExecutableRange(
                expectedTechnique,
                kTechniqueSetupSignature.size()) ||
            std::memcmp(
                expectedTechnique,
                kTechniqueSetupSignature.data(),
                kTechniqueSetupSignature.size()) != 0 ||
            !isReadableRange(geometryCell, sizeof(*geometryCell)) ||
            *geometryCell != expectedGeometry ||
            !isExecutableRange(
                expectedGeometry,
                kGeometrySetupSignature.size()) ||
            std::memcmp(
                expectedGeometry,
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
                sizeof(float)) ||
            !isExecutableRange(
                nativePow,
                kNativeScalarPowThunkSignature.size()) ||
            std::memcmp(
                nativePow,
                kNativeScalarPowThunkSignature.data(),
                kNativeScalarPowThunkSignature.size()) != 0 ||
            !isExecutableRange(
                localAmbientAccessor,
                kLocalAmbientAccessorSignature.size()) ||
            std::memcmp(
                localAmbientAccessor,
                kLocalAmbientAccessorSignature.data(),
                kLocalAmbientAccessorSignature.size()) != 0 ||
            !isExecutableRange(
                fallbackAmbientAccessor,
                kFallbackAmbientAccessorSignature.size()) ||
            std::memcmp(
                fallbackAmbientAccessor,
                kFallbackAmbientAccessorSignature.data(),
                kFallbackAmbientAccessorSignature.size()) != 0) {
            logging::error(
                "BSDF lighting geometry/producer live identity gate failed; Linear Lighting remains vanilla.");
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
        const auto originalCallsitesOwned = [image](
                                                   const auto& callsites,
                                                   const std::byte* target) {
            for (const auto& callsite : callsites) {
                const auto* instruction = image + callsite.rva;
                if (!isExecutableRange(
                        instruction,
                        callsite.signature.size()) ||
                    std::memcmp(
                        instruction,
                        callsite.signature.data(),
                        callsite.signature.size()) != 0 ||
                    resolveRelativeCallTarget(instruction) != target) {
                    return false;
                }
            }
            return true;
        };
        if (!originalCallsitesOwned(kDirectionalPowCallsites, nativePow) ||
            !originalCallsitesOwned(
                std::span{ kAmbientTransformCallsites }.first<1>(),
                fallbackAmbientAccessor) ||
            !originalCallsitesOwned(
                std::span{ kAmbientTransformCallsites }.subspan<1, 1>(),
                localAmbientAccessor)) {
            logging::error(
                "BSDF DFLight producer-callsite identity gate failed; Linear Lighting remains vanilla.");
            return false;
        }

        const auto imageAddress = reinterpret_cast<std::uintptr_t>(image);
        if (imageAddress >
            std::numeric_limits<std::uintptr_t>::max() - imageSize) {
            logging::error(
                "BSDF DFLight jump-island preferred address overflowed; Linear Lighting remains vanilla.");
            return false;
        }
        std::array<std::uintptr_t,
            kAmbientTransformCallsites.size() +
                kDirectionalPowCallsites.size()>
            nextInstructions{};
        std::size_t nextIndex{};
        for (const auto& callsite : kDirectionalPowCallsites) {
            nextInstructions[nextIndex++] = imageAddress + callsite.rva +
                kRelativeCallBytes;
        }
        for (const auto& callsite : kAmbientTransformCallsites) {
            nextInstructions[nextIndex++] = imageAddress + callsite.rva +
                kRelativeCallBytes;
        }
        auto* island = static_cast<std::byte*>(
            support::near_allocation::allocateReachablePage(
                nextInstructions,
                imageAddress + imageSize,
                kJumpIslandRequiredBytes));
        if (!island) {
            logging::error(
                "BSDF DFLight producer could not allocate a reachable jump island; Linear Lighting remains vanilla.");
            return false;
        }
        auto* directionalStub = island + kDirectionalJumpStubOffset;
        auto* fallbackAmbientStub =
            island + kFallbackAmbientJumpStubOffset;
        auto* localAmbientStub = island + kLocalAmbientJumpStubOffset;
        buildAbsoluteJumpStub(
            directionalStub,
            reinterpret_cast<const void*>(&hookDirectionalScalarPow));
        buildAbsoluteJumpStub(
            fallbackAmbientStub,
            reinterpret_cast<const void*>(&hookFallbackAmbientTransform));
        buildAbsoluteJumpStub(
            localAmbientStub,
            reinterpret_cast<const void*>(&hookLocalAmbientTransform));
        DWORD oldIslandProtection{};
        if (!VirtualProtect(
                island,
                kJumpIslandRequiredBytes,
                PAGE_EXECUTE_READ,
                &oldIslandProtection)) {
            (void)VirtualFree(island, 0, MEM_RELEASE);
            logging::error(
                "BSDF DFLight jump-island protection failed; Linear Lighting remains vanilla.");
            return false;
        }
        FlushInstructionCache(
            GetCurrentProcess(),
            island,
            kJumpIslandRequiredBytes);

        executableImage = image;
        originalNativeScalarPow =
            reinterpret_cast<NativeScalarPowFunction>(
                const_cast<std::byte*>(nativePow));
        originalLocalAmbientTransformAccessor =
            reinterpret_cast<LocalAmbientTransformAccessorFunction>(
                const_cast<std::byte*>(localAmbientAccessor));
        originalFallbackAmbientTransformAccessor =
            reinterpret_cast<FallbackAmbientTransformAccessorFunction>(
                const_cast<std::byte*>(fallbackAmbientAccessor));
        powJumpIsland = island;
        directionalPowJumpStub = directionalStub;
        fallbackAmbientJumpStub = fallbackAmbientStub;
        localAmbientJumpStub = localAmbientStub;
        producerOwnershipReady.store(false, std::memory_order_release);

        std::size_t patchedCallsites{};
        const auto patchFamily = [&patchedCallsites](
                                     const auto& callsites,
                                     const void* target) {
            for (const auto& callsite : callsites) {
                ++patchedCallsites;
                if (!writeRelativeCall(
                        executableImage + callsite.rva,
                        target)) {
                    return false;
                }
            }
            return true;
        };
        if (!patchFamily(kDirectionalPowCallsites, directionalStub) ||
            !patchFamily(
                std::span{ kAmbientTransformCallsites }.first<1>(),
                fallbackAmbientStub) ||
            !patchFamily(
                std::span{ kAmbientTransformCallsites }.subspan<1, 1>(),
                localAmbientStub) ||
            !dFLightProducerCallsitesOwned()) {
            const auto restored = restoreProducerCallsites(patchedCallsites);
            releaseProducerPatchStateIfRestored(restored);
            logging::error(
                "BSDF DFLight producer patch/ownership gate failed (restored={}); Linear Lighting remains vanilla.",
                restored);
            return false;
        }

        originalTechniqueSetup =
            reinterpret_cast<TechniqueSetupFunction>(expectedTechnique);
        originalGeometrySetup =
            reinterpret_cast<GeometrySetupFunction>(expectedGeometry);
        lightingState = state;
        if (!patchPointer(
                techniqueCell,
                expectedTechnique,
                reinterpret_cast<void*>(&hookTechniqueSetup))) {
            originalTechniqueSetup = nullptr;
            originalGeometrySetup = nullptr;
            lightingState = nullptr;
            const auto restored = restoreProducerCallsites(patchedCallsites);
            releaseProducerPatchStateIfRestored(restored);
            logging::error(
                "BSDF lighting technique vtable patch failed (producer restored={}); Linear Lighting remains vanilla.",
                restored);
            return false;
        }
        if (!patchPointer(
                geometryCell,
                expectedGeometry,
                reinterpret_cast<void*>(&hookGeometrySetup))) {
            const auto techniqueRestored = patchPointer(
                techniqueCell,
                reinterpret_cast<void*>(&hookTechniqueSetup),
                expectedTechnique);
            const auto producerRestored = techniqueRestored &&
                restoreProducerCallsites(patchedCallsites);
            releaseProducerPatchStateIfRestored(producerRestored);
            if (techniqueRestored) {
                originalTechniqueSetup = nullptr;
            }
            originalGeometrySetup = nullptr;
            lightingState = nullptr;
            logging::error(
                "BSDF lighting geometry vtable patch failed (techniqueRestored={}, producerRestored={}); Linear Lighting remains vanilla.",
                techniqueRestored,
                producerRestored);
            return false;
        }

        techniqueSetupCell = techniqueCell;
        geometrySetupCell = geometryCell;
        producerOwnershipReady.store(true, std::memory_order_release);
        installed.store(true, std::memory_order_release);
        linear_lighting::Runtime::get().setGeometryProviderReady(true);
        logging::info(
            "Installed verified Fallout4VR BSDF lighting hook (vtable slots 3/9, six directional pow callsites, two ambient-transform callsites, renderer state RVA 0x068787F0 +0x1BC).");
        return true;
    }

    bool validateBSLightingGeometryHook(const char* trigger) noexcept
    {
        const auto hookInstalled = installed.load(std::memory_order_acquire);
        const auto techniqueVtableOwned = hookInstalled &&
            readPointerCell(techniqueSetupCell) ==
                reinterpret_cast<void*>(&hookTechniqueSetup);
        const auto geometryVtableOwned = hookInstalled &&
            readPointerCell(geometrySetupCell) ==
                reinterpret_cast<void*>(&hookGeometrySetup);
        const auto producerCallsitesOwned =
            hookInstalled && dFLightProducerCallsitesOwned();
        const auto owned =
            techniqueVtableOwned && geometryVtableOwned &&
            producerCallsitesOwned;
        producerOwnershipReady.store(owned, std::memory_order_release);
        if (!owned && hookInstalled) {
            linear_lighting::Runtime::get().setGeometryProviderReady(false);
            const auto failures = validationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if ((failures & (failures - 1)) == 0) {
                logging::error(
                    "BSDF lighting hook ownership validation failed at '{}' (techniqueVtableOwned={}, geometryVtableOwned={}, dFLightProducerCallsitesOwned={}, failures={}); geometry and DFLight producers are fail-closed.",
                    trigger ? trigger : "unknown",
                    techniqueVtableOwned,
                    geometryVtableOwned,
                    producerCallsitesOwned,
                    failures);
            }
        }
        return owned;
    }

    void publishDFLightProducerSettings(
        const linear_lighting::Settings& settings) noexcept
    {
        const auto state = linear_lighting::makeDFLightProducerState(settings);
        desiredDirectionalGammaBits.store(
            std::bit_cast<std::uint32_t>(state.directionalGamma),
            std::memory_order_relaxed);
        desiredDirectionalMultiplierBits.store(
            std::bit_cast<std::uint32_t>(state.directionalMultiplier),
            std::memory_order_relaxed);
        desiredAmbientGammaBits.store(
            std::bit_cast<std::uint32_t>(state.ambientGamma),
            std::memory_order_relaxed);
        desiredAmbientMultiplierBits.store(
            std::bit_cast<std::uint32_t>(state.ambientMultiplier),
            std::memory_order_relaxed);
        desiredAmbientInputScaleBits.store(
            std::bit_cast<std::uint32_t>(
                linear_lighting::directionalAmbientInputScale(
                    state.ambientMultiplier,
                    state.ambientGamma)),
            std::memory_order_relaxed);
        desiredProducerEnabled.store(state.enabled, std::memory_order_release);
    }

    GeometryHookSnapshot geometryHookSnapshot() noexcept
    {
        const auto hookInstalled = installed.load(std::memory_order_acquire);
        // Ownership is refreshed only at explicit lifecycle/qualification
        // validation points. Wrist telemetry can request snapshots every
        // frame, so it must not perform VirtualQuery or scan six callsites.
        const auto producerOwned = hookInstalled &&
            producerOwnershipReady.load(std::memory_order_acquire);
        const auto producerActive = producerOwned &&
            desiredProducerEnabled.load(std::memory_order_acquire);
        const auto techniqueOwned = hookInstalled &&
            readPointerCell(techniqueSetupCell) ==
                reinterpret_cast<void*>(&hookTechniqueSetup);
        const auto geometryOwned = hookInstalled &&
            readPointerCell(geometrySetupCell) ==
                reinterpret_cast<void*>(&hookGeometrySetup);
        return {
            .installed = hookInstalled,
            .vtableCellOwned = techniqueOwned && geometryOwned,
            .techniqueVtableCellOwned = techniqueOwned,
            .geometryVtableCellOwned = geometryOwned,
            .dFLightProducerCallsitesOwned = producerOwned,
            .dFLightProducerEnabled = producerActive,
            .techniqueCalls =
                techniqueCalls.load(std::memory_order_acquire),
            .calls = calls.load(std::memory_order_acquire),
            .acceptedUpdates = acceptedUpdates.load(std::memory_order_relaxed),
            .rejectedSources = rejectedSources.load(std::memory_order_relaxed),
            .ambientDescriptors =
                ambientDescriptors.load(std::memory_order_relaxed),
            .directionalDescriptors =
                directionalDescriptors.load(std::memory_order_relaxed),
            .otherDescriptors =
                otherDescriptors.load(std::memory_order_relaxed),
            .ambientTransformCalls =
                ambientTransformCalls.load(std::memory_order_acquire),
            .ambientTransformPrepared =
                ambientTransformPrepared.load(std::memory_order_relaxed),
            .ambientTransformPassThrough =
                ambientTransformPassThrough.load(std::memory_order_relaxed),
            .directionalPowCalls =
                directionalPowCalls.load(std::memory_order_acquire),
            .directionalPowModified =
                directionalPowModified.load(std::memory_order_relaxed),
            .directionalPowPassThrough =
                directionalPowPassThrough.load(std::memory_order_relaxed),
            .invalidPowResults =
                invalidPowResults.load(std::memory_order_relaxed),
            .validationFailures =
                validationFailures.load(std::memory_order_relaxed),
            .deepestStage = static_cast<GeometrySourceStage>(
                deepestStage.load(std::memory_order_relaxed)),
            .lastDescriptor = lastDescriptor.load(std::memory_order_relaxed),
            .lastSourceEmissiveMultiplier = std::bit_cast<float>(
                lastSourceEmissiveMultiplierBits.load(
                    std::memory_order_relaxed)),
            .activeDirectionalGamma = producerActive ?
                std::bit_cast<float>(desiredDirectionalGammaBits.load(
                    std::memory_order_relaxed)) :
                linear_lighting::kVanillaDFLightGamma,
            .activeDirectionalMultiplier = producerActive ?
                std::bit_cast<float>(desiredDirectionalMultiplierBits.load(
                    std::memory_order_relaxed)) :
                1.0f,
            .activeAmbientGamma = producerActive ?
                std::bit_cast<float>(
                    desiredAmbientGammaBits.load(std::memory_order_relaxed)) :
                linear_lighting::kVanillaDFLightGamma,
            .activeAmbientMultiplier = producerActive ?
                std::bit_cast<float>(desiredAmbientMultiplierBits.load(
                    std::memory_order_relaxed)) :
                1.0f,
        };
    }
}
