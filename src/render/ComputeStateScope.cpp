#include "render/ComputeStateScope.h"

namespace csah::render
{
    namespace
    {
        [[nodiscard]] constexpr bool validSpan(
            UINT first,
            UINT count,
            UINT capacity) noexcept
        {
            return first <= capacity && count <= capacity - first;
        }

        template <class T, std::size_t Size>
        void releaseRange(std::array<T*, Size>& values, UINT count) noexcept
        {
            for (UINT index = 0; index < count; ++index) {
                if (values[index]) {
                    values[index]->Release();
                    values[index] = nullptr;
                }
            }
        }
    }

    ScopedComputeState::ScopedComputeState(
        ID3D11DeviceContext* context,
        ComputeStateFootprint footprint) noexcept :
        context_(context),
        footprint_(footprint)
    {
        if (!context_ ||
            !validSpan(
                footprint_.firstShaderResource,
                footprint_.shaderResourceCount,
                D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) ||
            !validSpan(
                footprint_.firstUnorderedAccess,
                footprint_.unorderedAccessCount,
                D3D11_PS_CS_UAV_REGISTER_COUNT) ||
            !validSpan(
                footprint_.firstSampler,
                footprint_.samplerCount,
                D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) ||
            !validSpan(
                footprint_.firstConstantBuffer,
                footprint_.constantBufferCount,
                D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)) {
            context_ = nullptr;
            return;
        }

        classInstanceCount_ = static_cast<UINT>(classInstances_.size());
        context_->CSGetShader(
            &shader_,
            classInstances_.data(),
            &classInstanceCount_);
        if (footprint_.shaderResourceCount != 0) {
            context_->CSGetShaderResources(
                footprint_.firstShaderResource,
                footprint_.shaderResourceCount,
                shaderResources_.data());
        }
        if (footprint_.unorderedAccessCount != 0) {
            context_->CSGetUnorderedAccessViews(
                footprint_.firstUnorderedAccess,
                footprint_.unorderedAccessCount,
                unorderedAccessViews_.data());
        }
        if (footprint_.samplerCount != 0) {
            context_->CSGetSamplers(
                footprint_.firstSampler,
                footprint_.samplerCount,
                samplers_.data());
        }
        if (footprint_.constantBufferCount != 0) {
            context_->CSGetConstantBuffers(
                footprint_.firstConstantBuffer,
                footprint_.constantBufferCount,
                constantBuffers_.data());
        }
        captured_ = true;
    }

    ScopedComputeState::~ScopedComputeState()
    {
        (void)restore();
    }

    bool ScopedComputeState::restore() noexcept
    {
        if (!captured_ || !context_) {
            return false;
        }
        context_->CSSetShader(
            shader_,
            classInstances_.data(),
            classInstanceCount_);
        if (footprint_.shaderResourceCount != 0) {
            context_->CSSetShaderResources(
                footprint_.firstShaderResource,
                footprint_.shaderResourceCount,
                shaderResources_.data());
        }
        if (footprint_.unorderedAccessCount != 0) {
            context_->CSSetUnorderedAccessViews(
                footprint_.firstUnorderedAccess,
                footprint_.unorderedAccessCount,
                unorderedAccessViews_.data(),
                nullptr);
        }
        if (footprint_.samplerCount != 0) {
            context_->CSSetSamplers(
                footprint_.firstSampler,
                footprint_.samplerCount,
                samplers_.data());
        }
        if (footprint_.constantBufferCount != 0) {
            context_->CSSetConstantBuffers(
                footprint_.firstConstantBuffer,
                footprint_.constantBufferCount,
                constantBuffers_.data());
        }
        captured_ = false;
        releaseRetainedState();
        context_ = nullptr;
        return true;
    }

    void ScopedComputeState::releaseRetainedState() noexcept
    {
        releaseRange(classInstances_, classInstanceCount_);
        classInstanceCount_ = 0;
        if (shader_) {
            shader_->Release();
            shader_ = nullptr;
        }
        releaseRange(shaderResources_, footprint_.shaderResourceCount);
        releaseRange(unorderedAccessViews_, footprint_.unorderedAccessCount);
        releaseRange(samplers_, footprint_.samplerCount);
        releaseRange(constantBuffers_, footprint_.constantBufferCount);
    }
}
