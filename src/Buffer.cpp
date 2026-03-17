#include "Buffer.h"
#include "Globals.h"

// ---------------------------------------------------------------------------
// ConstantBuffer
// ---------------------------------------------------------------------------

ConstantBuffer::ConstantBuffer(size_t size)
{
    // 16-byte align the size (required for constant buffers)
    m_size = (size + 15u) & ~static_cast<size_t>(15u);

    auto* device = Globals::GetDevice();
    if (!device) {
        spdlog::error("ConstantBuffer::ctor — device is null, cannot create buffer of size {}", m_size);
        return;
    }

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(m_size);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_buffer);
    if (FAILED(hr)) {
        spdlog::error("ConstantBuffer::ctor — CreateBuffer failed (size={}, hr={:#x})", m_size, static_cast<uint32_t>(hr));
        m_buffer = nullptr;
    } else {
        spdlog::trace("ConstantBuffer::ctor — created buffer {} bytes @ {}", m_size, fmt::ptr(m_buffer));
    }
}

ConstantBuffer::~ConstantBuffer()
{
    if (m_buffer) {
        m_buffer->Release();
        m_buffer = nullptr;
    }
}

void ConstantBuffer::Update(const void* data, size_t size)
{
    if (!m_buffer) {
        spdlog::warn("ConstantBuffer::Update — buffer is null");
        return;
    }

    if (size > m_size) {
        spdlog::error("ConstantBuffer::Update — data size {} exceeds buffer capacity {}", size, m_size);
        return;
    }

    auto* ctx = Globals::GetContext();
    if (!ctx) {
        spdlog::error("ConstantBuffer::Update — context is null");
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::error("ConstantBuffer::Update — Map failed (hr={:#x})", static_cast<uint32_t>(hr));
        return;
    }

    std::memcpy(mapped.pData, data, size);
    ctx->Unmap(m_buffer, 0);
}

void ConstantBuffer::VSBind(uint32_t slot)
{
    if (!m_buffer) {
        return;
    }

    auto* ctx = Globals::GetContext();
    if (ctx) {
        ctx->VSSetConstantBuffers(slot, 1, &m_buffer);
    }
}

void ConstantBuffer::PSBind(uint32_t slot)
{
    if (!m_buffer) {
        return;
    }

    auto* ctx = Globals::GetContext();
    if (ctx) {
        ctx->PSSetConstantBuffers(slot, 1, &m_buffer);
    }
}

void ConstantBuffer::CSBind(uint32_t slot)
{
    if (!m_buffer) {
        return;
    }

    auto* ctx = Globals::GetContext();
    if (ctx) {
        ctx->CSSetConstantBuffers(slot, 1, &m_buffer);
    }
}

// ---------------------------------------------------------------------------
// ScopedD3DState
// ---------------------------------------------------------------------------

ScopedD3DState::ScopedD3DState()
{
    // Nothing to do on construction — we invalidate on destruction.
}

ScopedD3DState::~ScopedD3DState()
{
    uintptr_t state = Globals::GetGraphicsState();
    if (!state) {
        spdlog::warn("ScopedD3DState::~dtor — GraphicsState is null, cannot invalidate dirty flags");
        return;
    }

    // Force the game to rebind all PS SRVs (t0-t15) and PS samplers (s0-s15)
    // by setting all dirty bits in the state cache.
    auto* psSrvDirty = reinterpret_cast<uint32_t*>(state + 0x1EF4);
    auto* psSamplerDirty = reinterpret_cast<uint32_t*>(state + 0x1EF8);

    *psSrvDirty = 0xFFFF;
    *psSamplerDirty = 0xFFFF;

    spdlog::trace("ScopedD3DState — invalidated PS SRV + sampler dirty flags at state {:#x}", state);
}
