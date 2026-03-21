#pragma once
#include "PCH.h"
#include <mutex>

class ShaderReplacer {
public:
    static ShaderReplacer& GetSingleton();

    // Thread safety for concurrent access from hook callbacks
    std::mutex replacerMutex;

    // Walk scatter table for a shader type and replace all permutations with custom-compiled versions
    void ReplaceAllPermutations(void* bsShader, uint32_t shaderType);

    // Walk scatter table for a shader type and replace only PS permutations matching the filter predicate
    void ReplaceFilteredPermutations(void* bsShader, uint32_t shaderType,
        std::function<bool(uint32_t techniqueID)> filter);

    // Restore all vanilla PS pointers captured during filtered replacement
    void RestoreAllVanillaPS();

    // Replace a single technique's pixel shader
    bool ReplacePixelShader(void* bsShader, uint32_t techniqueID, ID3D11PixelShader* customPS);

    // Replace a single technique's vertex shader
    bool ReplaceVertexShader(void* bsShader, uint32_t techniqueID, ID3D11VertexShader* customVS);

    // Track replacements (prevent COM release)
    std::vector<Microsoft::WRL::ComPtr<ID3D11PixelShader>> keepAlivePS;
    std::vector<Microsoft::WRL::ComPtr<ID3D11VertexShader>> keepAliveVS;

    // Vanilla fallbacks (captured before replacement)
    std::unordered_map<uint64_t, ID3D11PixelShader*> vanillaPS;
    std::unordered_map<uint64_t, ID3D11VertexShader*> vanillaVS;

    // Slot addresses for vanilla PS rollback (populated by ReplaceFilteredPermutations)
    std::unordered_map<uint64_t, ID3D11PixelShader**> vanillaPSSlots;

private:
    ShaderReplacer() = default;

    // BSTScatterTable internal layout (from Ghidra findings):
    // BSTScatterTable layout (from Ghidra RE of FO4VR BSShader::BeginTechnique):
    //   +0x00: unknown (uint32)
    //   +0x04: bucketCount (uint32) — capacityMask = bucketCount - 1
    //   +0x08: unknown
    //   +0x10: sentinel node pointer (void*)
    //   +0x20: bucket array pointer (ScatterEntry*)
    // VS/PS entries are 0x10 bytes per bucket slot:
    //   [0x00]: pointer to shader data (first uint32 at data = technique key)
    //   [0x08]: next entry pointer (for chaining)
    struct ScatterEntry {
        void* data;          // Points to BSGraphics shader struct; key = *(uint32_t*)data
        ScatterEntry* next;  // Next in chain (nullptr or sentinel = end)
    };

    // Walk all entries in a scatter table at bsShader + tableOffset
    void WalkScatterTable(void* bsShader, size_t tableOffset,
                          std::function<void(uint32_t techniqueID, void* shaderObj)> callback);

    // Scatter table offsets (from Ghidra FXP loader FUN_142814260)
    // FXP data loads into: VS=+0x28, HS=+0x58, DS=+0x88, PS=+0xB8, CS=+0xE8
    static constexpr size_t kVSTableOffset = 0x28;
    static constexpr size_t kPSTableOffset = 0xB8;

    // D3D shader pointer offset within BSGraphics shader blob
    static constexpr size_t kD3DShaderPtrOffset = 0x08;
};
