#pragma once
#include "PCH.h"

class ShaderReplacer {
public:
    static ShaderReplacer& GetSingleton();

    // Walk scatter table for a shader type and replace all permutations with custom-compiled versions
    void ReplaceAllPermutations(void* bsShader, uint32_t shaderType);

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

private:
    ShaderReplacer() = default;

    // BSTScatterTable internal layout (from Ghidra findings):
    //   +0x00: capacityMask (uint32, = bucketCount - 1)
    //   +0x04: unknown (uint32)
    //   +0x08: sentinel node pointer (void*)
    //   +0x10: bucket array pointer (ScatterEntry*)
    struct ScatterEntry {
        uint32_t key;
        uint32_t pad;
        void* value;       // Points to BSGraphics::PixelShader or VertexShader struct
        ScatterEntry* next;
    };

    // Walk all entries in a scatter table at bsShader + tableOffset
    void WalkScatterTable(void* bsShader, size_t tableOffset,
                          std::function<void(uint32_t techniqueID, void* shaderObj)> callback);

    // Scatter table offsets within BSShader (from Ghidra RE)
    static constexpr size_t kVSTableOffset = 0x28;
    static constexpr size_t kPSTableOffset = 0xB8;

    // D3D shader pointer offset within BSGraphics shader blob
    static constexpr size_t kD3DShaderPtrOffset = 0x08;
};
