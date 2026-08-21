#include "PCH.h"

#pragma push_macro("MEM_RELEASE")
#pragma push_macro("MAX_PATH")
#pragma push_macro("near")
#pragma push_macro("far")
#undef MEM_RELEASE
#undef MAX_PATH
#undef near
#undef far
#include <RE/Fallout.h>
#pragma pop_macro("far")
#pragma pop_macro("near")
#pragma pop_macro("MAX_PATH")
#pragma pop_macro("MEM_RELEASE")

#include "Features/skylighting/SkylightingNativeHooks.h"

#include "Features/skylighting/SkylightingRuntime.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
        constexpr std::uintptr_t kDepthTargetMapperRva = 0x01DB9E40;
        constexpr std::uintptr_t kRendererStateRva = 0x038AC010;
        constexpr std::uintptr_t kCubeSizeRva = 0x05A3CFA4;
        constexpr std::uintptr_t kDirectionRva = 0x05A3CFC8;
        constexpr std::uintptr_t kLightingPropertyVtableRva = 0x030A5C18;
        constexpr std::size_t kLightingPassBuilderSlot = 0x2E;
        constexpr std::uintptr_t kLightingPassBuilderRva = 0x027A48B0;
        constexpr std::uintptr_t kLightingPassListResolverRva = 0x027A51E0;
        constexpr std::uintptr_t kPassListClearRva = 0x0278E3E0;
        constexpr std::uintptr_t kPassListEmplaceRva = 0x0278E610;
        constexpr std::uintptr_t kUtilityShaderSingletonRva = 0x0689B4F0;
        constexpr std::uintptr_t kUtilityShaderVtableRva = 0x030BD988;
        constexpr std::uintptr_t kUtilityShaderSecondaryVtableRva =
            0x030BD9F8;
        constexpr std::uintptr_t kBsxFlagsVtableRva = 0x02E72CB8;
        constexpr std::size_t kRenderDepthTargetSetupOffset = 0x1CC;
        constexpr std::size_t kDepthTargetMapperSignatureOffset = 0x1A;
        constexpr std::size_t kDepthTargetMapOffset = 0x15FC;
        constexpr std::ptrdiff_t kPrecipitationManagerOffset = 0xA0;
        constexpr std::size_t kPrecipitationManagerReadableSize = 0x98;
        constexpr std::size_t kAccumulatorPassIndexOffset = 0xF6B0;
        constexpr std::size_t kAccumulatorPassKeyOffset = 0xF6B8;
        constexpr std::uint32_t kAccumulatorPassListCount = 4;
        constexpr std::size_t kBsxValueOffset = 0x18;
        constexpr std::size_t kUtilityShaderSecondaryVtableOffset = 0x10;
        constexpr std::size_t kUtilityShaderKindOffset = 0x18;
        constexpr std::uint32_t kUtilityShaderKind = 1;
        constexpr std::size_t kUtilityShaderIdentitySize = 0x1C;
        constexpr std::size_t kMaximumParentTraversal = 64;
        constexpr float kMinimumOccluderRadius = 32.0f;
        constexpr std::uint32_t kUtilityVertexColorDescriptor = 1u << 0;
        constexpr std::uint32_t kUtilityTextureDescriptor = 1u << 1;
        constexpr std::uint32_t kUtilityAlphaTestDescriptor = 1u << 7;
        constexpr std::uint32_t kUtilityRenderDepthDescriptor = 1u << 13;
        constexpr std::uint32_t kUtilityTreeAnimDescriptor = 1u << 26;
        constexpr std::uint8_t kUtilityDepthPassCategory = 0x1E;
        constexpr std::uint16_t kNiAlphaPropertyAlphaTest = 1u << 9;
        constexpr std::int32_t kExcludedBsxFlags = 0x3D54;

        constexpr std::uint64_t kPropertySkinned = 1ull << 1;
        constexpr std::uint64_t kPropertyTempRefraction = 1ull << 2;
        constexpr std::uint64_t kPropertyRefraction = 1ull << 15;
        constexpr std::uint64_t kPropertyEyeReflect = 1ull << 17;
        constexpr std::uint64_t kPropertyDecal = 1ull << 26;
        constexpr std::uint64_t kPropertyDynamicDecal = 1ull << 27;
        constexpr std::uint64_t kPropertyZBufferWrite = 1ull << 32;
        constexpr std::uint64_t kPropertyLodLandscape = 1ull << 33;
        constexpr std::uint64_t kPropertyVertexColors = 1ull << 37;
        constexpr std::uint64_t kPropertyTreeAnim = 1ull << 61;
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
        constexpr std::array<std::byte, 27> kRenderDepthTargetSetupSignature{
            std::byte{ 0x45 }, std::byte{ 0x33 }, std::byte{ 0xC9 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x0D },
            std::byte{ 0x7A }, std::byte{ 0x6D }, std::byte{ 0x27 },
            std::byte{ 0x03 }, std::byte{ 0x41 }, std::byte{ 0x8D },
            std::byte{ 0x51 }, std::byte{ 0x09 }, std::byte{ 0x45 },
            std::byte{ 0x33 }, std::byte{ 0xC0 }, std::byte{ 0xC6 },
            std::byte{ 0x44 }, std::byte{ 0x24 }, std::byte{ 0x20 },
            std::byte{ 0x00 }, std::byte{ 0xE8 }, std::byte{ 0x99 },
            std::byte{ 0x4B }, std::byte{ 0x78 }, std::byte{ 0x01 },
        };
        constexpr std::array<std::byte, 17> kDepthTargetMapperSignature{
            std::byte{ 0x48 }, std::byte{ 0x63 }, std::byte{ 0xC2 },
            std::byte{ 0x41 }, std::byte{ 0x89 }, std::byte{ 0x92 },
            std::byte{ 0x88 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x8B }, std::byte{ 0x8C },
            std::byte{ 0x81 }, std::byte{ 0xFC }, std::byte{ 0x15 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 33> kLightingPassBuilderSignature{
            std::byte{ 0x40 }, std::byte{ 0x53 }, std::byte{ 0x55 },
            std::byte{ 0x56 }, std::byte{ 0x41 }, std::byte{ 0x55 },
            std::byte{ 0x41 }, std::byte{ 0x57 }, std::byte{ 0x48 },
            std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x60 },
            std::byte{ 0x65 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0x04 }, std::byte{ 0x25 }, std::byte{ 0x58 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x44 }, std::byte{ 0x8B }, std::byte{ 0x15 },
            std::byte{ 0x00 }, std::byte{ 0x82 }, std::byte{ 0x0F },
            std::byte{ 0x04 }, std::byte{ 0x41 }, std::byte{ 0xBB },
            std::byte{ 0xC0 }, std::byte{ 0x09 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 32>
            kLightingPassListResolverSignature{
                std::byte{ 0x45 }, std::byte{ 0x33 }, std::byte{ 0xC0 },
                std::byte{ 0x4C }, std::byte{ 0x8B }, std::byte{ 0xD1 },
                std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x81 },
                std::byte{ 0x90 }, std::byte{ 0x00 }, std::byte{ 0x00 },
                std::byte{ 0x00 }, std::byte{ 0x45 }, std::byte{ 0x33 },
                std::byte{ 0xC9 }, std::byte{ 0x48 }, std::byte{ 0x39 },
                std::byte{ 0x10 }, std::byte{ 0x74 }, std::byte{ 0x10 },
                std::byte{ 0x49 }, std::byte{ 0xFF }, std::byte{ 0xC1 },
                std::byte{ 0x41 }, std::byte{ 0xFF }, std::byte{ 0xC0 },
                std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xC0 },
                std::byte{ 0x20 }, std::byte{ 0x49 },
            };
        constexpr std::array<std::byte, 32> kPassListClearSignature{
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x5C },
            std::byte{ 0x24 }, std::byte{ 0x10 }, std::byte{ 0x56 },
            std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xEC },
            std::byte{ 0x20 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0x19 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0xF1 }, std::byte{ 0x48 }, std::byte{ 0x85 },
            std::byte{ 0xDB }, std::byte{ 0x74 }, std::byte{ 0x3F },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x7C },
            std::byte{ 0x24 }, std::byte{ 0x30 }, std::byte{ 0x66 },
            std::byte{ 0x0F }, std::byte{ 0x1F }, std::byte{ 0x44 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 32> kPassListEmplaceSignature{
            std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xEC },
            std::byte{ 0x68 }, std::byte{ 0x4D }, std::byte{ 0x8B },
            std::byte{ 0xD1 }, std::byte{ 0x48 }, std::byte{ 0x85 },
            std::byte{ 0xD2 }, std::byte{ 0x75 }, std::byte{ 0x48 },
            std::byte{ 0x44 }, std::byte{ 0x8B }, std::byte{ 0x8C },
            std::byte{ 0x24 }, std::byte{ 0x90 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x33 },
            std::byte{ 0xC0 }, std::byte{ 0x49 }, std::byte{ 0x8B },
            std::byte{ 0xD2 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x44 }, std::byte{ 0x24 }, std::byte{ 0x50 },
            std::byte{ 0x48 }, std::byte{ 0x89 },
        };

        using WrapperFunction = void(__fastcall*)();
        using NativeSkySingleton = void*(__fastcall*)();
        using LightingPassBuilder = void*(__fastcall*)(
            void* property,
            void* geometry,
            std::uint32_t renderMode,
            void* accumulator);
        using PassListClear = void(__fastcall*)(void** list);
        using LightingPassListResolver = void**(__fastcall*)(
            void* property,
            void* passKey);
        using PassListEmplace = void*(__fastcall*)(
            void** list,
            void* reusablePass,
            void* property,
            void* shader,
            std::uint32_t descriptor,
            std::uint8_t passCategory,
            void* geometry);

        struct DetourIdentity
        {
            const std::byte* patch{};
            const void* destination{};
        };

        struct PointerPatchOutcome
        {
            bool owned{};
            bool protectionRestored{};
            bool rolledBack{};
        };

        struct UtilityShaderIdentity
        {
            void* primaryVtable{};
            void* secondaryVtable{};
            std::uint32_t kind{};
            bool primaryExact{};
            bool primaryCallable{};
            bool secondaryExact{};

            [[nodiscard]] bool valid() const noexcept
            {
                return primaryCallable && secondaryExact &&
                    kind == kUtilityShaderKind;
            }
        };

        enum class BsxFilterResult
        {
            include,
            exclude,
            invalid,
        };

        WrapperFunction originalWrapper{};
        NativeSkySingleton nativeSkySingleton{};
        NativePrecipitationRender nativeRender{};
        NativeProjectionSetup nativeProjection{};
        LightingPassBuilder originalLightingPassBuilder{};
        LightingPassListResolver resolveLightingPassList{};
        PassListClear clearPassList{};
        PassListEmplace emplacePass{};
        void** lightingPassBuilderCell{};
        void* utilityShader{};
        const void* utilityShaderVtable{};
        const void* utilityShaderSecondaryVtable{};
        const void* bsxFlagsVtable{};
        std::optional<RE::BSFixedString> bsxKey;
        std::byte* wrapperTarget{};
        DetourIdentity installedIdentity{};
        std::atomic_bool installed{};
        std::atomic_bool passProducerReady{};
        std::atomic_bool utilityShaderDeferredLogged{};
        std::atomic_bool firstCallbackLogged{};
        std::atomic_bool missingManagerLogged{};
        std::atomic_uint64_t passProducerCalls{};
        std::atomic_uint64_t emittedPasses{};
        std::atomic_uint64_t rejectedInvalid{};
        std::atomic_uint64_t rejectedSkinned{};
        std::atomic_uint64_t rejectedSmall{};
        std::atomic_uint64_t rejectedBsx{};
        std::atomic_uint64_t rejectedFlags{};
        std::atomic_uint64_t rejectedAllocation{};
        std::atomic_bool passProductionActive{};

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

        [[nodiscard]] void* readPointerCell(void** cell) noexcept
        {
            return cell ? ReadPointerAcquire(
                              reinterpret_cast<void* const volatile*>(cell)) :
                          nullptr;
        }

        [[nodiscard]] UtilityShaderIdentity inspectUtilityShader(
            void* instance,
            const void* expectedPrimaryVtable,
            const void* expectedSecondaryVtable) noexcept
        {
            UtilityShaderIdentity identity;
            if (!isReadableRange(instance, kUtilityShaderIdentitySize)) {
                return identity;
            }
            auto* bytes = static_cast<std::byte*>(instance);
            identity.primaryVtable = readPointerCell(
                reinterpret_cast<void**>(bytes));
            identity.secondaryVtable = readPointerCell(
                reinterpret_cast<void**>(
                    bytes + kUtilityShaderSecondaryVtableOffset));
            std::memcpy(
                &identity.kind,
                bytes + kUtilityShaderKindOffset,
                sizeof(identity.kind));
            identity.primaryExact =
                identity.primaryVtable == expectedPrimaryVtable;
            identity.secondaryExact =
                identity.secondaryVtable == expectedSecondaryVtable;
            if (isReadableRange(identity.primaryVtable, sizeof(void*))) {
                auto* firstMethod = readPointerCell(
                    reinterpret_cast<void**>(identity.primaryVtable));
                identity.primaryCallable =
                    isExecutableRange(firstMethod, 1);
            }
            return identity;
        }

        [[nodiscard]] PointerPatchOutcome patchPointerCell(
            void** target,
            void* expected,
            void* replacement) noexcept
        {
            PointerPatchOutcome result;
            if (!target || !expected || !replacement ||
                readPointerCell(target) != expected) {
                return result;
            }
            DWORD oldProtection{};
            if (VirtualProtect(
                    target,
                    sizeof(*target),
                    PAGE_READWRITE,
                    &oldProtection) == FALSE) {
                return result;
            }
            auto* observed = InterlockedCompareExchangePointer(
                target,
                replacement,
                expected);
            DWORD ignoredProtection{};
            result.protectionRestored = VirtualProtect(
                                            target,
                                            sizeof(*target),
                                            oldProtection,
                                            &ignoredProtection) != FALSE;
            FlushInstructionCache(
                GetCurrentProcess(),
                target,
                sizeof(*target));
            result.owned = observed == expected &&
                readPointerCell(target) == replacement;
            if (!result.owned || result.protectionRestored) {
                return result;
            }

            observed = InterlockedCompareExchangePointer(
                target,
                expected,
                replacement);
            DWORD secondIgnoredProtection{};
            const auto secondRestore = VirtualProtect(
                target,
                sizeof(*target),
                oldProtection,
                &secondIgnoredProtection);
            FlushInstructionCache(
                GetCurrentProcess(),
                target,
                sizeof(*target));
            result.rolledBack = observed == replacement &&
                readPointerCell(target) == expected;
            result.protectionRestored = secondRestore != FALSE;
            result.owned = readPointerCell(target) == replacement;
            return result;
        }

        [[nodiscard]] BsxFilterResult filterBsxFlags(
            RE::BSGeometry* geometry) noexcept
        {
            if (!geometry || geometry->userData == 0) {
                return BsxFilterResult::include;
            }
            if (!bsxKey || !bsxFlagsVtable) {
                return BsxFilterResult::invalid;
            }
            auto* parent = geometry->parent;
            std::size_t depth{};
            while (parent && depth < kMaximumParentTraversal) {
                auto* extra = parent->GetExtraData(*bsxKey);
                if (extra) {
                    auto* vtable = *reinterpret_cast<void**>(extra);
                    if (vtable != bsxFlagsVtable) {
                        return BsxFilterResult::invalid;
                    }
                    std::int32_t value{};
                    std::memcpy(
                        &value,
                        reinterpret_cast<const std::byte*>(extra) +
                            kBsxValueOffset,
                        sizeof(value));
                    return (value & kExcludedBsxFlags) != 0 ?
                        BsxFilterResult::exclude :
                        BsxFilterResult::include;
                }
                parent = parent->parent;
                ++depth;
            }
            return parent ? BsxFilterResult::invalid :
                            BsxFilterResult::include;
        }

        void* __fastcall hookLightingPassBuilder(
            void* propertyAddress,
            void* geometryAddress,
            std::uint32_t renderMode,
            void* accumulator) noexcept
        {
            if (!passProductionActive.load(std::memory_order_acquire) ||
                !passProducerReady.load(std::memory_order_acquire)) {
                return originalLightingPassBuilder ?
                    originalLightingPassBuilder(
                        propertyAddress,
                        geometryAddress,
                        renderMode,
                        accumulator) :
                    nullptr;
            }
            (void)renderMode;
            passProducerCalls.fetch_add(1, std::memory_order_relaxed);
            if (!propertyAddress || !geometryAddress || !accumulator ||
                !resolveLightingPassList || !clearPassList || !emplacePass ||
                !utilityShader ||
                !isReadableRange(
                    accumulator,
                    kAccumulatorPassKeyOffset + sizeof(void*))) {
                rejectedInvalid.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }

            auto* accumulatorBytes = static_cast<std::byte*>(accumulator);
            std::uint32_t passIndex{};
            std::memcpy(
                &passIndex,
                accumulatorBytes + kAccumulatorPassIndexOffset,
                sizeof(passIndex));
            if (passIndex >= kAccumulatorPassListCount) {
                rejectedInvalid.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            auto* passKey = readPointerCell(reinterpret_cast<void**>(
                accumulatorBytes + kAccumulatorPassKeyOffset));
            auto** passBucket = resolveLightingPassList(
                propertyAddress,
                passKey);
            if (!isReadableRange(
                    passBucket,
                    sizeof(void*) * kAccumulatorPassListCount)) {
                rejectedInvalid.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            auto** passList = passBucket + passIndex;
            clearPassList(passList);

            auto* property = static_cast<RE::BSShaderProperty*>(
                propertyAddress);
            auto* geometry = static_cast<RE::BSGeometry*>(geometryAddress);
            const auto propertyFlags = property->flags.underlying();
            const auto treeAnimated =
                (propertyFlags & kPropertyTreeAnim) != 0;
            if ((propertyFlags & kPropertySkinned) != 0 && !treeAnimated) {
                rejectedSkinned.fetch_add(1, std::memory_order_relaxed);
                return passList;
            }
            if (!std::isfinite(geometry->worldBound.fRadius)) {
                rejectedInvalid.fetch_add(1, std::memory_order_relaxed);
                return passList;
            }
            if (geometry->worldBound.fRadius <= kMinimumOccluderRadius) {
                rejectedSmall.fetch_add(1, std::memory_order_relaxed);
                return passList;
            }

            switch (filterBsxFlags(geometry)) {
            case BsxFilterResult::exclude:
                rejectedBsx.fetch_add(1, std::memory_order_relaxed);
                return passList;
            case BsxFilterResult::invalid:
                rejectedInvalid.fetch_add(1, std::memory_order_relaxed);
                return passList;
            case BsxFilterResult::include:
                break;
            }

            constexpr auto excludedPropertyFlags =
                kPropertyRefraction | kPropertyTempRefraction |
                kPropertyLodLandscape | kPropertyEyeReflect |
                kPropertyDecal | kPropertyDynamicDecal;
            if ((propertyFlags & kPropertyZBufferWrite) == 0 ||
                (propertyFlags & excludedPropertyFlags) != 0) {
                rejectedFlags.fetch_add(1, std::memory_order_relaxed);
                return passList;
            }

            auto descriptor = kUtilityRenderDepthDescriptor;
            if ((propertyFlags & kPropertyVertexColors) != 0) {
                descriptor |= kUtilityVertexColorDescriptor;
            }
            const auto& geometryRuntime = geometry->GetRuntimeData();
            auto* alphaProperty = geometryRuntime.properties[0].get();
            if (alphaProperty) {
                std::uint16_t alphaFlags{};
                std::memcpy(
                    &alphaFlags,
                    reinterpret_cast<const std::byte*>(alphaProperty) + 0x28,
                    sizeof(alphaFlags));
                if ((alphaFlags & kNiAlphaPropertyAlphaTest) != 0) {
                    descriptor |= kUtilityTextureDescriptor |
                        kUtilityAlphaTestDescriptor;
                }
            }
            if (treeAnimated) {
                descriptor |= kUtilityTreeAnimDescriptor;
            }
            // FO4VR's VS canonicalizer at 0x142974C50 strips the Skyrim
            // utility LOD bit. Do not publish an unsupported PS key.

            auto* pass = emplacePass(
                passList,
                nullptr,
                propertyAddress,
                utilityShader,
                descriptor,
                kUtilityDepthPassCategory,
                geometryAddress);
            if (!pass) {
                rejectedAllocation.fetch_add(1, std::memory_order_relaxed);
                return passList;
            }
            emittedPasses.fetch_add(1, std::memory_order_relaxed);
            return passList;
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

        [[nodiscard]] const std::byte* ripRelativeTarget(
            const std::byte* instruction) noexcept
        {
            if (!isReadableRange(instruction, 7) ||
                instruction[0] != std::byte{ 0x48 } ||
                instruction[1] != std::byte{ 0x8D } ||
                instruction[2] != std::byte{ 0x0D }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, instruction + 3, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) + 7;
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
            if (!nativeSkySingleton) {
                return nullptr;
            }
            auto* skyState = nativeSkySingleton();
            if (!skyState) {
                return nullptr;
            }
            auto* slot = static_cast<std::byte*>(skyState) +
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
                    "Skylighting could not resolve the persistent FO4VR precipitation manager at native Sky offset +0xA0; captures remain fail-closed.");
            }
            Runtime::get().onNativePrecipitationFrame(
                precipitation,
                nativeRender,
                nativeProjection);
        }
    }

    ScopedOcclusionPassProduction::ScopedOcclusionPassProduction() noexcept
    {
        active_ = passProducerReady.load(std::memory_order_acquire) &&
            originalLightingPassBuilder && resolveLightingPassList &&
            clearPassList && emplacePass && utilityShader &&
            lightingPassBuilderCell &&
            readPointerCell(lightingPassBuilderCell) ==
                reinterpret_cast<void*>(&hookLightingPassBuilder);
        if (active_) {
            auto expected = false;
            active_ = passProductionActive.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
    }

    ScopedOcclusionPassProduction::~ScopedOcclusionPassProduction() noexcept
    {
        if (active_) {
            passProductionActive.store(false, std::memory_order_release);
        }
    }

    bool ScopedOcclusionPassProduction::active() const noexcept
    {
        return active_;
    }

    OcclusionPassProducerSnapshot occlusionPassProducerSnapshot() noexcept
    {
        return {
            .owned = passProducerReady.load(std::memory_order_acquire) &&
                lightingPassBuilderCell &&
                readPointerCell(lightingPassBuilderCell) ==
                    reinterpret_cast<void*>(&hookLightingPassBuilder),
            .calls = passProducerCalls.load(std::memory_order_relaxed),
            .emittedPasses = emittedPasses.load(std::memory_order_relaxed),
            .rejectedInvalid = rejectedInvalid.load(
                std::memory_order_relaxed),
            .rejectedSkinned = rejectedSkinned.load(
                std::memory_order_relaxed),
            .rejectedSmall = rejectedSmall.load(std::memory_order_relaxed),
            .rejectedBsx = rejectedBsx.load(std::memory_order_relaxed),
            .rejectedFlags = rejectedFlags.load(std::memory_order_relaxed),
            .rejectedAllocation = rejectedAllocation.load(
                std::memory_order_relaxed),
        };
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
        if (!inImage(
                kRenderRva + kRenderDepthTargetSetupOffset,
                kRenderDepthTargetSetupSignature.size()) ||
            !inImage(
                kDepthTargetMapperRva +
                    kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.size()) ||
            !inImage(
                kRendererStateRva + kDepthTargetMapOffset,
                sizeof(std::int32_t) * 10)) {
            logging::error(
                "Skylighting native depth-target mapping contract is outside the FO4VR image.");
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
        if (!inImage(
                kLightingPropertyVtableRva +
                    kLightingPassBuilderSlot * sizeof(void*),
                sizeof(void*)) ||
            !inImage(
                kLightingPassBuilderRva,
                kLightingPassBuilderSignature.size()) ||
            !inImage(
                kLightingPassListResolverRva,
                kLightingPassListResolverSignature.size()) ||
            !inImage(kPassListClearRva, kPassListClearSignature.size()) ||
            !inImage(
                kPassListEmplaceRva,
                kPassListEmplaceSignature.size()) ||
            !inImage(kUtilityShaderSingletonRva, sizeof(void*)) ||
            !inImage(kUtilityShaderVtableRva, sizeof(void*)) ||
            !inImage(kUtilityShaderSecondaryVtableRva, sizeof(void*)) ||
            !inImage(kBsxFlagsVtableRva, sizeof(void*))) {
            logging::error(
                "Skylighting native world-occlusion producer contract is outside the FO4VR image.");
            return false;
        }

        auto* wrapper = image + kWrapperRva;
        auto* render = image + kRenderRva;
        auto* projection = image + kProjectionRva;
        auto* renderDepthTargetSetup =
            render + kRenderDepthTargetSetupOffset;
        auto* depthTargetMapper = image + kDepthTargetMapperRva;
        auto** lightingPassCell = reinterpret_cast<void**>(
            image + kLightingPropertyVtableRva +
            kLightingPassBuilderSlot * sizeof(void*));
        auto* expectedLightingPass = image + kLightingPassBuilderRva;
        auto* lightingPassListResolver =
            image + kLightingPassListResolverRva;
        auto* passListClear = image + kPassListClearRva;
        auto* passListEmplace = image + kPassListEmplaceRva;
        auto** utilityShaderCell = reinterpret_cast<void**>(
            image + kUtilityShaderSingletonRva);
        auto* expectedUtilityVtable = image + kUtilityShaderVtableRva;
        auto* expectedUtilitySecondaryVtable =
            image + kUtilityShaderSecondaryVtableRva;
        auto* expectedBsxVtable = image + kBsxFlagsVtableRva;
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
        if (!isExecutableRange(
                renderDepthTargetSetup,
                kRenderDepthTargetSetupSignature.size()) ||
            std::memcmp(
                renderDepthTargetSetup,
                kRenderDepthTargetSetupSignature.data(),
                kRenderDepthTargetSetupSignature.size()) != 0 ||
            ripRelativeTarget(renderDepthTargetSetup + 3) !=
                image + kRendererStateRva ||
            relativeTarget(renderDepthTargetSetup + 22) !=
                depthTargetMapper) {
            logging::error(
                "Skylighting native render depth-target setup signature mismatch at RVA 0x0063528C.");
            return false;
        }
        if (!isExecutableRange(
                depthTargetMapper + kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.size()) ||
            std::memcmp(
                depthTargetMapper + kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.data(),
                kDepthTargetMapperSignature.size()) != 0) {
            logging::error(
                "Skylighting native depth-target mapper signature mismatch at RVA 0x01DB9E5A.");
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
        if (!isReadableRange(lightingPassCell, sizeof(*lightingPassCell)) ||
            readPointerCell(lightingPassCell) != expectedLightingPass) {
            logging::error(
                "Skylighting rejected BSLightingShaderProperty precipitation pass-builder slot 0x2E: the verified FO4VR target RVA 0x027A48B0 is not installed.");
            return false;
        }
        if (!isExecutableRange(
                expectedLightingPass,
                kLightingPassBuilderSignature.size()) ||
            std::memcmp(
                expectedLightingPass,
                kLightingPassBuilderSignature.data(),
                kLightingPassBuilderSignature.size()) != 0) {
            logging::error(
                "Skylighting native precipitation pass-builder signature mismatch at RVA 0x027A48B0.");
            return false;
        }
        if (!isExecutableRange(
                lightingPassListResolver,
                kLightingPassListResolverSignature.size()) ||
            std::memcmp(
                lightingPassListResolver,
                kLightingPassListResolverSignature.data(),
                kLightingPassListResolverSignature.size()) != 0) {
            logging::error(
                "Skylighting native precipitation pass-list resolver signature mismatch at RVA 0x027A51E0.");
            return false;
        }
        if (!isExecutableRange(passListClear, kPassListClearSignature.size()) ||
            std::memcmp(
                passListClear,
                kPassListClearSignature.data(),
                kPassListClearSignature.size()) != 0) {
            logging::error(
                "Skylighting native pass-list clear signature mismatch at RVA 0x0278E3E0.");
            return false;
        }
        if (!isExecutableRange(
                passListEmplace,
                kPassListEmplaceSignature.size()) ||
            std::memcmp(
                passListEmplace,
                kPassListEmplaceSignature.data(),
                kPassListEmplaceSignature.size()) != 0) {
            logging::error(
                "Skylighting native pass-list emplace signature mismatch at RVA 0x0278E610.");
            return false;
        }
        if (!isReadableRange(utilityShaderCell, sizeof(*utilityShaderCell))) {
            logging::error(
                "Skylighting native utility-shader singleton cell at RVA 0x0689B4F0 is not readable.");
            return false;
        }
        auto* resolvedUtilityShader = readPointerCell(utilityShaderCell);
        if (!resolvedUtilityShader) {
            if (!utilityShaderDeferredLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Skylighting native hook installation is deferred until FO4VR constructs the utility-shader singleton.");
            }
            return false;
        }
        const auto utilityIdentity = inspectUtilityShader(
            resolvedUtilityShader,
            expectedUtilityVtable,
            expectedUtilitySecondaryVtable);
        if (!utilityIdentity.valid()) {
            logging::error(
                "Skylighting rejected the FO4VR utility-shader singleton (primary={}, expectedPrimary={}, callable={}, secondary={}, expectedSecondary={}, kind={}).",
                fmt::ptr(utilityIdentity.primaryVtable),
                fmt::ptr(expectedUtilityVtable),
                utilityIdentity.primaryCallable,
                fmt::ptr(utilityIdentity.secondaryVtable),
                fmt::ptr(expectedUtilitySecondaryVtable),
                utilityIdentity.kind);
            return false;
        }
        if (!utilityIdentity.primaryExact) {
            logging::warn(
                "Skylighting accepted a runtime-replaced utility-shader primary vtable {} through the exact secondary vtable RVA 0x030BD9F8 and shader-kind contract.",
                fmt::ptr(utilityIdentity.primaryVtable));
        }

        try {
            bsxKey.emplace("BSX");
        } catch (...) {
            logging::error(
                "Skylighting could not acquire the BSX extra-data key; the world-occlusion producer remains disabled.");
            return false;
        }
        originalLightingPassBuilder =
            reinterpret_cast<LightingPassBuilder>(expectedLightingPass);
        resolveLightingPassList =
            reinterpret_cast<LightingPassListResolver>(
                lightingPassListResolver);
        clearPassList = reinterpret_cast<PassListClear>(passListClear);
        emplacePass = reinterpret_cast<PassListEmplace>(passListEmplace);
        lightingPassBuilderCell = lightingPassCell;
        utilityShader = resolvedUtilityShader;
        utilityShaderVtable = expectedUtilityVtable;
        utilityShaderSecondaryVtable = expectedUtilitySecondaryVtable;
        bsxFlagsVtable = expectedBsxVtable;

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
            originalLightingPassBuilder = nullptr;
            resolveLightingPassList = nullptr;
            clearPassList = nullptr;
            emplacePass = nullptr;
            lightingPassBuilderCell = nullptr;
            utilityShader = nullptr;
            utilityShaderVtable = nullptr;
            utilityShaderSecondaryVtable = nullptr;
            bsxFlagsVtable = nullptr;
            bsxKey.reset();
            return false;
        }
        originalWrapper = reinterpret_cast<WrapperFunction>(trampoline);
        nativeSkySingleton =
            reinterpret_cast<NativeSkySingleton>(
                image + kWrapperFirstCallTargetRva);
        nativeRender = reinterpret_cast<NativePrecipitationRender>(render);
        nativeProjection = reinterpret_cast<NativeProjectionSetup>(projection);
        const auto passPatch = patchPointerCell(
            lightingPassCell,
            expectedLightingPass,
            reinterpret_cast<void*>(&hookLightingPassBuilder));
        if (!passPatch.owned || !passPatch.protectionRestored) {
            (void)MH_RemoveHook(wrapper);
            originalWrapper = nullptr;
            nativeSkySingleton = nullptr;
            nativeRender = nullptr;
            nativeProjection = nullptr;
            passProducerReady.store(false, std::memory_order_release);
            if (readPointerCell(lightingPassCell) !=
                reinterpret_cast<void*>(&hookLightingPassBuilder)) {
                originalLightingPassBuilder = nullptr;
                resolveLightingPassList = nullptr;
                clearPassList = nullptr;
                emplacePass = nullptr;
                lightingPassBuilderCell = nullptr;
                utilityShader = nullptr;
                utilityShaderVtable = nullptr;
                utilityShaderSecondaryVtable = nullptr;
                bsxFlagsVtable = nullptr;
                bsxKey.reset();
            }
            logging::error(
                "Skylighting world-occlusion pass-builder patch failed (owned={}, protectionRestored={}, rolledBack={}); capture remains disabled.",
                passPatch.owned,
                passPatch.protectionRestored,
                passPatch.rolledBack);
            return false;
        }
        status = MH_EnableHook(wrapper);
        DetourIdentity identity{};
        if (status != MH_OK || !captureDetourIdentity(wrapper, identity)) {
            (void)MH_DisableHook(wrapper);
            (void)MH_RemoveHook(wrapper);
            const auto passRollback = patchPointerCell(
                lightingPassCell,
                reinterpret_cast<void*>(&hookLightingPassBuilder),
                expectedLightingPass);
            originalWrapper = nullptr;
            nativeSkySingleton = nullptr;
            nativeRender = nullptr;
            nativeProjection = nullptr;
            passProducerReady.store(false, std::memory_order_release);
            if (passRollback.owned && passRollback.protectionRestored &&
                readPointerCell(lightingPassCell) == expectedLightingPass) {
                originalLightingPassBuilder = nullptr;
                resolveLightingPassList = nullptr;
                clearPassList = nullptr;
                emplacePass = nullptr;
                lightingPassBuilderCell = nullptr;
                utilityShader = nullptr;
                utilityShaderVtable = nullptr;
                utilityShaderSecondaryVtable = nullptr;
                bsxFlagsVtable = nullptr;
                bsxKey.reset();
            }
            logging::error(
                "Skylighting native wrapper detour activation failed: {} ({}); pass-builder rollback owned={}, protectionRestored={}.",
                MH_StatusToString(status),
                static_cast<int>(status),
                passRollback.owned,
                passRollback.protectionRestored);
            return false;
        }

        wrapperTarget = wrapper;
        installedIdentity = identity;
        passProducerReady.store(true, std::memory_order_release);
        installed.store(true, std::memory_order_release);
        Runtime::get().setNativeHookOwned(true);
        logging::info(
            "Installed verified FO4VR Skylighting capture and world-occlusion producer (wrapper RVA 0x00634300, property vtable RVA 0x030A5C18 slot 0x2E, pass-list resolver RVA 0x027A51E0, utility shader RVA 0x0689B4F0)." );
        return true;
    }

    bool validateNativeHooks(const char* trigger) noexcept
    {
        DetourIdentity current{};
        const auto wrapperOwned = installed.load(std::memory_order_acquire) &&
            wrapperTarget && installedIdentity.patch &&
            installedIdentity.destination &&
            captureDetourIdentity(wrapperTarget, current) &&
            current.patch == installedIdentity.patch &&
            current.destination == installedIdentity.destination;
        const auto passBuilderOwned = lightingPassBuilderCell &&
            readPointerCell(lightingPassBuilderCell) ==
                reinterpret_cast<void*>(&hookLightingPassBuilder);
        const auto utilityIdentity = inspectUtilityShader(
            utilityShader,
            utilityShaderVtable,
            utilityShaderSecondaryVtable);
        const auto utilityShaderOwned = utilityIdentity.valid();
        const auto owned = wrapperOwned && passBuilderOwned &&
            utilityShaderOwned;
        passProducerReady.store(owned, std::memory_order_release);
        Runtime::get().setNativeHookOwned(owned);
        if (!owned && installed.load(std::memory_order_acquire)) {
            logging::error(
                "Skylighting native ownership validation failed at '{}' (wrapper={}, passBuilder={}, utilityShader={}); ambient consumption and private capture are disabled.",
                trigger ? trigger : "unknown",
                wrapperOwned,
                passBuilderOwned,
                utilityShaderOwned);
        }
        return owned;
    }
}
