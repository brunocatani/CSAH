#include "Features/linear_lighting/LinearLightingRuntime.h"

#include "Features/linear_lighting/DFTiledPointLightHook.h"

#include "render/BSLightingGeometryHook.h"
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

        #include "Features/linear_lighting/GeneratedLinearLightingContracts.inl"

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

    ScopedReplacementPixelConstants::ScopedReplacementPixelConstants(
        ID3D11DeviceContext* context,
        ID3D11Buffer* frameBuffer,
        ID3D11Buffer* geometryBuffer,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context),
        restoreCounter_(restoreCounter)
    {
        ID3D11Buffer* previousFrameBuffer{};
        ID3D11Buffer* previousGeometryBuffer{};
        context_->PSGetConstantBuffers(5, 1, &previousFrameBuffer);
        context_->PSGetConstantBuffers(8, 1, &previousGeometryBuffer);
        previousFrameBuffer_.Attach(previousFrameBuffer);
        previousGeometryBuffer_.Attach(previousGeometryBuffer);

        context_->PSSetConstantBuffers(5, 1, &frameBuffer);
        context_->PSSetConstantBuffers(8, 1, &geometryBuffer);
    }

    ScopedReplacementPixelConstants::~ScopedReplacementPixelConstants() noexcept
    {
        if (!context_) {
            return;
        }

        auto* previousFrameBuffer = previousFrameBuffer_.Get();
        auto* previousGeometryBuffer = previousGeometryBuffer_.Get();
        context_->PSSetConstantBuffers(5, 1, &previousFrameBuffer);
        context_->PSSetConstantBuffers(8, 1, &previousGeometryBuffer);
        restoreCounter_->fetch_add(1, std::memory_order_relaxed);
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
        frameDataUploads_.fetch_add(1, std::memory_order_relaxed);
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

        matchingShaderContractMask_.set(
            contractIndex,
            std::memory_order_relaxed);
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

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        shaderSelectionCalls_.fetch_add(1, std::memory_order_relaxed);
        const auto isCapturedContext = context == context_.Get();
        if (!isCapturedContext) {
            rejectedShaderContexts_.fetch_add(1, std::memory_order_relaxed);
            return { requested, 0 };
        }

        if (currentlyRequestedShader_.Get() != requested) {
            currentlyRequestedShader_ = requested;
        }
        applyQueuedSettingsForRenderBoundary();

        if (!requested ||
            !enabled_.load(std::memory_order_acquire) ||
            !gpuResourcesReady_.load(std::memory_order_acquire) ||
            !geometryProviderReady_.load(std::memory_order_acquire)) {
            inactiveShaderSelections_.fetch_add(1, std::memory_order_relaxed);
            return { requested, 0 };
        }

        ID3D11PixelShader* replacement = nullptr;
        std::uint32_t replacementContractPlusOne{};
        for (std::size_t contractIndex = 0;
             contractIndex < originalShaders_.size() && !replacement;
             ++contractIndex) {
            for (const auto& slot : originalShaders_[contractIndex]) {
                if (slot.load(std::memory_order_acquire) == requested) {
                    replacement = replacementShaders_[contractIndex].Get();
                    replacementContractPlusOne =
                        static_cast<std::uint32_t>(contractIndex + 1);
                    break;
                }
            }
        }
        if (!replacement) {
            unmatchedShaderSelections_.fetch_add(1, std::memory_order_relaxed);
            return { requested, 0 };
        }

        auto noContractObserved = 0u;
        firstReplacementContractPlusOne_.compare_exchange_strong(
            noContractObserved,
            replacementContractPlusOne,
            std::memory_order_release,
            std::memory_order_relaxed);
        replacementBinds_.fetch_add(1, std::memory_order_relaxed);
        return { replacement, replacementContractPlusOne };
    }

    ScopedReplacementPixelConstants Runtime::scopeReplacementPixelConstants(
        ID3D11DeviceContext* context,
        std::uint32_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > replacementShaders_.size() || !frameBuffer_ ||
            !geometryBuffer_) {
            return ScopedReplacementPixelConstants{};
        }

        replacementConstantScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedReplacementPixelConstants(
            context,
            frameBuffer_.Get(),
            geometryBuffer_.Get(),
            &replacementConstantRestores_);
    }

    std::uint32_t Runtime::inspectReplacementPipelineState(
        ID3D11DeviceContext* context,
        std::uint32_t contractPlusOne) const noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > replacementShaders_.size()) {
            return 0;
        }

        ID3D11PixelShader* observedShader{};
        ID3D11Buffer* observedFrame{};
        ID3D11Buffer* observedGeometry{};
        context->PSGetShader(&observedShader, nullptr, nullptr);
        context->PSGetConstantBuffers(5, 1, &observedFrame);
        context->PSGetConstantBuffers(8, 1, &observedGeometry);

        std::uint32_t state{};
        if (observedShader == replacementShaders_[contractPlusOne - 1].Get()) {
            state |= PipelineBinding_SelectedReplacement;
        }
        if (observedFrame == frameBuffer_.Get()) {
            state |= PipelineBinding_FrameBuffer;
        }
        if (observedGeometry == geometryBuffer_.Get()) {
            state |= PipelineBinding_GeometryBuffer;
        }

        if (observedGeometry) {
            observedGeometry->Release();
        }
        if (observedFrame) {
            observedFrame->Release();
        }
        if (observedShader) {
            observedShader->Release();
        }
        return state;
    }

    std::uint64_t Runtime::geometryUpdateGeneration() const noexcept
    {
        return geometryUpdates_.load(std::memory_order_acquire);
    }

    const char* Runtime::shaderContractName(
        std::size_t contractIndex) noexcept
    {
        return contractIndex < kShaderContracts.size() ?
            kShaderContracts[contractIndex].name :
            "<invalid>";
    }

    void Runtime::queueSettings(const Settings& settings) noexcept
    {
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            queuedSettings_ = sanitize(settings);
        }
        queuedSettingsRevision_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::applyQueuedSettingsForRenderBoundary() noexcept
    {
        const auto revision =
            queuedSettingsRevision_.load(std::memory_order_acquire);
        if (revision ==
            appliedSettingsRevision_.load(std::memory_order_acquire)) {
            return;
        }

        Settings next{};
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            next = queuedSettings_;
        }

        applySettings(next);
        appliedSettingsRevision_.store(revision, std::memory_order_release);
    }

    bool Runtime::updateGeometryEmissive(float emissiveMultiplier) noexcept
    {
        const auto reject = [this](std::atomic_uint64_t& reason) noexcept {
            reason.fetch_add(1, std::memory_order_relaxed);
            rejectedGeometryUpdates_.fetch_add(1, std::memory_order_relaxed);
            return false;
        };

        if (!std::isfinite(emissiveMultiplier) ||
            emissiveMultiplier < 0.0f ||
            emissiveMultiplier > 1.0e6f) {
            return reject(geometryInvalidSourceRejects_);
        }

        auto* context = context_.Get();
        if (!context ||
            !gpuResourcesReady_.load(std::memory_order_acquire) ||
            !geometryBuffer_) {
            return reject(geometryResourceRejects_);
        }
        if (!enabled_.load(std::memory_order_acquire)) {
            return reject(geometryDisabledRejects_);
        }

        // FO4VR performs geometry setup while a previous or vanilla pixel
        // shader can still be active. Update the private b8 resource here,
        // but bind it only inside a replacement draw so vanilla stereo state
        // is never overwritten between draws.
        GeometryData data{};
        data.emissiveMultiplier = emissiveMultiplier;
        context->UpdateSubresource(geometryBuffer_.Get(), 0, nullptr, &data, 0, 0);
        geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Runtime::setGeometryProviderReady(bool ready) noexcept
    {
        const auto previous =
            geometryProviderReady_.exchange(ready, std::memory_order_acq_rel);
        if (previous == ready) {
            return;
        }
        logging::info(
            "Linear Lighting verified geometry provider is {}.",
            ready ? "ready" : "unavailable");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        settings_ = sanitize(settings);
        enabled_.store(settings_.enabled, std::memory_order_release);
        publishDFTiledPointLightSettings(settings_);
        render::publishDFLightProducerSettings(settings_);
        publishFrameData();
    }

    void Runtime::publishFrameData() noexcept
    {
        if (!context_ || !frameBuffer_) {
            return;
        }
        const auto data = makeFrameData(settings_, true, false, 1.0f);
        context_->UpdateSubresource(frameBuffer_.Get(), 0, nullptr, &data, 0, 0);
        frameDataUploads_.fetch_add(1, std::memory_order_relaxed);
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
            .matchingShaderContractMask =
                matchingShaderContractMask_.load(std::memory_order_relaxed),
            .matchingShadersCreated = matchingShadersCreated_.load(std::memory_order_relaxed),
            .trackedOriginalShaders = trackedOriginalShaders_.load(std::memory_order_relaxed),
            .firstReplacementContractPlusOne =
                firstReplacementContractPlusOne_.load(
                    std::memory_order_acquire),
            .shaderSelectionCalls =
                shaderSelectionCalls_.load(std::memory_order_relaxed),
            .rejectedShaderContexts =
                rejectedShaderContexts_.load(std::memory_order_relaxed),
            .inactiveShaderSelections =
                inactiveShaderSelections_.load(std::memory_order_relaxed),
            .unmatchedShaderSelections =
                unmatchedShaderSelections_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .replacementConstantScopes =
                replacementConstantScopes_.load(std::memory_order_relaxed),
            .replacementConstantRestores =
                replacementConstantRestores_.load(std::memory_order_relaxed),
            .geometryUpdates = geometryUpdates_.load(std::memory_order_relaxed),
            .rejectedGeometryUpdates = rejectedGeometryUpdates_.load(std::memory_order_relaxed),
            .geometryResourceRejects =
                geometryResourceRejects_.load(std::memory_order_relaxed),
            .geometryDisabledRejects =
                geometryDisabledRejects_.load(std::memory_order_relaxed),
            .geometryInvalidSourceRejects =
                geometryInvalidSourceRejects_.load(std::memory_order_relaxed),
            .queuedSettingsRevision =
                queuedSettingsRevision_.load(std::memory_order_acquire),
            .appliedSettingsRevision =
                appliedSettingsRevision_.load(std::memory_order_acquire),
            .frameDataUploads =
                frameDataUploads_.load(std::memory_order_relaxed),
        };
    }
}
