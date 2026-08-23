#include "Features/vanilla_fixes/VanillaShaderFixes.h"

#include "Features/vanilla_fixes/DirectionalLightPitchPatch.h"
#include "Features/vanilla_fixes/ReflectionCompositePatch.h"
#include "Features/vanilla_fixes/SslrEnvironmentBinding.h"

#include "support/Logger.h"

#include "VanillaFixesSaoBlurHCS.h"
#include "VanillaFixesSaoRawAOCS.h"
#include "VanillaFixesSslrBlurHVS.h"
#include "VanillaFixesSslrPrepassPS.h"
#include "VanillaFixesSslrRaytracePS.h"

#include <atomic>
#include <cstring>
#include <ranges>
#include <span>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        struct CompleteIdentity final
        {
            std::size_t size{};
            std::uint64_t hash{};
            std::array<std::uint32_t, 4> checksum{};
        };

        constexpr CompleteIdentity kSaoRawAo{
            3892,
            0x4A8B8CB64AC24499ull,
            { 0xE73D9A26u, 0x0C9AFE98u, 0xF628F975u, 0x0E5F8AEFu },
        };
        constexpr CompleteIdentity kSaoHorizontalBlur{
            2324,
            0x5186B7FAB41E51CEull,
            { 0x41398F0Au, 0x77BA0310u, 0xAF299CBCu, 0xE9D4C63Cu },
        };
        constexpr CompleteIdentity kSslrHorizontalBlur{
            800,
            0x45D4CB8E6B37F5E7ull,
            { 0xA6CDD478u, 0xADC1E0F8u, 0xF45AC193u, 0xC6BAEB00u },
        };
        constexpr CompleteIdentity kSslrPrepass{
            3080,
            0xBB9FDCC3817DC31Cull,
            { 0xACB92B5Fu, 0xCC72D1AFu, 0x2E08AF26u, 0x715E3A04u },
        };
        constexpr CompleteIdentity kSslrRaytrace{
            68624,
            0xE4DB1ED97A719E55ull,
            { 0x6DF2B232u, 0x8530B38Cu, 0x3A519509u, 0xAEEE0713u },
        };
        constexpr CompleteIdentity kFocusShadow{
            19504,
            0xF4EBA56324D74051ull,
            { 0x6C19C7D4u, 0xFF8FE5A6u, 0x94E7B9E2u, 0xC02418F2u },
        };
        constexpr CompleteIdentity kDirectionalLightRadialFade{
            9388,
            0x2BF99E4AE72E5F31ull,
            { 0xEE23B97Eu, 0x314D542Du, 0xE122666Du, 0xC065151Fu },
        };
        constexpr std::array<CompleteIdentity, 4> kReflectionComposite{
            CompleteIdentity{ 9348, 0x59FAED17411F08C7ull,
                { 0xAFC6ED93u, 0xD2B2CB41u, 0x992E9690u, 0x25CE4F5Au } },
            CompleteIdentity{ 9564, 0x0903D20AD75EBB0Eull,
                { 0xBA70684Du, 0xE798547Du, 0x5F19B329u, 0x78F9CD7Du } },
            CompleteIdentity{ 11100, 0x81247323F5EF60D3ull,
                { 0x919483EBu, 0x92EE08ABu, 0x905948FCu, 0xC5BE5B94u } },
            CompleteIdentity{ 11316, 0x29D9E8483ACB1988ull,
                { 0x7BDA24FCu, 0x6003ADBDu, 0x9EFC21E2u, 0x9F2EAD45u } },
        };

        std::atomic_uint64_t targeted{};
        std::atomic_uint64_t accepted{};
        std::atomic_uint64_t stockFallbacks{};
        std::atomic_uint64_t focusShadersCreated{};
        std::atomic_bool directionalLightResultLogged{};

        struct SslrPixelShaderPair final
        {
            std::atomic<ID3D11PixelShader*> key{};
            ID3D11PixelShader* fixed{};
            ID3D11PixelShader* stock{};
            ShaderFix fix{ ShaderFix::none };
        };

        constexpr std::size_t kSslrPixelShaderPairCapacity = 16;
        std::array<SslrPixelShaderPair, kSslrPixelShaderPairCapacity>
            sslrPixelShaderPairs{};

        struct DirectionalLightPixelShaderPair final
        {
            std::atomic<ID3D11PixelShader*> key{};
            ID3D11PixelShader* fixed{};
            ID3D11PixelShader* stock{};
        };

        constexpr std::size_t kDirectionalLightPixelShaderPairCapacity = 8;
        std::array<DirectionalLightPixelShaderPair,
            kDirectionalLightPixelShaderPairCapacity>
            directionalLightPixelShaderPairs{};
        std::atomic_bool directionalLightPitchFixRequested{ true };

        [[nodiscard]] bool matches(
            const ShaderIdentity& identity,
            const CompleteIdentity& expected) noexcept
        {
            return identity.hasDxbcHeader &&
                identity.bytecodeSize == expected.size &&
                identity.hash == expected.hash &&
                identity.checksum == expected.checksum;
        }
    }

    ShaderIdentity identifyShader(
        const void* bytecode,
        const std::size_t bytecodeLength) noexcept
    {
        ShaderIdentity identity{ .bytecodeSize = bytecodeLength };
        if (bytecode && bytecodeLength) {
            identity.hash = kFnvOffset;
            const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
            for (std::size_t index = 0; index < bytecodeLength; ++index) {
                identity.hash ^= bytes[index];
                identity.hash *= kFnvPrime;
            }
        }
        constexpr std::array<char, 4> signature{ 'D', 'X', 'B', 'C' };
        if (!bytecode || bytecodeLength < 20 ||
            std::memcmp(bytecode, signature.data(), signature.size()) != 0) {
            return identity;
        }
        std::memcpy(
            identity.checksum.data(),
            static_cast<const std::byte*>(bytecode) + 4,
            16);
        identity.hasDxbcHeader = true;
        return identity;
    }

    bool isStockFocusShadowPixel(const ShaderIdentity& identity) noexcept
    {
        return matches(identity, kFocusShadow);
    }

    ShaderSelection selectVertexShader(
        const void* bytecode,
        const std::size_t bytecodeLength) noexcept
    {
        if (matches(
                identifyShader(bytecode, bytecodeLength),
                kSslrHorizontalBlur)) {
            return {
                fo4vr_cs_vanilla_sslr_blur_h_vs,
                sizeof(fo4vr_cs_vanilla_sslr_blur_h_vs),
                ShaderFix::sslrHorizontalBlur,
                true,
            };
        }
        return { bytecode, bytecodeLength };
    }

    ShaderSelection selectPixelShader(
        const void* bytecode,
        const std::size_t bytecodeLength,
        std::vector<std::byte>& patchStorage) noexcept
    {
        patchStorage.clear();
        const auto identity = identifyShader(bytecode, bytecodeLength);
        if (matches(identity, kSslrPrepass)) {
            return {
                fo4vr_cs_vanilla_sslr_prepass_ps,
                sizeof(fo4vr_cs_vanilla_sslr_prepass_ps),
                ShaderFix::sslrPrepass,
                true,
            };
        }
        if (matches(identity, kSslrRaytrace)) {
            return {
                fo4vr_cs_vanilla_sslr_raytrace_ps,
                sizeof(fo4vr_cs_vanilla_sslr_raytrace_ps),
                ShaderFix::sslrRaytrace,
                true,
            };
        }
        if (matches(identity, kDirectionalLightRadialFade)) {
            const auto stock = std::span<const std::byte>{
                static_cast<const std::byte*>(bytecode),
                bytecodeLength,
            };
            const auto ready = patchStockDirectionalLightRadialFade(
                stock,
                patchStorage);
            return {
                ready ? patchStorage.data() : bytecode,
                ready ? patchStorage.size() : bytecodeLength,
                ShaderFix::directionalLightRadialFade,
                ready,
            };
        }
        for (const auto& candidate : kReflectionComposite) {
            if (!matches(identity, candidate)) {
                continue;
            }
            const auto stock = std::span<const std::byte>{
                static_cast<const std::byte*>(bytecode),
                bytecodeLength,
            };
            const auto ready =
                patchStockReflectionCompositeSurfaceAnchoredCubemap(
                    stock,
                    patchStorage);
            return {
                ready ? patchStorage.data() : bytecode,
                ready ? patchStorage.size() : bytecodeLength,
                ShaderFix::surfaceAnchoredCubemap,
                ready,
            };
        }
        return { bytecode, bytecodeLength };
    }

    ShaderSelection selectComputeShader(
        const void* bytecode,
        const std::size_t bytecodeLength) noexcept
    {
        const auto identity = identifyShader(bytecode, bytecodeLength);
        if (matches(identity, kSaoRawAo)) {
            return {
                fo4vr_cs_vanilla_sao_raw_ao_cs,
                sizeof(fo4vr_cs_vanilla_sao_raw_ao_cs),
                ShaderFix::saoRawAo,
                true,
            };
        }
        if (matches(identity, kSaoHorizontalBlur)) {
            return {
                fo4vr_cs_vanilla_sao_blur_h_cs,
                sizeof(fo4vr_cs_vanilla_sao_blur_h_cs),
                ShaderFix::saoHorizontalBlur,
                true,
            };
        }
        return { bytecode, bytecodeLength };
    }

    bool publishSslrPixelShaderPair(
        ID3D11PixelShader* fixedShader,
        ID3D11PixelShader* stockShader,
        const ShaderFix fix) noexcept
    {
        if (!fixedShader || !stockShader ||
            (fix != ShaderFix::sslrPrepass &&
                fix != ShaderFix::sslrRaytrace)) {
            return false;
        }
        auto* const busy = reinterpret_cast<ID3D11PixelShader*>(
            std::uintptr_t{ 1 });
        for (auto& pair : sslrPixelShaderPairs) {
            auto* expected = static_cast<ID3D11PixelShader*>(nullptr);
            if (!pair.key.compare_exchange_strong(
                    expected,
                    busy,
                    std::memory_order_acq_rel)) {
                if (expected == fixedShader) {
                    return false;
                }
                continue;
            }
            fixedShader->AddRef();
            stockShader->AddRef();
            pair.fixed = fixedShader;
            pair.stock = stockShader;
            pair.fix = fix;
            pair.key.store(fixedShader, std::memory_order_release);
            return true;
        }
        return false;
    }

    ID3D11PixelShader* selectSslrPixelShaderForBinding(
        ID3D11PixelShader* engineShader) noexcept
    {
        if (!engineShader) {
            return nullptr;
        }
        const auto useFixed = sslrSuiteReady();
        for (const auto& pair : sslrPixelShaderPairs) {
            if (pair.key.load(std::memory_order_acquire) == engineShader) {
                return useFixed ? pair.fixed : pair.stock;
            }
        }
        return engineShader;
    }

    ID3D11PixelShader* retainedStockSslrPixelShader(
        ID3D11PixelShader* correctedShader) noexcept
    {
        if (!correctedShader) {
            return nullptr;
        }
        for (const auto& pair : sslrPixelShaderPairs) {
            const auto* const published = pair.key.load(
                std::memory_order_acquire);
            if (published == correctedShader) {
                return pair.stock;
            }
        }
        return nullptr;
    }

    bool isSslrRaytracePixelShader(ID3D11PixelShader* shader) noexcept
    {
        if (!shader) {
            return false;
        }
        return std::ranges::any_of(
            sslrPixelShaderPairs,
            [shader](const SslrPixelShaderPair& pair) noexcept {
                const auto* key = pair.key.load(
                    std::memory_order_acquire);
                return key == shader &&
                    pair.fix == ShaderFix::sslrRaytrace &&
                    pair.fixed == shader;
            });
    }

    bool publishDirectionalLightPixelShaderPair(
        ID3D11PixelShader* fixedShader,
        ID3D11PixelShader* stockShader,
        const ShaderFix fix) noexcept
    {
        if (!fixedShader || !stockShader ||
            fix != ShaderFix::directionalLightRadialFade) {
            return false;
        }
        auto* const busy = reinterpret_cast<ID3D11PixelShader*>(
            std::uintptr_t{ 1 });
        for (auto& pair : directionalLightPixelShaderPairs) {
            auto* expected = static_cast<ID3D11PixelShader*>(nullptr);
            if (!pair.key.compare_exchange_strong(
                    expected,
                    busy,
                    std::memory_order_acq_rel)) {
                if (expected == fixedShader) {
                    return false;
                }
                continue;
            }
            fixedShader->AddRef();
            stockShader->AddRef();
            pair.fixed = fixedShader;
            pair.stock = stockShader;
            pair.key.store(fixedShader, std::memory_order_release);
            return true;
        }
        return false;
    }

    ID3D11PixelShader* selectDirectionalLightPixelShaderForBinding(
        ID3D11PixelShader* engineShader) noexcept
    {
        if (!engineShader) {
            return nullptr;
        }
        const auto useFixed = directionalLightPitchFixRequested.load(
            std::memory_order_acquire);
        for (const auto& pair : directionalLightPixelShaderPairs) {
            if (pair.key.load(std::memory_order_acquire) == engineShader) {
                return useFixed ? pair.fixed : pair.stock;
            }
        }
        return engineShader;
    }

    void setDirectionalLightPitchFixRequested(const bool requested) noexcept
    {
        directionalLightPitchFixRequested.store(
            requested,
            std::memory_order_release);
    }

    void reportShaderCreationResult(
        const ShaderSelection& selection,
        const bool wasAccepted) noexcept
    {
        if (!selection.targeted()) {
            return;
        }
        targeted.fetch_add(1, std::memory_order_relaxed);
        (wasAccepted ? accepted : stockFallbacks)
            .fetch_add(1, std::memory_order_relaxed);
        if (selection.fix == ShaderFix::directionalLightRadialFade &&
            !directionalLightResultLogged.exchange(
                true,
                std::memory_order_relaxed)) {
            if (wasAccepted) {
                logging::info(
                    "Vanilla Fixes accepted the exact 9,388-byte peripheral-radial-fade diagnostic and retained its stock pair; the live Vanilla Fixes master toggle owns bind selection.");
            } else {
                logging::error(
                    "Vanilla Fixes rejected the peripheral-radial-fade diagnostic; the exact stock directional-light shader remains active.");
            }
        }
    }

    void reportFocusShaderCreated() noexcept
    {
        focusShadersCreated.fetch_add(1, std::memory_order_relaxed);
    }

    ShaderFixSnapshot shaderFixSnapshot() noexcept
    {
        return {
            .targeted = targeted.load(std::memory_order_relaxed),
            .accepted = accepted.load(std::memory_order_relaxed),
            .stockFallbacks = stockFallbacks.load(std::memory_order_relaxed),
            .focusShadersCreated =
                focusShadersCreated.load(std::memory_order_relaxed),
        };
    }
}
