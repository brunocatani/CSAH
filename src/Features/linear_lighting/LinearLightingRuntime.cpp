#include "Features/linear_lighting/LinearLightingRuntime.h"

#include "Features/linear_lighting/DFLightAmbientShaderPatch.h"
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

        struct DFLightAmbientContractDefinition
        {
            const char* name{};
            DxbcIdentity original{};
            DFLightAmbientGammaOffsets gammaOffsets{};
        };

        struct SkyShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct DistantTreeShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct ParticleShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct DFLightAmbientDescriptorContract
        {
            std::uint32_t descriptor{};
            std::uint32_t contractIndex{};
        };

        #include "Features/linear_lighting/GeneratedLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedSkyLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedDistantTreeLinearLightingContract.inl"
        #include "Features/linear_lighting/GeneratedParticleLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedDFLightAmbientContracts.inl"

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

        [[nodiscard]] constexpr std::uint64_t encodeShaderBinding(
            ReplacementShaderBinding binding) noexcept
        {
            return static_cast<std::uint64_t>(binding.family) |
                (static_cast<std::uint64_t>(binding.constantFlags) << 8) |
                (static_cast<std::uint64_t>(binding.contractPlusOne) << 16);
        }

        [[nodiscard]] constexpr ReplacementShaderBinding decodeShaderBinding(
            std::uint64_t encoded) noexcept
        {
            return {
                static_cast<ReplacementShaderFamily>(encoded & 0xFFu),
                static_cast<std::uint32_t>(encoded >> 16),
                static_cast<std::uint8_t>((encoded >> 8) & 0xFFu),
            };
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
        std::uint8_t constantFlags,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context),
        constantFlags_(constantFlags),
        restoreCounter_(restoreCounter)
    {
        if ((constantFlags_ & ReplacementPixelConstants_Frame) != 0) {
            ID3D11Buffer* previousFrameBuffer{};
            context_->PSGetConstantBuffers(5, 1, &previousFrameBuffer);
            previousFrameBuffer_.Attach(previousFrameBuffer);
            context_->PSSetConstantBuffers(5, 1, &frameBuffer);
        }
        if ((constantFlags_ & ReplacementPixelConstants_Geometry) != 0) {
            ID3D11Buffer* previousGeometryBuffer{};
            context_->PSGetConstantBuffers(8, 1, &previousGeometryBuffer);
            previousGeometryBuffer_.Attach(previousGeometryBuffer);
            context_->PSSetConstantBuffers(8, 1, &geometryBuffer);
        }
    }

    ScopedReplacementPixelConstants::~ScopedReplacementPixelConstants() noexcept
    {
        if (!context_) {
            return;
        }

        if ((constantFlags_ & ReplacementPixelConstants_Frame) != 0) {
            auto* previousFrameBuffer = previousFrameBuffer_.Get();
            context_->PSSetConstantBuffers(5, 1, &previousFrameBuffer);
        }
        if ((constantFlags_ & ReplacementPixelConstants_Geometry) != 0) {
            auto* previousGeometryBuffer = previousGeometryBuffer_.Get();
            context_->PSSetConstantBuffers(8, 1, &previousGeometryBuffer);
        }
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
            createPixelShader_ = createPixelShader;
            if (!createResources(device, createPixelShader)) {
                createPixelShader_ = nullptr;
                device_.Reset();
                context_.Reset();
                return;
            }

            gpuResourcesReady_.store(true, std::memory_order_release);
            logging::info(
                "Linear Lighting GPU resources ready (materialContracts={}, skyContracts={}, distantTreeContracts={}, particleContracts={}); active shader replacement remains {}.",
                kShaderContracts.size(),
                kSkyShaderContracts.size(),
                kDistantTreeShaderContractCount,
                kParticleShaderContracts.size(),
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
        static_assert(kSkyShaderContracts.size() == kSkyShaderContractCount);
        static_assert(kDistantTreeShaderContractCount == 1);
        static_assert(
            kParticleShaderContracts.size() == kParticleShaderContractCount);
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

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSkyShaderContracts.size()>
            skyReplacements{};
        for (std::size_t index = 0;
             index < kSkyShaderContracts.size();
             ++index) {
            const auto& contract = kSkyShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!matchesDxbcIdentity(
                    embedded.data,
                    embedded.size,
                    contract.replacement.size,
                    contract.replacement.checksum)) {
                logging::error(
                    "Linear Lighting embedded Sky replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                skyReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Linear Lighting Sky replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDistantTreeShaderContractCount>
            distantTreeReplacements{};
        const auto distantTreeEmbedded =
            loadEmbeddedShader(kDistantTreeShaderContract.resourceId);
        if (!matchesDxbcIdentity(
                distantTreeEmbedded.data,
                distantTreeEmbedded.size,
                kDistantTreeShaderContract.replacement.size,
                kDistantTreeShaderContract.replacement.checksum)) {
            logging::error(
                "Linear Lighting embedded DistantTree replacement '{}' is missing or invalid.",
                kDistantTreeShaderContract.name);
            return false;
        }
        const auto distantTreeResult = createPixelShader(
            device,
            distantTreeEmbedded.data,
            distantTreeEmbedded.size,
            nullptr,
            distantTreeReplacements[0].GetAddressOf());
        if (FAILED(distantTreeResult)) {
            logging::error(
                "Linear Lighting DistantTree replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                kDistantTreeShaderContract.name,
                static_cast<std::uint32_t>(distantTreeResult));
            return false;
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kParticleShaderContracts.size()>
            particleReplacements{};
        for (std::size_t index = 0;
             index < kParticleShaderContracts.size();
             ++index) {
            const auto& contract = kParticleShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!matchesDxbcIdentity(
                    embedded.data,
                    embedded.size,
                    contract.replacement.size,
                    contract.replacement.checksum)) {
                logging::error(
                    "Linear Lighting embedded Particle replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto particleResult = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                particleReplacements[index].GetAddressOf());
            if (FAILED(particleResult)) {
                logging::error(
                    "Linear Lighting Particle replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(particleResult));
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
        skyReplacementShaders_ = std::move(skyReplacements);
        distantTreeReplacementShaders_ =
            std::move(distantTreeReplacements);
        particleReplacementShaders_ = std::move(particleReplacements);
        frameBuffer_ = std::move(frameBuffer);
        geometryBuffer_ = std::move(geometryBuffer);
        enabled_.store(settings_.enabled, std::memory_order_release);
        frameDataUploads_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::createDFLightAmbientReplacement(
        std::span<const std::byte> originalBytecode,
        std::size_t contractIndex,
        float ambientGamma,
        Microsoft::WRL::ComPtr<ID3D11PixelShader>& replacement) noexcept
    {
        if (contractIndex >= kDFLightAmbientContracts.size() ||
            !device_ || !createPixelShader_ || originalBytecode.empty()) {
            return false;
        }

        const auto& contract = kDFLightAmbientContracts[contractIndex];
        if (!matchesDxbcIdentity(
                originalBytecode.data(),
                originalBytecode.size(),
                contract.original.size,
                contract.original.checksum)) {
            return false;
        }

        try {
            std::vector<std::byte> patched(
                originalBytecode.begin(), originalBytecode.end());
            if (!patchDFLightAmbientGamma(
                    patched,
                    contract.gammaOffsets,
                    ambientGamma)) {
                logging::error(
                    "DFLight ambient replacement '{}' failed its six-offset gamma identity gate.",
                    contract.name);
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D11PixelShader> candidate;
            const auto result = createPixelShader_(
                device_.Get(),
                patched.data(),
                patched.size(),
                nullptr,
                candidate.GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "DFLight ambient replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
            replacement = std::move(candidate);
            dFLightAmbientReplacementBuilds_.fetch_add(
                1, std::memory_order_relaxed);
            return true;
        } catch (const std::exception& error) {
            logging::error(
                "DFLight ambient replacement '{}' failed while copying bytecode: {}",
                contract.name,
                error.what());
        } catch (...) {
            logging::error(
                "DFLight ambient replacement '{}' failed while copying bytecode with an unknown exception.",
                contract.name);
        }
        return false;
    }

    bool Runtime::rebuildDFLightAmbientReplacements(
        float ambientGamma) noexcept
    {
        std::scoped_lock lock(shaderRegistryMutex_);
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDFLightAmbientShaderContractCount>
            replacements{};
        std::uint64_t rebuiltMask{};
        for (std::size_t index = 0;
             index < dFLightAmbientOriginalBytecode_.size();
             ++index) {
            const auto& original = dFLightAmbientOriginalBytecode_[index];
            if (original.empty()) {
                continue;
            }
            if (!createDFLightAmbientReplacement(
                    original,
                    index,
                    ambientGamma,
                    replacements[index])) {
                dFLightAmbientReplacementFailures_.fetch_add(
                    1, std::memory_order_relaxed);
                return false;
            }
            rebuiltMask |= 1ull << index;
        }

        for (std::size_t index = 0; index < replacements.size(); ++index) {
            if (replacements[index]) {
                dFLightAmbientReplacementShaders_[index] =
                    std::move(replacements[index]);
            }
        }
        dFLightAmbientGammaBits_.store(
            std::bit_cast<std::uint32_t>(ambientGamma),
            std::memory_order_release);
        readyDFLightAmbientContractMask_.fetch_or(
            rebuiltMask, std::memory_order_release);
        dFLightAmbientGammaRebuilds_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::registerShaderBinding(
        ID3D11PixelShader* shader,
        ReplacementShaderBinding binding) noexcept
    {
        if (shaderBindingLookup_.insert(shader, encodeShaderBinding(binding))) {
            return true;
        }

        shaderBindingLookupFailures_.fetch_add(1, std::memory_order_relaxed);
        if (!shaderBindingLookupCapacityWarningLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::error(
                "Linear Lighting fixed shader-binding lookup rejected an original shader; the affected instance remains vanilla.");
        }
        return false;
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
        if (contractIndex != kShaderContracts.size()) {
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
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::material,
                                static_cast<std::uint32_t>(contractIndex + 1),
                                ReplacementPixelConstants_Frame |
                                    ReplacementPixelConstants_Geometry,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalShaders_.fetch_add(
                        1, std::memory_order_relaxed);
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
            return;
        }

        std::size_t skyContractIndex = kSkyShaderContracts.size();
        for (std::size_t index = 0;
             index < kSkyShaderContracts.size();
             ++index) {
            const auto& identity = kSkyShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                skyContractIndex = index;
                break;
            }
        }
        if (skyContractIndex != kSkyShaderContracts.size()) {
            matchingSkyShaderContractMask_.fetch_or(
                static_cast<std::uint16_t>(1u << skyContractIndex),
                std::memory_order_relaxed);
            matchingSkyShadersCreated_.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalSkyShaderOwners_[skyContractIndex];
            auto& slots = originalSkyShaders_[skyContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::sky,
                                static_cast<std::uint32_t>(skyContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalSkyShaders_.fetch_add(
                        1, std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalSkyCapacityWarningLogged_[skyContractIndex].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Sky-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kSkyShaderContracts[skyContractIndex].name);
            }
            return;
        }

        if (matchesDxbcIdentity(
                bytecode,
                bytecodeLength,
                kDistantTreeShaderContract.original.size,
                kDistantTreeShaderContract.original.checksum)) {
            matchingDistantTreeShaderContractMask_.fetch_or(
                1u,
                std::memory_order_relaxed);
            matchingDistantTreeShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalDistantTreeShaderOwners_[0];
            auto& slots = originalDistantTreeShaders_[0];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::distantTree,
                                1u,
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalDistantTreeShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalDistantTreeCapacityWarningLogged_[0].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original DistantTree-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kDistantTreeShaderContract.name);
            }
            return;
        }

        std::size_t particleContractIndex = kParticleShaderContracts.size();
        for (std::size_t index = 0;
             index < kParticleShaderContracts.size();
             ++index) {
            const auto& identity = kParticleShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                particleContractIndex = index;
                break;
            }
        }
        if (particleContractIndex != kParticleShaderContracts.size()) {
            matchingParticleShaderContractMask_.fetch_or(
                static_cast<std::uint8_t>(1u << particleContractIndex),
                std::memory_order_relaxed);
            matchingParticleShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalParticleShaderOwners_[particleContractIndex];
            auto& slots = originalParticleShaders_[particleContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::particle,
                                static_cast<std::uint32_t>(
                                    particleContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalParticleShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalParticleCapacityWarningLogged_[particleContractIndex]
                     .exchange(true, std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Particle-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kParticleShaderContracts[particleContractIndex].name);
            }
            return;
        }

        std::size_t ambientContractIndex = kDFLightAmbientContracts.size();
        for (std::size_t index = 0;
             index < kDFLightAmbientContracts.size();
             ++index) {
            const auto& identity = kDFLightAmbientContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                ambientContractIndex = index;
                break;
            }
        }
        if (ambientContractIndex == kDFLightAmbientContracts.size()) {
            return;
        }

        matchingDFLightAmbientContractMask_.fetch_or(
            1ull << ambientContractIndex,
            std::memory_order_relaxed);
        matchingDFLightAmbientShaders_.fetch_add(
            1, std::memory_order_relaxed);
        std::scoped_lock lock(shaderRegistryMutex_);
        auto& owners =
            dFLightAmbientOriginalShaderOwners_[ambientContractIndex];
        auto& slots = dFLightAmbientOriginalShaders_[ambientContractIndex];
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index].load(std::memory_order_relaxed) == shader) {
                return;
            }
            if (!owners[index]) {
                if (dFLightAmbientOriginalBytecode_[ambientContractIndex].empty()) {
                    const auto* begin = static_cast<const std::byte*>(bytecode);
                    try {
                        dFLightAmbientOriginalBytecode_[ambientContractIndex]
                            .assign(begin, begin + bytecodeLength);
                    } catch (const std::exception& error) {
                        logging::error(
                            "DFLight ambient bytecode capture '{}' failed: {}",
                            kDFLightAmbientContracts[ambientContractIndex].name,
                            error.what());
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        return;
                    } catch (...) {
                        logging::error(
                            "DFLight ambient bytecode capture '{}' failed with an unknown exception.",
                            kDFLightAmbientContracts[ambientContractIndex].name);
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        return;
                    }
                }
                if (!dFLightAmbientReplacementShaders_[ambientContractIndex]) {
                    if (!createDFLightAmbientReplacement(
                            dFLightAmbientOriginalBytecode_[ambientContractIndex],
                            ambientContractIndex,
                            std::bit_cast<float>(
                                dFLightAmbientGammaBits_.load(
                                    std::memory_order_acquire)),
                            dFLightAmbientReplacementShaders_[ambientContractIndex])) {
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                    } else {
                        readyDFLightAmbientContractMask_.fetch_or(
                            1ull << ambientContractIndex,
                            std::memory_order_release);
                    }
                }
                owners[index] = shader;
                if (!registerShaderBinding(
                        shader,
                        {
                            ReplacementShaderFamily::dFLightAmbient,
                            static_cast<std::uint32_t>(
                                ambientContractIndex + 1),
                            ReplacementPixelConstants_None,
                        })) {
                    owners[index].Reset();
                    return;
                }
                slots[index].store(shader, std::memory_order_release);
                trackedDFLightAmbientShaders_.fetch_add(
                    1, std::memory_order_relaxed);
                return;
            }
        }
        if (!dFLightAmbientCapacityWarningLogged_[ambientContractIndex].exchange(
                true,
                std::memory_order_relaxed)) {
            logging::warn(
                "DFLight ambient original-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                kDFLightAmbientContracts[ambientContractIndex].name);
        }
    }

    PixelShaderSelection Runtime::selectDFLightAmbientShader(
        ID3D11PixelShader* requested,
        std::size_t contractIndex) noexcept
    {
        if (contractIndex >= dFLightAmbientReplacementShaders_.size()) {
            return { requested, {}, false };
        }

        std::scoped_lock lock(shaderRegistryMutex_);
        auto* replacement =
            dFLightAmbientReplacementShaders_[contractIndex].Get();
        if (!replacement) {
            return { requested, {}, false };
        }
        replacement->AddRef();
        dFLightAmbientReplacementBinds_.fetch_add(
            1, std::memory_order_relaxed);
        return {
            replacement,
            {
                ReplacementShaderFamily::dFLightAmbient,
                static_cast<std::uint32_t>(contractIndex + 1),
                ReplacementPixelConstants_None,
            },
            true,
        };
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        shaderSelectionCalls_.fetch_add(1, std::memory_order_relaxed);
        const auto isCapturedContext = context == context_.Get();
        if (!isCapturedContext) {
            rejectedShaderContexts_.fetch_add(1, std::memory_order_relaxed);
            return { requested, {} };
        }

        if (currentlyRequestedShader_.Get() != requested) {
            currentlyRequestedShader_ = requested;
        }
        applyQueuedSettingsForRenderBoundary();

        if (!requested ||
            !enabled_.load(std::memory_order_acquire) ||
            !gpuResourcesReady_.load(std::memory_order_acquire)) {
            inactiveShaderSelections_.fetch_add(1, std::memory_order_relaxed);
            return { requested, {} };
        }

        const auto binding = decodeShaderBinding(
            shaderBindingLookup_.find(requested));
        if (binding.family == ReplacementShaderFamily::material) {
            if (!geometryProviderReady_.load(std::memory_order_acquire)) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }

            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > replacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                replacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }

            auto noContractObserved = 0u;
            firstReplacementContractPlusOne_.compare_exchange_strong(
                noContractObserved,
                binding.contractPlusOne,
                std::memory_order_release,
                std::memory_order_relaxed);
            replacementBinds_.fetch_add(1, std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::sky) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > skyReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                skyReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            skyReplacementBinds_.fetch_add(1, std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::distantTree) {
            if (binding.contractPlusOne != 1) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement = distantTreeReplacementShaders_[0].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            distantTreeReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::particle) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > particleReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                particleReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            particleReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::dFLightAmbient &&
            binding.contractPlusOne > 0) {
            return selectDFLightAmbientShader(
                requested,
                binding.contractPlusOne - 1);
        }
        unmatchedShaderSelections_.fetch_add(1, std::memory_order_relaxed);
        return { requested, {}, false };
    }

    ScopedReplacementPixelConstants Runtime::scopeReplacementPixelConstants(
        ID3D11DeviceContext* context,
        ReplacementShaderBinding binding) noexcept
    {
        const auto frameAndGeometry = static_cast<std::uint8_t>(
            ReplacementPixelConstants_Frame |
            ReplacementPixelConstants_Geometry);
        const auto validMaterial =
            binding.family == ReplacementShaderFamily::material &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= replacementShaders_.size() &&
            binding.constantFlags == frameAndGeometry;
        const auto validSky =
            binding.family == ReplacementShaderFamily::sky &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= skyReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validDistantTree =
            binding.family == ReplacementShaderFamily::distantTree &&
            binding.contractPlusOne == 1 &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validParticle =
            binding.family == ReplacementShaderFamily::particle &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= particleReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        if (!context || context != context_.Get() ||
            (!validMaterial && !validSky && !validDistantTree &&
                !validParticle) ||
            !frameBuffer_ ||
            (validMaterial && !geometryBuffer_)) {
            return ScopedReplacementPixelConstants{};
        }

        replacementConstantScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedReplacementPixelConstants(
            context,
            frameBuffer_.Get(),
            validMaterial ? geometryBuffer_.Get() : nullptr,
            binding.constantFlags,
            &replacementConstantRestores_);
    }

    std::uint32_t Runtime::inspectReplacementPipelineState(
        ID3D11DeviceContext* context,
        ReplacementShaderBinding binding) const noexcept
    {
        ID3D11PixelShader* expectedShader{};
        if (binding.family == ReplacementShaderFamily::material &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= replacementShaders_.size()) {
            expectedShader =
                replacementShaders_[binding.contractPlusOne - 1].Get();
        } else if (binding.family == ReplacementShaderFamily::sky &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= skyReplacementShaders_.size()) {
            expectedShader =
                skyReplacementShaders_[binding.contractPlusOne - 1].Get();
        } else if (
            binding.family == ReplacementShaderFamily::distantTree &&
            binding.contractPlusOne == 1) {
            expectedShader = distantTreeReplacementShaders_[0].Get();
        } else if (binding.family == ReplacementShaderFamily::particle &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= particleReplacementShaders_.size()) {
            expectedShader =
                particleReplacementShaders_[binding.contractPlusOne - 1].Get();
        }
        if (!context || context != context_.Get() || !expectedShader) {
            return 0;
        }

        ID3D11PixelShader* observedShader{};
        ID3D11Buffer* observedFrame{};
        ID3D11Buffer* observedGeometry{};
        context->PSGetShader(&observedShader, nullptr, nullptr);
        if ((binding.constantFlags & ReplacementPixelConstants_Frame) != 0) {
            context->PSGetConstantBuffers(5, 1, &observedFrame);
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Geometry) != 0) {
            context->PSGetConstantBuffers(8, 1, &observedGeometry);
        }

        std::uint32_t state{};
        if (observedShader == expectedShader) {
            state |= PipelineBinding_SelectedReplacement;
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Frame) != 0 &&
            observedFrame == frameBuffer_.Get()) {
            state |= PipelineBinding_FrameBuffer;
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Geometry) != 0 &&
            observedGeometry == geometryBuffer_.Get()) {
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

    bool Runtime::dFLightAmbientDescriptorReady(
        std::uint32_t descriptor) const noexcept
    {
        const auto found = std::lower_bound(
            kDFLightAmbientDescriptorContracts.begin(),
            kDFLightAmbientDescriptorContracts.end(),
            descriptor,
            [](const DFLightAmbientDescriptorContract& contract,
                std::uint32_t value) {
                return contract.descriptor < value;
            });
        if (found == kDFLightAmbientDescriptorContracts.end() ||
            found->descriptor != descriptor ||
            found->contractIndex >= kDFLightAmbientShaderContractCount) {
            return false;
        }
        return (readyDFLightAmbientContractMask_.load(
                    std::memory_order_acquire) &
                   (1ull << found->contractIndex)) != 0;
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
        auto next = sanitize(settings);
        if (next.ambientGamma != settings_.ambientGamma && device_ &&
            !rebuildDFLightAmbientReplacements(next.ambientGamma)) {
            logging::error(
                "DFLight ambient gamma update failed to rebuild every observed replacement; retaining gamma {} while applying the remaining settings.",
                settings_.ambientGamma);
            next.ambientGamma = settings_.ambientGamma;
        }
        settings_ = next;
        dFLightAmbientGammaBits_.store(
            std::bit_cast<std::uint32_t>(settings_.ambientGamma),
            std::memory_order_release);
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
            .verifiedSkyShaderContracts =
                static_cast<std::uint32_t>(kSkyShaderContracts.size()),
            .matchingSkyShaderContractMask =
                matchingSkyShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingSkyShadersCreated =
                matchingSkyShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalSkyShaders =
                trackedOriginalSkyShaders_.load(
                    std::memory_order_relaxed),
            .skyReplacementBinds =
                skyReplacementBinds_.load(std::memory_order_relaxed),
            .verifiedDistantTreeShaderContracts =
                static_cast<std::uint32_t>(
                    kDistantTreeShaderContractCount),
            .matchingDistantTreeShaderContractMask =
                matchingDistantTreeShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingDistantTreeShadersCreated =
                matchingDistantTreeShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalDistantTreeShaders =
                trackedOriginalDistantTreeShaders_.load(
                    std::memory_order_relaxed),
            .distantTreeReplacementBinds =
                distantTreeReplacementBinds_.load(
                    std::memory_order_relaxed),
            .verifiedParticleShaderContracts =
                static_cast<std::uint32_t>(kParticleShaderContracts.size()),
            .matchingParticleShaderContractMask =
                matchingParticleShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingParticleShadersCreated =
                matchingParticleShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalParticleShaders =
                trackedOriginalParticleShaders_.load(
                    std::memory_order_relaxed),
            .particleReplacementBinds =
                particleReplacementBinds_.load(std::memory_order_relaxed),
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
            .shaderBindingLookupFailures =
                shaderBindingLookupFailures_.load(std::memory_order_relaxed),
            .verifiedDFLightAmbientShaderContracts =
                static_cast<std::uint32_t>(
                    kDFLightAmbientContracts.size()),
            .matchingDFLightAmbientContractMask =
                matchingDFLightAmbientContractMask_.load(
                    std::memory_order_relaxed),
            .readyDFLightAmbientContractMask =
                readyDFLightAmbientContractMask_.load(
                    std::memory_order_acquire),
            .matchingDFLightAmbientShaders =
                matchingDFLightAmbientShaders_.load(
                    std::memory_order_relaxed),
            .trackedDFLightAmbientShaders =
                trackedDFLightAmbientShaders_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementBinds =
                dFLightAmbientReplacementBinds_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementBuilds =
                dFLightAmbientReplacementBuilds_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementFailures =
                dFLightAmbientReplacementFailures_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientGammaRebuilds =
                dFLightAmbientGammaRebuilds_.load(
                    std::memory_order_relaxed),
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
