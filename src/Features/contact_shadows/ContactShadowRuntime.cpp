#include "Features/contact_shadows/ContactShadowRuntime.h"

#include "support/Logger.h"

#include "ContactShadowsDFLight.h"

#include <cstring>

namespace community_shaders::contact_shadows
{
    namespace
    {
        constexpr std::size_t kOriginalSize = 26152;
        constexpr std::array<std::byte, 16> kOriginalChecksum{
            std::byte{ 0x12 }, std::byte{ 0x28 }, std::byte{ 0x07 },
            std::byte{ 0x87 }, std::byte{ 0xD2 }, std::byte{ 0xA5 },
            std::byte{ 0x11 }, std::byte{ 0x0C }, std::byte{ 0x82 },
            std::byte{ 0x0F }, std::byte{ 0x43 }, std::byte{ 0x37 },
            std::byte{ 0x51 }, std::byte{ 0xC8 }, std::byte{ 0x46 },
            std::byte{ 0x48 },
        };
        constexpr UINT kConstantSlot = 13;

        struct alignas(16) GpuSettings
        {
            float strength{};
            float maxDistance{};
            float thickness{};
            float sampleCount{};
            float minimumThickness{ 0.35f };
            float selfIntersectionBias{ 0.08f };
            float outerSampleScale{ 0.50f };
            float foveated{};
            float fadeDistance{};
            float reserved0{};
            float reserved1{};
            float reserved2{};
        };
        static_assert(sizeof(GpuSettings) == 48);

        [[nodiscard]] bool matchesOriginal(
            const void* bytecode,
            SIZE_T bytecodeLength) noexcept
        {
            return bytecode && bytecodeLength == kOriginalSize &&
                std::memcmp(bytecode, "DXBC", 4) == 0 &&
                std::memcmp(
                    static_cast<const std::byte*>(bytecode) + 4,
                    kOriginalChecksum.data(),
                    kOriginalChecksum.size()) == 0;
        }
    }

    ScopedConstants::ScopedConstants(
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context), restoreCounter_(restoreCounter)
    {
        if (!context_ || !constants) {
            context_ = nullptr;
            return;
        }
        context_->PSGetConstantBuffers(kConstantSlot, 1, previous_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
    }

    ScopedConstants::~ScopedConstants() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previous = previous_.Get();
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previous);
        if (restoreCounter_) {
            restoreCounter_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        replacement_.Reset();
        constants_.Reset();
        for (auto& original : originals_) {
            original.Reset();
        }
        trackedShaders_.store(0, std::memory_order_relaxed);
        uploadedRevision_ = 0;
        if (!device || !context || !createPixelShader) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ID3D11PixelShader* replacement{};
        const auto shaderResult = createPixelShader(
            device,
            fo4vr_cs_contact_shadows_dflight,
            sizeof(fo4vr_cs_contact_shadows_dflight),
            nullptr,
            &replacement);
        if (FAILED(shaderResult) || !replacement) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows exact DFLight replacement creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(shaderResult));
            return;
        }
        replacement_.Attach(replacement);

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto bufferResult = device->CreateBuffer(
            &description,
            nullptr,
            constants_.ReleaseAndGetAddressOf());
        if (FAILED(bufferResult) || !constants_) {
            replacement_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(bufferResult));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Contact Shadows GPU resources ready; exact directional DFLight identity 12280787d2a5110c820f433751c84648 is armed fail-closed.");
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!shader || !matchesOriginal(bytecode, bytecodeLength)) {
            return;
        }
        matchingShaders_.fetch_add(1, std::memory_order_relaxed);
        for (const auto& original : originals_) {
            if (original.Get() == shader) {
                return;
            }
        }
        for (auto& original : originals_) {
            if (!original) {
                original = shader;
                trackedShaders_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        failures_.fetch_add(1, std::memory_order_relaxed);
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11PixelShader* requested) noexcept
    {
        if (!requested || !featureEnabled() || !replacement_ || !constants_) {
            return { requested, {} };
        }
        for (const auto& original : originals_) {
            if (original.Get() == requested) {
                replacementBinds_.fetch_add(1, std::memory_order_relaxed);
                return { replacement_.Get(), { requested, replacement_.Get() } };
            }
        }
        return { requested, {} };
    }

    void Runtime::uploadSettings(ID3D11DeviceContext* context) noexcept
    {
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (!context || !constants_ || revision == uploadedRevision_) {
            return;
        }
        const GpuSettings data{
            .strength = strength_.load(std::memory_order_relaxed),
            .maxDistance = maxDistance_.load(std::memory_order_relaxed),
            .thickness = thickness_.load(std::memory_order_relaxed),
            .sampleCount = static_cast<float>(
                sampleCount_.load(std::memory_order_relaxed)),
            .foveated = foveated_.load(std::memory_order_relaxed) ? 1.0f : 0.0f,
            .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
        };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
    }

    ScopedConstants Runtime::scopeConstants(
        ID3D11DeviceContext* context,
        ShaderBinding binding) noexcept
    {
        if (!binding || binding.replacement != replacement_.Get() ||
            !featureEnabled() || !constants_) {
            return {};
        }
        uploadSettings(context);
        constantScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedConstants(context, constants_.Get(), &constantRestores_);
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        foveated_.store(safe.foveated, std::memory_order_relaxed);
        strength_.store(safe.strength, std::memory_order_relaxed);
        maxDistance_.store(safe.maxDistance, std::memory_order_relaxed);
        fadeDistance_.store(safe.fadeDistance, std::memory_order_relaxed);
        thickness_.store(safe.thickness, std::memory_order_relaxed);
        sampleCount_.store(safe.sampleCount, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .foveated = foveated_.load(std::memory_order_relaxed),
                .strength = strength_.load(std::memory_order_relaxed),
                .maxDistance = maxDistance_.load(std::memory_order_relaxed),
                .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
                .thickness = thickness_.load(std::memory_order_relaxed),
                .sampleCount = sampleCount_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .matchingShaders = matchingShaders_.load(std::memory_order_relaxed),
            .trackedShaders = trackedShaders_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .constantScopes = constantScopes_.load(std::memory_order_relaxed),
            .constantRestores = constantRestores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
