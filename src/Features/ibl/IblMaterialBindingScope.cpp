#include "Features/ibl/IblMaterialBindingScope.h"

#include <utility>

namespace community_shaders::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11DeviceChild* child) noexcept
        {
            if (!expected || !child) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            child->GetDevice(&device);
            return device.Get() == expected;
        }

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11ShaderResourceView* view) noexcept
        {
            if (!expected || !view) {
                return false;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            resource->GetDevice(&device);
            return device.Get() == expected;
        }
    }

    ScopedMaterialBindings::ScopedMaterialBindings(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* albedo,
        ID3D11ShaderResourceView* radiance,
        ID3D11ShaderResourceView* validity,
        ID3D11Buffer* constants) noexcept
    {
        if (!context) {
            return;
        }
        if (!constants) {
            rejection_ = MaterialBindingRejection::missingConstants;
            return;
        }

        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (!device || !sameDevice(device.Get(), constants) ||
            (albedo && !sameDevice(device.Get(), albedo)) ||
            (radiance && !sameDevice(device.Get(), radiance)) ||
            (validity && !sameDevice(device.Get(), validity))) {
            rejection_ = MaterialBindingRejection::deviceMismatch;
            return;
        }

        context_ = context;
        std::array<ID3D11ShaderResourceView*, 3> previousResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(previousResources.size()),
            previousResources.data());
        for (std::size_t index = 0; index < previousResources.size(); ++index) {
            previousResources_[index].Attach(previousResources[index]);
        }
        ID3D11Buffer* previousConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &previousConstants);
        previousConstants_.Attach(previousConstants);
        captured_ = true;

        std::array<ID3D11ShaderResourceView*, 3> resources{
            albedo,
            radiance,
            validity,
        };
        context_->PSSetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(resources.size()),
            resources.data());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);

        std::array<ID3D11ShaderResourceView*, 3> appliedResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(appliedResources.size()),
            appliedResources.data());
        ID3D11Buffer* appliedConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &appliedConstants);
        const auto applied = appliedResources[0] == albedo &&
            appliedResources[1] == radiance &&
            appliedResources[2] == validity && appliedConstants == constants;
        for (auto* resource : appliedResources) {
            if (resource) {
                resource->Release();
            }
        }
        if (appliedConstants) {
            appliedConstants->Release();
        }
        if (!applied) {
            rejection_ = MaterialBindingRejection::applyFailed;
            (void)restoreCaptured();
            restored_ = true;
            return;
        }

        rejection_ = MaterialBindingRejection::none;
        active_ = true;
    }

    ScopedMaterialBindings::~ScopedMaterialBindings()
    {
        (void)restore();
    }

    ScopedMaterialBindings::ScopedMaterialBindings(
        ScopedMaterialBindings&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    ScopedMaterialBindings& ScopedMaterialBindings::operator=(
        ScopedMaterialBindings&& other) noexcept
    {
        if (this != &other) {
            (void)restore();
            moveFrom(std::move(other));
        }
        return *this;
    }

    bool ScopedMaterialBindings::active() const noexcept
    {
        return active_ && !restored_;
    }

    bool ScopedMaterialBindings::restore() noexcept
    {
        if (restored_) {
            return true;
        }
        if (!captured_) {
            restored_ = true;
            active_ = false;
            return false;
        }
        auto restored = restoreCaptured();
        if (!restored) {
            restored = restoreCaptured();
        }
        restored_ = true;
        active_ = false;
        return restored;
    }

    MaterialBindingRejection ScopedMaterialBindings::rejection() const noexcept
    {
        return rejection_;
    }

    bool ScopedMaterialBindings::restoreCaptured() noexcept
    {
        if (!context_ || !captured_) {
            return false;
        }
        std::array<ID3D11ShaderResourceView*, 3> resources{
            previousResources_[0].Get(),
            previousResources_[1].Get(),
            previousResources_[2].Get(),
        };
        auto* constants = previousConstants_.Get();
        context_->PSSetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(resources.size()),
            resources.data());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);

        std::array<ID3D11ShaderResourceView*, 3> restoredResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(restoredResources.size()),
            restoredResources.data());
        ID3D11Buffer* restoredConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &restoredConstants);
        const auto restored =
            restoredResources[0] == resources[0] &&
            restoredResources[1] == resources[1] &&
            restoredResources[2] == resources[2] &&
            restoredConstants == constants;
        for (auto* resource : restoredResources) {
            if (resource) {
                resource->Release();
            }
        }
        if (restoredConstants) {
            restoredConstants->Release();
        }
        return restored;
    }

    void ScopedMaterialBindings::moveFrom(
        ScopedMaterialBindings&& other) noexcept
    {
        context_ = std::move(other.context_);
        previousResources_ = std::move(other.previousResources_);
        previousConstants_ = std::move(other.previousConstants_);
        rejection_ = other.rejection_;
        captured_ = other.captured_;
        active_ = other.active_;
        restored_ = other.restored_;
        other.captured_ = false;
        other.active_ = false;
        other.restored_ = true;
    }
}
