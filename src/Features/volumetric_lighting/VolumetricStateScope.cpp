#include "Features/volumetric_lighting/VolumetricStateScope.h"

#include <algorithm>

namespace community_shaders::volumetric_lighting
{
    namespace
    {
        template <std::size_t Size>
        void releaseInterfaces(std::array<ID3D11ClassInstance*, Size>& values,
                               UINT count) noexcept
        {
            const auto bounded = std::min(
                values.size(), static_cast<std::size_t>(count));
            for (std::size_t index = 0; index < bounded; ++index) {
                if (values[index]) {
                    values[index]->Release();
                    values[index] = nullptr;
                }
            }
        }

        template <class Interface, std::size_t Size>
        void releaseInterfaces(std::array<Interface*, Size>& values) noexcept
        {
            for (auto*& value : values) {
                if (value) {
                    value->Release();
                    value = nullptr;
                }
            }
        }
    }

    StateScope::StateScope(ID3D11DeviceContext* context) noexcept :
        context_(context)
    {
        if (!context_) {
            return;
        }
        context_->OMGetRenderTargets(
            static_cast<UINT>(renderTargets_.size()),
            renderTargets_.data(),
            &depthStencil_);
        context_->IAGetInputLayout(inputLayout_.GetAddressOf());
        context_->IAGetPrimitiveTopology(&topology_);

        vertexClassCount_ = static_cast<UINT>(vertexClasses_.size());
        context_->VSGetShader(
            vertexShader_.GetAddressOf(),
            vertexClasses_.data(),
            &vertexClassCount_);
        pixelClassCount_ = static_cast<UINT>(pixelClasses_.size());
        context_->PSGetShader(
            pixelShader_.GetAddressOf(),
            pixelClasses_.data(),
            &pixelClassCount_);
        geometryClassCount_ = static_cast<UINT>(geometryClasses_.size());
        context_->GSGetShader(
            geometryShader_.GetAddressOf(),
            geometryClasses_.data(),
            &geometryClassCount_);
        hullClassCount_ = static_cast<UINT>(hullClasses_.size());
        context_->HSGetShader(
            hullShader_.GetAddressOf(),
            hullClasses_.data(),
            &hullClassCount_);
        domainClassCount_ = static_cast<UINT>(domainClasses_.size());
        context_->DSGetShader(
            domainShader_.GetAddressOf(),
            domainClasses_.data(),
            &domainClassCount_);
        computeClassCount_ = static_cast<UINT>(computeClasses_.size());
        context_->CSGetShader(
            computeShader_.GetAddressOf(),
            computeClasses_.data(),
            &computeClassCount_);

        context_->PSGetShaderResources(
            0, kPixelResources, pixelResources_.data());
        context_->PSGetSamplers(0, kPixelSamplers, pixelSamplers_.data());
        context_->PSGetConstantBuffers(
            0, kPixelConstants, pixelConstants_.data());
        context_->CSGetShaderResources(
            0, kComputeResources, computeResources_.data());
        context_->CSGetSamplers(0, kComputeSamplers, computeSamplers_.data());
        context_->CSGetConstantBuffers(
            0, kComputeConstants, computeConstants_.data());
        context_->CSGetUnorderedAccessViews(
            0, kComputeOutputs, computeOutputs_.data());

        context_->OMGetBlendState(
            blendState_.GetAddressOf(), blendFactor_, &sampleMask_);
        context_->OMGetDepthStencilState(
            depthState_.GetAddressOf(), &stencilReference_);
        context_->RSGetState(rasterState_.GetAddressOf());
        viewportCount_ = static_cast<UINT>(viewports_.size());
        context_->RSGetViewports(&viewportCount_, viewports_.data());
        context_->GetPredication(predicate_.GetAddressOf(), &predicateValue_);
    }

    StateScope::~StateScope() noexcept
    {
        if (!context_) {
            return;
        }
        context_->OMSetRenderTargets(
            static_cast<UINT>(renderTargets_.size()),
            renderTargets_.data(),
            depthStencil_);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetPrimitiveTopology(topology_);
        context_->VSSetShader(
            vertexShader_.Get(), vertexClasses_.data(), vertexClassCount_);
        context_->PSSetShader(
            pixelShader_.Get(), pixelClasses_.data(), pixelClassCount_);
        context_->GSSetShader(
            geometryShader_.Get(), geometryClasses_.data(), geometryClassCount_);
        context_->HSSetShader(
            hullShader_.Get(), hullClasses_.data(), hullClassCount_);
        context_->DSSetShader(
            domainShader_.Get(), domainClasses_.data(), domainClassCount_);
        context_->CSSetShader(
            computeShader_.Get(), computeClasses_.data(), computeClassCount_);
        context_->PSSetShaderResources(
            0, kPixelResources, pixelResources_.data());
        context_->PSSetSamplers(0, kPixelSamplers, pixelSamplers_.data());
        context_->PSSetConstantBuffers(
            0, kPixelConstants, pixelConstants_.data());
        context_->CSSetUnorderedAccessViews(
            0, kComputeOutputs, computeOutputs_.data(), nullptr);
        context_->CSSetShaderResources(
            0, kComputeResources, computeResources_.data());
        context_->CSSetSamplers(0, kComputeSamplers, computeSamplers_.data());
        context_->CSSetConstantBuffers(
            0, kComputeConstants, computeConstants_.data());
        context_->OMSetBlendState(blendState_.Get(), blendFactor_, sampleMask_);
        context_->OMSetDepthStencilState(
            depthState_.Get(), stencilReference_);
        context_->RSSetState(rasterState_.Get());
        context_->RSSetViewports(viewportCount_, viewports_.data());
        context_->SetPredication(predicate_.Get(), predicateValue_);

        releaseInterfaces(renderTargets_);
        if (depthStencil_) {
            depthStencil_->Release();
            depthStencil_ = nullptr;
        }
        releaseInterfaces(pixelResources_);
        releaseInterfaces(pixelSamplers_);
        releaseInterfaces(pixelConstants_);
        releaseInterfaces(computeResources_);
        releaseInterfaces(computeSamplers_);
        releaseInterfaces(computeConstants_);
        releaseInterfaces(computeOutputs_);
        releaseInterfaces(vertexClasses_, vertexClassCount_);
        releaseInterfaces(pixelClasses_, pixelClassCount_);
        releaseInterfaces(geometryClasses_, geometryClassCount_);
        releaseInterfaces(hullClasses_, hullClassCount_);
        releaseInterfaces(domainClasses_, domainClassCount_);
        releaseInterfaces(computeClasses_, computeClassCount_);
    }
}
