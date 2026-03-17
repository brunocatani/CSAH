#pragma once
#include "PCH.h"

class ConstantBuffer {
public:
    ConstantBuffer(size_t size);
    ~ConstantBuffer();

    ConstantBuffer(const ConstantBuffer&) = delete;
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;

    void Update(const void* data, size_t size);
    void VSBind(uint32_t slot);
    void PSBind(uint32_t slot);
    void CSBind(uint32_t slot);

    ID3D11Buffer* Get() const { return m_buffer; }
    bool IsValid() const { return m_buffer != nullptr; }

private:
    ID3D11Buffer* m_buffer = nullptr;
    size_t m_size = 0;
};

// RAII helper: invalidates game's state cache dirty flags on destruction.
// Use this around any direct D3D11 calls (PSSetShaderResources, CSSetShader, etc.)
// to prevent the game's deferred state tracking from getting out of sync.
//
// Dirty flag layout at BSGraphics::State + 0x1EE0:
//   +0x1EF4 (uint32): PS SRV dirty bitmask (bits 0-15 for t0-t15)
//   +0x1EF8 (uint32): PS sampler dirty bitmask (bits 0-15 for s0-s15)
struct ScopedD3DState {
    ScopedD3DState();
    ~ScopedD3DState();
};
