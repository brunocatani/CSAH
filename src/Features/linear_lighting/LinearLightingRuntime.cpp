#include "Features/linear_lighting/LinearLightingRuntime.h"

#include "resources.h"
#include "support/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace community_shaders::linear_lighting
{
    namespace
    {
        struct DxbcIdentity
        {
            std::size_t size{};
            std::array<std::byte, 16> checksum{};
        };

        struct ShaderContractDefinition
        {
            const char* name{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        constexpr std::array<ShaderContractDefinition, 2> kShaderContracts{ {
            {
                "DefaultProjectedFiveMrt_L4_00008002",
                IDR_LINEAR_LIGHTING_DEFAULT_PROJECTED_PS,
                {
                    3052,
                    {
                        std::byte{ 0xDE }, std::byte{ 0x5B }, std::byte{ 0x08 }, std::byte{ 0xB8 },
                        std::byte{ 0xA3 }, std::byte{ 0x77 }, std::byte{ 0x00 }, std::byte{ 0xC6 },
                        std::byte{ 0xF0 }, std::byte{ 0xC1 }, std::byte{ 0x29 }, std::byte{ 0x77 },
                        std::byte{ 0x9F }, std::byte{ 0x1E }, std::byte{ 0x25 }, std::byte{ 0x70 },
                    },
                },
                {
                    6184,
                    {
                        std::byte{ 0xE2 }, std::byte{ 0x5F }, std::byte{ 0x7C }, std::byte{ 0x95 },
                        std::byte{ 0xE6 }, std::byte{ 0x1A }, std::byte{ 0xC1 }, std::byte{ 0xCB },
                        std::byte{ 0x3E }, std::byte{ 0xBF }, std::byte{ 0x80 }, std::byte{ 0x0D },
                        std::byte{ 0x25 }, std::byte{ 0xBB }, std::byte{ 0x05 }, std::byte{ 0x2C },
                    },
                },
            },
            {
                "DefaultProjectedFiveMrt_L3_00008003",
                IDR_LINEAR_LIGHTING_DEFAULT_PROJECTED_VERTEX_COLOR_PS,
                {
                    3120,
                    {
                        std::byte{ 0xD6 }, std::byte{ 0xAB }, std::byte{ 0xD2 }, std::byte{ 0x0B },
                        std::byte{ 0x46 }, std::byte{ 0xCC }, std::byte{ 0x60 }, std::byte{ 0x9D },
                        std::byte{ 0x2B }, std::byte{ 0x64 }, std::byte{ 0xA8 }, std::byte{ 0x8D },
                        std::byte{ 0xF0 }, std::byte{ 0x23 }, std::byte{ 0x28 }, std::byte{ 0x3A },
                    },
                },
                {
                    6252,
                    {
                        std::byte{ 0xE0 }, std::byte{ 0x3E }, std::byte{ 0x51 }, std::byte{ 0x59 },
                        std::byte{ 0x45 }, std::byte{ 0x3A }, std::byte{ 0x9D }, std::byte{ 0x4B },
                        std::byte{ 0x6A }, std::byte{ 0xE5 }, std::byte{ 0x43 }, std::byte{ 0x5C },
                        std::byte{ 0x21 }, std::byte{ 0xE0 }, std::byte{ 0xE8 }, std::byte{ 0xA9 },
                    },
                },
            },
        } };

        struct EmbeddedShader
        {
            const void* data{};
            std::size_t size{};
        };

        [[nodiscard]] EmbeddedShader loadEmbeddedShader(int resourceId) noexcept
        {
            HMODULE module{};
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&loadEmbeddedShader),
                    &module)) {
                return {};
            }

            const auto resource = FindResourceW(
                module,
                MAKEINTRESOURCEW(resourceId),
                RT_RCDATA);
            if (!resource) {
                return {};
            }
            const auto loaded = LoadResource(module, resource);
            if (!loaded) {
                return {};
            }
            const auto size = SizeofResource(module, resource);
            const auto* data = LockResource(loaded);
            return { data, static_cast<std::size_t>(size) };
        }

        [[nodiscard]] bool matchesDxbcIdentity(
            const void* bytecode,
            std::size_t bytecodeLength,
            std::size_t expectedLength,
            const std::array<std::byte, 16>& expectedChecksum) noexcept
        {
            return bytecode && bytecodeLength == expectedLength &&
                bytecodeLength >= 20 &&
                std::memcmp(bytecode, "DXBC", 4) == 0 &&
                std::memcmp(
                    static_cast<const std::byte*>(bytecode) + 4,
                    expectedChecksum.data(),
                    expectedChecksum.size()) == 0;
        }

        [[nodiscard]] D3D11_BUFFER_DESC makeConstantBufferDescription(
            std::uint32_t byteWidth) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = byteWidth;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            return description;
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
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        try {
            if (!device || !context || !createPixelShader) {
                logging::error(
                    "Linear Lighting rejected incomplete D3D11 device capture.");
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
            context->GetDevice(contextDevice.GetAddressOf());
            if (contextDevice.Get() != device) {
                logging::error(
                    "Linear Lighting rejected mismatched D3D11 device/context identity.");
                return;
            }

            device_ = device;
            context_ = context;
            if (!createResources(device, createPixelShader)) {
                device_.Reset();
                context_.Reset();
                return;
            }

            gpuResourcesReady_.store(true, std::memory_order_release);
            logging::info(
                "Linear Lighting GPU resources ready; active shader replacement remains {}.",
                enabled_.load(std::memory_order_relaxed) ? "enabled" : "disabled");
        } catch (const std::exception& error) {
            logging::error(
                "Linear Lighting D3D11 initialization failed: {}",
                error.what());
        } catch (...) {
            logging::error(
                "Linear Lighting D3D11 initialization failed with an unknown exception.");
        }
    }

    bool Runtime::createResources(
        ID3D11Device* device,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        static_assert(kShaderContracts.size() == kShaderContractCount);
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kShaderContracts.size()>
            replacements{};
        for (std::size_t index = 0; index < kShaderContracts.size(); ++index) {
            const auto& contract = kShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!matchesDxbcIdentity(
                    embedded.data,
                    embedded.size,
                    contract.replacement.size,
                    contract.replacement.checksum)) {
                logging::error(
                    "Linear Lighting embedded replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                replacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Linear Lighting replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        const auto safeSettings = sanitize(settings_);
        const auto frameData = makeFrameData(
            safeSettings,
            true,
            false,
            1.0f);
        const GeometryData geometryData{};
        const D3D11_SUBRESOURCE_DATA frameInitial{ &frameData, 0, 0 };
        const D3D11_SUBRESOURCE_DATA geometryInitial{ &geometryData, 0, 0 };

        auto frameDescription = makeConstantBufferDescription(sizeof(FrameData));
        Microsoft::WRL::ComPtr<ID3D11Buffer> frameBuffer;
        auto result = device->CreateBuffer(
            &frameDescription,
            &frameInitial,
            frameBuffer.GetAddressOf());
        if (FAILED(result)) {
            logging::error(
                "Linear Lighting frame-buffer creation failed (HRESULT 0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return false;
        }

        auto geometryDescription = makeConstantBufferDescription(sizeof(GeometryData));
        Microsoft::WRL::ComPtr<ID3D11Buffer> geometryBuffer;
        result = device->CreateBuffer(
            &geometryDescription,
            &geometryInitial,
            geometryBuffer.GetAddressOf());
        if (FAILED(result)) {
            logging::error(
                "Linear Lighting geometry-buffer creation failed (HRESULT 0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return false;
        }

        settings_ = safeSettings;
        replacementShaders_ = std::move(replacements);
        frameBuffer_ = std::move(frameBuffer);
        geometryBuffer_ = std::move(geometryBuffer);
        enabled_.store(settings_.enabled, std::memory_order_release);
        return true;
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!shader) {
            return;
        }

        std::size_t contractIndex = kShaderContracts.size();
        for (std::size_t index = 0; index < kShaderContracts.size(); ++index) {
            const auto& identity = kShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                contractIndex = index;
                break;
            }
        }
        if (contractIndex == kShaderContracts.size()) {
            return;
        }

        matchingShadersCreated_.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(shaderRegistryMutex_);
        auto& owners = originalShaderOwners_[contractIndex];
        auto& slots = originalShaders_[contractIndex];
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index].load(std::memory_order_relaxed) == shader) {
                return;
            }
            if (!owners[index]) {
                owners[index] = shader;
                slots[index].store(shader, std::memory_order_release);
                trackedOriginalShaders_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (!originalCapacityWarningLogged_[contractIndex].exchange(
                true,
                std::memory_order_relaxed)) {
            logging::warn(
                "Linear Lighting original-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                kShaderContracts[contractIndex].name);
        }
    }

    ID3D11PixelShader* Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        if (context == context_.Get() &&
            currentlyRequestedShader_.Get() != requested) {
            currentlyRequestedShader_ = requested;
        }

        if (!requested ||
            !enabled_.load(std::memory_order_acquire) ||
            !gpuResourcesReady_.load(std::memory_order_acquire) ||
            !geometryProviderReady_.load(std::memory_order_acquire) ||
            context != context_.Get()) {
            replacementCurrentlyBound_.store(false, std::memory_order_release);
            return requested;
        }

        ID3D11PixelShader* replacement = nullptr;
        for (std::size_t contractIndex = 0;
             contractIndex < originalShaders_.size() && !replacement;
             ++contractIndex) {
            for (const auto& slot : originalShaders_[contractIndex]) {
                if (slot.load(std::memory_order_acquire) == requested) {
                    replacement = replacementShaders_[contractIndex].Get();
                    break;
                }
            }
        }
        if (!replacement) {
            replacementCurrentlyBound_.store(false, std::memory_order_release);
            return requested;
        }

        ID3D11Buffer* frame = frameBuffer_.Get();
        ID3D11Buffer* geometry = geometryBuffer_.Get();
        context->PSSetConstantBuffers(5, 1, &frame);
        context->PSSetConstantBuffers(8, 1, &geometry);
        replacementCurrentlyBound_.store(true, std::memory_order_release);
        replacementBinds_.fetch_add(1, std::memory_order_relaxed);
        return replacement;
    }

    void Runtime::queueSettings(const Settings& settings) noexcept
    {
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            queuedSettings_ = sanitize(settings);
        }
        queuedSettingsRevision_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::applyQueuedSettingsForGeometryDraw() noexcept
    {
        const auto revision =
            queuedSettingsRevision_.load(std::memory_order_acquire);
        if (revision == appliedSettingsRevision_) {
            return;
        }

        Settings next{};
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            next = queuedSettings_;
        }

        const auto wasEnabled = enabled_.load(std::memory_order_acquire);
        applySettings(next);
        appliedSettingsRevision_ = revision;

        // If the feature changed state while the engine retained the same
        // pixel-shader binding, explicitly replay the engine-requested shader.
        // The hooked vtable then chooses vanilla or replacement under the new
        // state without retaining a borrowed shader pointer.
        if (wasEnabled != next.enabled && context_ && currentlyRequestedShader_) {
            context_->PSSetShader(currentlyRequestedShader_.Get(), nullptr, 0);
        }
    }

    bool Runtime::updateGeometryEmissive(float emissiveMultiplier) noexcept
    {
        auto* context = context_.Get();
        if (!context ||
            !gpuResourcesReady_.load(std::memory_order_acquire) ||
            !enabled_.load(std::memory_order_acquire) ||
            !replacementCurrentlyBound_.load(std::memory_order_acquire) ||
            !geometryBuffer_ ||
            !std::isfinite(emissiveMultiplier) ||
            emissiveMultiplier < 0.0f ||
            emissiveMultiplier > 1.0e6f) {
            rejectedGeometryUpdates_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        GeometryData data{};
        data.emissiveMultiplier = emissiveMultiplier;
        context->UpdateSubresource(geometryBuffer_.Get(), 0, nullptr, &data, 0, 0);
        ID3D11Buffer* geometry = geometryBuffer_.Get();
        context->PSSetConstantBuffers(8, 1, &geometry);
        geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Runtime::setGeometryProviderReady(bool ready) noexcept
    {
        geometryProviderReady_.store(ready, std::memory_order_release);
        logging::info(
            "Linear Lighting verified geometry provider is {}.",
            ready ? "ready" : "unavailable");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        settings_ = sanitize(settings);
        enabled_.store(settings_.enabled, std::memory_order_release);
        if (!settings_.enabled) {
            replacementCurrentlyBound_.store(false, std::memory_order_release);
        }
        publishFrameData();
    }

    void Runtime::publishFrameData() noexcept
    {
        if (!context_ || !frameBuffer_) {
            return;
        }
        const auto data = makeFrameData(settings_, true, false, 1.0f);
        context_->UpdateSubresource(frameBuffer_.Get(), 0, nullptr, &data, 0, 0);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .enabled = enabled_.load(std::memory_order_acquire),
            .gpuResourcesReady = gpuResourcesReady_.load(std::memory_order_acquire),
            .geometryProviderReady =
                geometryProviderReady_.load(std::memory_order_acquire),
            .verifiedShaderContracts =
                static_cast<std::uint32_t>(kShaderContracts.size()),
            .matchingShadersCreated = matchingShadersCreated_.load(std::memory_order_relaxed),
            .trackedOriginalShaders = trackedOriginalShaders_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .geometryUpdates = geometryUpdates_.load(std::memory_order_relaxed),
            .rejectedGeometryUpdates = rejectedGeometryUpdates_.load(std::memory_order_relaxed),
        };
    }
}
